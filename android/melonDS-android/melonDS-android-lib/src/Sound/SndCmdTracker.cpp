/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

/*
    RealtimeBGM: NitroSDK sound command tracker.
    See SndCmdTracker.h and bgm-realtime-feasibility.md sections 3, 5.1 and 6.
    Struct offsets below are from NitroSDK include/nitro/snd/common/{seq,exchannel,work,bank}.h
    with natural ARM alignment.
*/
#include <string.h>
#include "SndCmdTracker.h"
#include "../NDS.h"
#include "../CRC32.h"
#include "../Platform.h"
#include "../Savestate.h"

namespace melonDS::Sound
{

using Platform::Log;
using Platform::LogLevel;

namespace
{

enum : u32
{
    CMD_START_SEQ = 0, CMD_STOP_SEQ = 1, CMD_PREPARE_SEQ = 2, CMD_START_PREPARED_SEQ = 3, CMD_PAUSE_SEQ = 4,
    CMD_PLAYER_PARAM = 6, CMD_TRACK_PARAM = 7, CMD_MUTE_TRACK = 8, CMD_ALLOCATABLE_CHANNEL = 9,
    CMD_PLAYER_LOCAL_VAR = 10, CMD_MASTER_VOLUME = 23, CMD_SHARED_WORK = 29, CMD_INVALIDATE_SEQ = 30,
    CMD_INVALIDATE_BANK = 31, CMD_READ_DRIVER_INFO = 33,
};

constexpr u32 MAGIC_SSEQ = 0x51455353, MAGIC_SBNK = 0x4B4E4253, MAGIC_SWAR = 0x52415753;

// SNDWork layout: SNDExChannel channel[16] (84 bytes each), SNDPlayer player[16] (36), SNDTrack track[32] (64),
// SNDAlarm alarm[8]. sizeof(SNDAlarm) depends on the compiler's u64 alignment (72 with 8-byte, 64 with 4-byte),
// so both SNDWork sizes are tried and the ARM7 copy is used to confirm.
constexpr u32 EXCH_SIZE = 84, PLAYER_SIZE = 36, TRACK_SIZE = 64;
constexpr u32 WORK_PLAYER_OFS = 16 * EXCH_SIZE;
constexpr u32 WORK_TRACK_OFS = WORK_PLAYER_OFS + 16 * PLAYER_SIZE;
constexpr u32 WORK_SIZES[2] = {WORK_TRACK_OFS + 32 * TRACK_SIZE + 8 * 72, WORK_TRACK_OFS + 32 * TRACK_SIZE + 8 * 64};
constexpr u32 EXCH_CALLBACK_DATA_OFS = 76;
constexpr u32 PLAYER_TRACKS_OFS = 8;

// Games fade by sending one PLAYER_PARAM extFader per frame; a change after a longer gap is not
// part of a fade and takes one frame.
constexpr u32 MAX_FADER_GAP = 4;

constexpr u32 MAX_MML = 0x100000;
constexpr u32 MAX_BLOB = 0x400000;

bool IsMainRAM(u32 addr) { return (addr >> 24) == 0x02; }

u32 Get32(const u8* p) { u32 v; memcpy(&v, p, 4); return v; }

}

SndCmdTracker::SndCmdTracker(melonDS::NDS& nds) : NDS(nds)
{
    ChanOwner.fill(-1);
}

SndCmdTracker::~SndCmdTracker() = default;

void SndCmdTracker::Reset()
{
    Bgm.Kill();
    P = {};
    HostP = -1;
    MutedP = 0;
    NDS.SPU.ClearHostTags();
    CurMuteMask = 0;
    SharedWork = 0;
    DriverInfoAddr = 0;
    DriverInfoReq = 0;
    DriverInfoPending = false;
    DriverInfoLogged = -1;
    ChanOwner.fill(-1);
    ChanOwnerValid = false;
    TrackMute = {};
    memset(TrackFader, 0, sizeof(TrackFader));
    memset(TrackPitch, 0, sizeof(TrackPitch));
    memset(TrackPan, 0, sizeof(TrackPan));
    StartOrder = {};
    StartFrame = {};
    NoHost = {};
    StartCounter = 0;
    MasterVol = 127;
}

void SndCmdTracker::OnCartChanged(const u8* rom, u32 romLen)
{
    Reset();
    Idx.Clear();
    if (!rom) return;

    Idx.Build(rom, romLen);
    Log(LogLevel::Info, "RealtimeBGM: index has %u sequences in %u SDATs\n",
        (u32)Idx.Sequences().size(), (u32)Idx.SdatNames().size());
}

u32 SndCmdTracker::RamRead32(u32 addr) const
{
    u32 v = 0;
    for (int i = 0; i < 4; i++)
        v |= (u32)NDS.MainRAM[(addr + i) & NDS.MainRAMMask] << (i * 8);
    return v;
}

bool SndCmdTracker::CopyRAM(u32 addr, u32 len, std::vector<u8>& out) const
{
    if (len == 0 || !IsMainRAM(addr) || !IsMainRAM(addr + len - 1)) return false;
    out.resize(len);
    for (u32 i = 0; i < len; i++)
        out[i] = NDS.MainRAM[(addr + i) & NDS.MainRAMMask];
    return true;
}

// Copies a loaded SWAR. Wave archives loaded wave-by-wave (NNS "single load") contain only the
// header and offset table, with absolute main RAM addresses for the loaded waves (see
// SND_GetWaveDataAddress); those are rebuilt into a self-contained SWAR with relative offsets.
bool SndCmdTracker::CopySwar(u32 addr, std::vector<u8>& out) const
{
    if (!IsMainRAM(addr) || RamRead32(addr) != MAGIC_SWAR) return false;
    u32 fsize = RamRead32(addr + 8);
    u32 count = RamRead32(addr + 0x38);
    if (fsize < 0x3C || fsize > MAX_BLOB || count > 0x10000) return false;

    bool absolute = false;
    for (u32 i = 0; i < count; i++)
        if (RamRead32(addr + 0x3C + i * 4) >= 0x02000000) absolute = true;
    if (!absolute)
        return CopyRAM(addr, fsize, out);

    u32 hdrLen = 0x3C + count * 4;
    if (!CopyRAM(addr, hdrLen, out)) return false;
    for (u32 i = 0; i < count; i++)
    {
        u32 ofs = RamRead32(addr + 0x3C + i * 4);
        u32 wave = ofs >= 0x02000000 ? ofs : (ofs ? addr + ofs : 0);
        u32 newOfs = 0;
        if (wave && IsMainRAM(wave))
        {
            // SNDWaveParam: loopstart (u16 @6) and looplen (u32 @8) in words
            u32 len = 12 + 4 * ((RamRead32(wave + 4) >> 16) + RamRead32(wave + 8));
            std::vector<u8> w;
            if (len <= MAX_BLOB && out.size() + len <= MAX_BLOB && CopyRAM(wave, len, w))
            {
                newOfs = (u32)out.size();
                out.insert(out.end(), w.begin(), w.end());
            }
        }
        memcpy(&out[0x3C + i * 4], &newOfs, 4);
    }
    u32 total = (u32)out.size();
    u32 blockSize = total - 0x10;
    memcpy(&out[8], &total, 4);
    memcpy(&out[0x14], &blockSize, 4);
    return true;
}

void SndCmdTracker::OnPxiWord(u32 word)
{
    if ((word & 0x1F) != 7) return;
    u32 node = word >> 6;
    if (node < 0x02000000) return;

    struct Cmd { u32 ID, Args[4]; };
    std::array<Cmd, 256> cmds;
    int n = 0;
    for (; n < 256 && IsMainRAM(node); n++)
    {
        cmds[n].ID = NDS.ARM7Read32(node + 4);
        for (int i = 0; i < 4; i++)
            cmds[n].Args[i] = NDS.ARM7Read32(node + 8 + i * 4);
        node = NDS.ARM7Read32(node);
    }

    for (int i = 0; i < n; i++)
    {
        const Cmd& c = cmds[i];
        // NNS sends ALLOCATABLE_CHANNEL (possibly several, for different track sets) right after
        // (PREPARE|START)_SEQ in the same list; apply them first so runtime classification sees
        // this start's channel mask.
        if ((c.ID == CMD_START_SEQ || c.ID == CMD_PREPARE_SEQ) && c.Args[0] < 16)
        {
            u16 mask = 0;
            for (int j = i + 1; j < n; j++)
            {
                if ((cmds[j].ID == CMD_START_SEQ || cmds[j].ID == CMD_PREPARE_SEQ) && cmds[j].Args[0] == c.Args[0]) break;
                if (cmds[j].ID == CMD_ALLOCATABLE_CHANNEL && cmds[j].Args[0] == c.Args[0])
                    mask |= (u16)cmds[j].Args[2];
            }
            if (mask) P[c.Args[0]].ChanMask = mask;
        }
        HandleCommand(c.ID, c.Args[0], c.Args[1], c.Args[2], c.Args[3]);
    }

    // a STOP of the host must not leave the other BGM players unmuted until the end of the frame
    if (FF && Settings.Enabled && HostP < 0)
        PickHost();
}

void SndCmdTracker::HandleCommand(u32 id, u32 a0, u32 a1, u32 a2, u32 a3)
{
    switch (id)
    {
    case CMD_START_SEQ:
    case CMD_PREPARE_SEQ:
        if (a0 < 16) OnStart((int)a0, a1, a2, a3, id == CMD_PREPARE_SEQ);
        break;

    case CMD_START_PREPARED_SEQ:
        if (a0 < 16 && P[a0].Active && P[a0].Prepared)
        {
            P[a0].Prepared = false;
            StartFrame[a0] = FrameCount;
            if (Settings.Enabled && FF && Eligible(P[a0]))
                EnterHostMode((int)a0, true);
            UpdateMuteMask();
        }
        break;

    case CMD_STOP_SEQ:
        if (a0 < 16) OnStop((int)a0);
        break;

    case CMD_PAUSE_SEQ:
        if (a0 < 16)
        {
            P[a0].Paused = a1 != 0;
            if ((int)a0 == HostP)
            {
                Log(LogLevel::Debug, "RealtimeBGM: host player %d %s\n", HostP, a1 ? "paused" : "resumed");
                Bgm.Pause(a1 != 0);
            }
        }
        break;

    case CMD_PLAYER_PARAM:
        if (a0 < 16)
        {
            s32 val = a3 == 1 ? (s32)(s8)a2 : a3 == 2 ? (s32)(s16)a2 : (s32)a2;
            if (a1 == 6)
            {
                // the host replays fader changes at their 1x pace, which keeps fades at their 1x
                // length while fast-forwarding
                u32 gap = FrameCount - P[a0].FaderFrame;
                P[a0].ExtFader = (s16)val;
                P[a0].FaderFrame = FrameCount;
                if ((int)a0 == HostP) Bgm.SetExtFader((s16)val, gap >= 1 && gap <= MAX_FADER_GAP ? gap : 1);
            }
            else if (a1 == 0x1A)
            {
                P[a0].TempoRatio = (u16)val;
                if ((int)a0 == HostP) Bgm.SetTempoRatio((u16)val);
            }
        }
        break;

    case CMD_TRACK_PARAM:
        {
            u32 player = a0 & 0xFFFFFF, size = a0 >> 24;
            if (player >= 16) break;
            s32 val = size == 1 ? (s32)(s8)a3 : size == 2 ? (s32)(s16)a3 : (s32)a3;
            // remembered for every player so a host adopted mid-song starts with the real track levels
            for (int t = 0; t < 16; t++)
            {
                if (!(a1 & (1 << t))) continue;
                if (a2 == 0xA) TrackFader[player][t] = (s16)val;
                else if (a2 == 0xC) TrackPitch[player][t] = (s16)val;
                else if (a2 == 9) TrackPan[player][t] = (s8)val;
            }
            if ((int)player != HostP) break;
            if (a2 == 0xA) Bgm.SetTrackFader((u16)a1, (s16)val);
            else if (a2 == 0xC) Bgm.SetTrackPitch((u16)a1, (s16)val);
            else if (a2 == 9) Bgm.SetTrackPan((u16)a1, (s8)val);
        }
        break;

    case CMD_MUTE_TRACK:
        if (a0 < 16)
        {
            if (a2) TrackMute[a0] |= (u16)a1;
            else TrackMute[a0] &= ~(u16)a1;
            if ((int)a0 == HostP) Bgm.MuteTracks((u16)a1, a2 != 0);
        }
        break;

    case CMD_ALLOCATABLE_CHANNEL:
        if (a0 < 16)
        {
            P[a0].ChanMask |= (u16)a2;
            if ((int)a0 == HostP) Bgm.SetChannelMask(P[a0].ChanMask);
        }
        break;

    case CMD_PLAYER_LOCAL_VAR:
        if ((int)a0 == HostP && a1 < 16) Bgm.SetVariable((u8)a1, (s16)a2);
        break;

    case CMD_MASTER_VOLUME:
        MasterVol = (u8)a0;
        Bgm.SetMasterVolume(MasterVol);
        break;

    case CMD_INVALIDATE_SEQ:
    case CMD_INVALIDATE_BANK:
        // NNS frees a heap without STOP_SEQ; the driver finishes every player whose sequence
        // data (or bank) lies in the freed range
        for (int p = 0; p < 16; p++)
        {
            const PlayerState& s = P[p];
            if (!s.Active) continue;
            bool hit = id == CMD_INVALIDATE_SEQ ? (s.MML <= a1 && s.MML + s.MMLLen >= a0)
                                                : (s.Bank >= a0 && s.Bank <= a1);
            if (hit) OnStop(p);
        }
        break;

    case CMD_SHARED_WORK:
        SharedWork = IsMainRAM(a0) ? a0 : 0;
        break;

    case CMD_READ_DRIVER_INFO:
        // NNS double-buffers driver info and only requests a new snapshot once the previous
        // one completed, so the buffer requested before this one is complete and stable.
        if (a0 != DriverInfoReq)
        {
            DriverInfoAddr = DriverInfoReq;
            DriverInfoReq = a0;
        }
        DriverInfoPending = DriverInfoAddr != 0;
        break;
    }
}

bool SndCmdTracker::Eligible(const PlayerState& s) const
{
    switch (s.Cls)
    {
    case SeqClass::BGM: return true;
    case SeqClass::Jingle: return Settings.JinglesAt1x;
    case SeqClass::Ambient: return Settings.AmbientAt1x;
    default: return false;
    }
}

void SndCmdTracker::OnStart(int player, u32 mml, u32 offset, u32 bank, bool prepareOnly)
{
    if (player == HostP) LeaveHostMode();
    // the driver stops the old sequence; the new one's notes are tagged at key-on
    MutedP &= ~(1 << player);

    PlayerState& s = P[player];
    u16 chanMask = s.ChanMask;
    s = PlayerState();
    s.ChanMask = chanMask;
    s.Active = true;
    s.Prepared = prepareOnly;
    s.MML = mml;
    s.Bank = bank;
    TrackMute[player] = 0;
    memset(TrackFader[player], 0, sizeof(TrackFader[player]));
    memset(TrackPitch[player], 0, sizeof(TrackPitch[player]));
    memset(TrackPan[player], 0, sizeof(TrackPan[player]));
    NoHost[player] = false;
    StartOrder[player] = ++StartCounter;
    StartFrame[player] = FrameCount;

    // NNS passes the sequence data of a whole SSEQ file, so the header sits right before it.
    // A non-zero offset means a sequence inside an SSAR, which the host renderer does not handle.
    u32 hdr = mml - 0x1C;
    if (offset == 0 && IsMainRAM(hdr) && RamRead32(hdr) == MAGIC_SSEQ)
    {
        u32 fsize = RamRead32(hdr + 8);
        if (fsize > 0x1C && fsize - 0x1C <= MAX_MML)
            s.MMLLen = fsize - 0x1C;
    }

    std::vector<u8> buf;
    if (s.MMLLen && CopyRAM(mml, s.MMLLen, buf))
    {
        s.CRC = CRC32(buf.data(), (int)s.MMLLen);
        s.Info = Idx.Lookup(s.CRC, s.MMLLen);
        if (s.Info)
            s.Cls = s.Info->Cls;
        else
            s.Cls = ClassifySeq(AnalyzeMML(buf.data(), s.MMLLen), s.ChanMask);
    }
    else
        s.MMLLen = 0;

    Log(LogLevel::Info, "RealtimeBGM: start player %d %s (%s) crc=%08X len=%u%s\n", player,
        s.Info && !s.Info->Name.empty() ? s.Info->Name.c_str() : "?", SeqClassName(s.Cls), s.CRC, s.MMLLen,
        prepareOnly ? " [prepared]" : "");

    if (!prepareOnly && Settings.Enabled && FF && Eligible(s))
        EnterHostMode(player, true);
    UpdateMuteMask();
}

void SndCmdTracker::OnStop(int player)
{
    P[player].Active = false;
    P[player].Prepared = false;
    if (player == HostP)
        LeaveHostMode();
}

bool SndCmdTracker::EnterHostMode(int player, bool fromStart)
{
    PlayerState& s = P[player];
    std::vector<u8> mml, sbnk, swar[4];

    bool ok = s.MMLLen && CopyRAM(s.MML, s.MMLLen, mml) && CRC32(mml.data(), (int)s.MMLLen) == s.CRC;
    // Bank and wave data live in the game's sound heap, which it may be rebuilding at the moment a
    // paused song is re-adopted (e.g. right after a battle theme stops). The blobs captured when the
    // song was first started are the ones the driver is playing, so prefer them.
    bool cached = false;
    if (ok)
    {
        for (const SongBlobs& c : BlobCache)
        {
            if (c.CRC != s.CRC || c.MML.empty()) continue;
            sbnk = c.Bank;
            for (int i = 0; i < 4; i++) swar[i] = c.Swar[i];
            cached = true;
            break;
        }
    }
    if (ok && !cached)
    {
        u32 bsize = RamRead32(s.Bank + 8);
        ok = IsMainRAM(s.Bank) && RamRead32(s.Bank) == MAGIC_SBNK && bsize >= 0x3C && bsize <= MAX_BLOB
            && CopyRAM(s.Bank, bsize, sbnk);
    }
    if (ok && !cached)
    {
        // SNDBankData.waveArcLink[4] (8 bytes each: waveArc, next) follows the 0x18-byte headers;
        // SND_AssignWaveArc fills waveArc with the address of the loaded SWAR.
        for (int i = 0; i < 4; i++)
        {
            u32 arc = RamRead32(s.Bank + 0x18 + i * 8);
            if (arc && !CopySwar(arc, swar[i]))
            {
                Log(LogLevel::Info, "RealtimeBGM: player %d wave archive %d at %08X could not be copied\n", player, i, arc);
                swar[i].clear();
                ok = false;
            }
        }
    }

    // fall back to the blobs captured when this sequence was first hosted
    if (!ok)
    {
        for (const SongBlobs& c : BlobCache)
        {
            if (c.CRC != s.CRC || c.MML.empty()) continue;
            mml = c.MML; sbnk = c.Bank;
            for (int i = 0; i < 4; i++) swar[i] = c.Swar[i];
            ok = true;
            Log(LogLevel::Info, "RealtimeBGM: player %d uses cached song data\n", player);
            break;
        }
    }

    const u8* swarPtr[4];
    u32 swarLen[4];
    for (int i = 0; i < 4; i++)
    {
        swarPtr[i] = swar[i].empty() ? nullptr : swar[i].data();
        swarLen[i] = (u32)swar[i].size();
    }

    // the current host keeps playing if this player cannot be loaded
    if (!ok || !Bgm.Load(mml.data(), s.MMLLen, sbnk.data(), (u32)sbnk.size(), swarPtr, swarLen))
    {
        Log(LogLevel::Warn, "RealtimeBGM: could not load player %d (%s) into the host renderer\n", player,
            s.Info && !s.Info->Name.empty() ? s.Info->Name.c_str() : "?");
        NoHost[player] = true;
        return false;
    }

    // remember the blobs for later re-adoptions (ring of the last few songs)
    {
        bool known = false;
        for (const SongBlobs& c : BlobCache)
            if (c.CRC == s.CRC && !c.MML.empty()) known = true;
        if (!known)
        {
            SongBlobs& c = BlobCache[BlobCacheNext];
            BlobCacheNext = (BlobCacheNext + 1) % BlobCache.size();
            c.CRC = s.CRC; c.MML = mml; c.Bank = sbnk;
            for (int i = 0; i < 4; i++) c.Swar[i] = swar[i];
        }
    }

    if (HostP >= 0) LeaveHostMode();

    // the driver keeps the player's notes on its allocatable channels (all of them until ALLOCATABLE_CHANNEL)
    Bgm.SetChannelMask(s.ChanMask ? s.ChanMask : 0xFFFF);
    ApplyOutputSettings();
    u32 tick = fromStart ? 0 : TickCounter(player);
    Bgm.Start(tick);
    Log(LogLevel::Debug, "RealtimeBGM: host enter player %d tick %u fader %d tempo %u paused %d\n", player, tick, s.ExtFader, s.TempoRatio, s.Paused);
    Bgm.SetMasterVolume(MasterVol);
    Bgm.SetExtFader(s.ExtFader);
    Bgm.SetTempoRatio(s.TempoRatio);
    if (TrackMute[player]) Bgm.MuteTracks(TrackMute[player], true);
    for (int t = 0; t < 16; t++)
    {
        if (TrackFader[player][t]) Bgm.SetTrackFader(1 << t, TrackFader[player][t]);
        if (TrackPitch[player][t]) Bgm.SetTrackPitch(1 << t, TrackPitch[player][t]);
        if (TrackPan[player][t]) Bgm.SetTrackPan(1 << t, TrackPan[player][t]);
    }
    if (!fromStart && SharedWork)
    {
        // the driver's current variables, including those set by the game mid-song
        for (int i = 0; i < 16; i++)
            Bgm.SetVariable((u8)i, (s16)RamRead32(SharedWork + 0x20 + player * 36 + i * 2));
    }
    if (s.Paused) Bgm.Pause(true);

    HostP = player;
    s.HostMode = true;
    MutedP |= 1 << player;
    // notes keyed on before this were tagged while the player was not muted
    NDS.SPU.RetagHostNotes();
    UpdateMuteMask();

    Log(LogLevel::Info, "RealtimeBGM: host renderer plays player %d (%s) from tick %u, mute mask %04X\n", player,
        s.Info && !s.Info->Name.empty() ? s.Info->Name.c_str() : "?", tick, CurMuteMask);
    return true;
}

void SndCmdTracker::LeaveHostMode()
{
    Bgm.Stop();
    if (HostP >= 0)
    {
        P[HostP].HostMode = false;
        Log(LogLevel::Info, "RealtimeBGM: host renderer released player %d\n", HostP);
    }
    HostP = -1;
    UpdateMuteMask();
}

void SndCmdTracker::PickHost()
{
    u32 status = SharedWork ? RamRead32(SharedWork + 4) : 0xFFFF;
    int best = -1;
    for (int p = 0; p < 16; p++)
    {
        const PlayerState& s = P[p];
        if (!s.Active || s.Prepared || NoHost[p] || !(status & (1 << p)) || !Eligible(s)) continue;
        if (best < 0 || StartOrder[p] > StartOrder[best]) best = p;
    }
    if (best >= 0)
        EnterHostMode(best, false);
}

void SndCmdTracker::SetFastForward(bool on)
{
    if (on == FF) return;
    FF = on;
    // when fast-forward ends the host stays the audible source until the next START/STOP on
    // that player, since the driver copy is now ahead in the song (design 6.2)
    if (FF && Settings.Enabled && HostP < 0)
        PickHost();
}

u32 SndCmdTracker::TickCounter(int player) const
{
    if (!SharedWork) return 0;
    // SNDSharedWork: 0x20 bytes of header, then per player { s16 variable[16]; u32 tickCounter; }
    return RamRead32(SharedWork + 0x20 + player * 36 + 32);
}

void SndCmdTracker::UpdateMuteMask()
{
    u16 mask = 0;
    if (HostP >= 0 && Settings.Enabled)
    {
        // a player counts while the driver reports it playing (sound effects finish without a STOP),
        // and right after its start, before the driver sets its status bit: a sound effect's first
        // notes must not be pre-muted on channels it may allocate
        u32 status = SharedWork ? RamRead32(SharedWork + 4) : 0xFFFF;
        u16 others = 0;
        for (int q = 0; q < 16; q++)
            if (q != HostP && P[q].Active && ((status & (1 << q)) || FrameCount - StartFrame[q] <= 2))
                others |= P[q].ChanMask;
        u16 exclusive = P[HostP].ChanMask & ~others;

        if (ChanOwnerValid)
        {
            // idle channels only the host may allocate (a note starting there before the next
            // snapshot would otherwise leak through); sounding notes are muted by their tag
            for (int ch = 0; ch < 16; ch++)
                if (ChanOwner[ch] < 0 && (exclusive & (1 << ch)))
                    mask |= 1 << ch;
        }
        else
            mask = exclusive;
    }

    CurMuteMask = mask;
}

bool SndCmdTracker::ParseDriverInfo()
{
    // SNDDriverInfo { SNDWork work; u32 chCtrl[16]; SNDWork* workAddress; u32 lockedChannels; u32 padding[6]; }
    std::vector<u8> buf;
    if (!CopyRAM(DriverInfoAddr, WORK_SIZES[0] + 72, buf)) return false;

    for (u32 ch = 0; ch < 16; ch++)
        if (buf[ch * EXCH_SIZE] != ch) return false;

    u32 work = 0, workSize = 0;
    for (u32 size : WORK_SIZES)
    {
        u32 wa = Get32(&buf[size + 64]);
        u32 locked = Get32(&buf[size + 68]);
        if ((wa & 3) || locked > 0xFFFF || (wa >> 24) < 0x02 || (wa >> 24) > 0x03) continue;
        // the live ARM7 copy must look like an SNDWork too
        bool match = true;
        for (u32 ch = 0; ch < 16 && match; ch++)
            match = NDS.ARM7Read8(wa + ch * EXCH_SIZE) == ch;
        if (match) { work = wa; workSize = size; break; }
    }
    if (!work) return false;
    LiveWork = work;

    u32 trackBase = work + WORK_TRACK_OFS;
    for (u32 ch = 0; ch < 16; ch++)
    {
        const u8* c = &buf[ch * EXCH_SIZE];
        s8 owner = -1;
        u32 trk = Get32(c + EXCH_CALLBACK_DATA_OFS);
        if ((c[3] & 1) && trk >= trackBase && trk < trackBase + 32 * TRACK_SIZE && !((trk - trackBase) % TRACK_SIZE))
        {
            u8 t = (u8)((trk - trackBase) / TRACK_SIZE);
            for (int p = 0; p < 16 && owner < 0; p++)
            {
                const u8* pl = &buf[WORK_PLAYER_OFS + p * PLAYER_SIZE];
                if (!(pl[0] & 1)) continue;
                for (int k = 0; k < 16; k++)
                    if (pl[PLAYER_TRACKS_OFS + k] == t) { owner = (s8)p; break; }
            }
        }
        ChanOwner[ch] = owner;
    }

    if (DriverInfoLogged != 1)
    {
        DriverInfoLogged = 1;
        Log(LogLevel::Info, "RealtimeBGM: driver info at %08X parsed (SNDWork at %08X, %u bytes): using exact channel ownership\n",
            DriverInfoAddr, work, workSize);
    }
    return true;
}

int SndCmdTracker::ChannelKeyOnOwner(int ch)
{
    if (!MutedP || !Settings.Enabled || !ChanOwnerValid || !LiveWork) return -1;

    u32 trackBase = LiveWork + WORK_TRACK_OFS;
    u32 trk = NDS.ARM7Read32(LiveWork + ch * EXCH_SIZE + EXCH_CALLBACK_DATA_OFS);
    if (trk < trackBase || trk >= trackBase + 32 * TRACK_SIZE || (trk - trackBase) % TRACK_SIZE) return -1;
    u8 t = (u8)((trk - trackBase) / TRACK_SIZE);

    for (int p = 0; p < 16; p++)
    {
        if (!(MutedP & (1 << p))) continue;
        u32 pl = LiveWork + WORK_PLAYER_OFS + p * PLAYER_SIZE;
        if (!(NDS.ARM7Read8(pl) & 1)) continue;
        for (int k = 0; k < 16; k++)
            if (NDS.ARM7Read8(pl + PLAYER_TRACKS_OFS + k) == t) return p;
    }
    return -1;
}

void SndCmdTracker::ApplyOutputSettings()
{
    if (Settings.Interpolation != AppliedInterp)
    {
        AppliedInterp = Settings.Interpolation;
        Bgm.SetInterpolation(AppliedInterp);
    }
    if (Settings.OutputSkew != AppliedSkew)
    {
        AppliedSkew = Settings.OutputSkew;
        Bgm.SetOutputSkew(AppliedSkew);
    }
}

void SndCmdTracker::OnFrame()
{
    FrameCount++;
    ApplyOutputSettings();
    if (!Settings.Enabled)
    {
        if (HostP >= 0) LeaveHostMode();
        if (MutedP)
        {
            MutedP = 0;
            NDS.SPU.ClearHostTags();
        }
    }
    if (HostP >= 0 && !Bgm.Playing() && !P[HostP].Paused)
    {
        // the host copy reached the end of a non-looping sequence
        Log(LogLevel::Info, "RealtimeBGM: host copy of player %d finished\n", HostP);
        NoHost[HostP] = true;
        LeaveHostMode();
    }

    if (DriverInfoPending)
    {
        DriverInfoPending = false;
        bool wasValid = ChanOwnerValid;
        ChanOwnerValid = ParseDriverInfo();
        if (!ChanOwnerValid) ChanOwner.fill(-1);
        // notes can only be tagged with a valid snapshot (savestate load: tags were dropped)
        if (ChanOwnerValid && !wasValid && MutedP) NDS.SPU.RetagHostNotes();
        if (!ChanOwnerValid && DriverInfoLogged != 0)
        {
            DriverInfoLogged = 0;
            Log(LogLevel::Info, "RealtimeBGM: driver info at %08X not recognised: using ALLOCATABLE_CHANNEL masks\n", DriverInfoAddr);
        }
    }

    if (FF && Settings.Enabled && HostP < 0)
        PickHost();

    // A muted player the host does not follow stays silent only while fast-forward may adopt it again.
    // Otherwise the hardware becomes its source again. Stopped players stay muted for their release tails.
    for (int p = 0; p < 16; p++)
    {
        if (p == HostP || !(MutedP & (1 << p)) || !P[p].Active || (FF && !NoHost[p])) continue;
        MutedP &= ~(1 << p);
        Log(LogLevel::Debug, "RealtimeBGM: player %d unmuted, hardware plays it again\n", p);
    }

    UpdateMuteMask();
}

void SndCmdTracker::DoSavestate(melonDS::Savestate* file)
{
    if (!file->Saving)
    {
        // Upstream savestates have no RealtimeBGM section, and a missing section is a load error,
        // so look for it first (same walk as Savestate::FindSection).
        const u8* b = (const u8*)file->Buffer();
        bool found = false;
        for (u32 ofs = 0x10; ofs + 8 <= file->BufferLength();)
        {
            if (!memcmp(b + ofs, "NELO", 4)) { found = true; break; }
            u32 len = Get32(b + ofs + 4);
            if (len == 0) break;
            ofs += len;
        }
        if (!found)
        {
            Bgm.Kill();
            u32 sw = SharedWork, dia = DriverInfoAddr, dir = DriverInfoReq;
            Reset();
            // same game: the driver's work areas do not move
            SharedWork = sw; DriverInfoAddr = dia; DriverInfoReq = dir;
            return;
        }
    }

    file->Section("NELO");
    file->Var32(&SharedWork);
    file->Var32(&DriverInfoAddr);
    file->Var32(&DriverInfoReq);
    file->Var8(&MasterVol);
    for (int p = 0; p < 16; p++)
    {
        PlayerState& s = P[p];
        file->Bool32(&s.Active);
        file->Bool32(&s.Prepared);
        file->Bool32(&s.Paused);
        file->Var32(&s.MML);
        file->Var32(&s.MMLLen);
        file->Var32(&s.Bank);
        file->Var32(&s.CRC);
        file->Var16(&s.ChanMask);
        file->Var16((u16*)&s.ExtFader);
        file->Var16(&s.TempoRatio);
        file->Var8((u8*)&s.Cls);
        file->Bool32(&s.HostMode);
        file->Var16(&TrackMute[p]);
        file->Var32(&StartOrder[p]);
    }
    file->Var32(&StartCounter);
    s32 host = HostP;
    file->Var32((u32*)&host);

    if (file->Saving) return;

    Bgm.Kill();
    HostP = -1;
    MutedP = 0;
    NoHost = {};
    ChanOwner.fill(-1);
    ChanOwnerValid = false;
    DriverInfoPending = false;
    for (PlayerState& s : P)
    {
        s.Info = s.MMLLen ? Idx.Lookup(s.CRC, s.MMLLen) : nullptr;
        s.HostMode = false;
    }
    if (host >= 0 && host < 16 && FF && Settings.Enabled)
        EnterHostMode(host, false);
    UpdateMuteMask();
}

}
