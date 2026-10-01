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
    RealtimeBGM: host-side SSEQ renderer, wall-clock paced. See BgmRenderer.h.
*/

#include <mutex>
#include <string.h>
#include "BgmRenderer.h"
#include "SSEQPlayer/Player.h"

namespace melonDS::Sound
{
namespace SP = SSEQPlayer;

namespace
{
// Sequence, bank and wave archives of the loaded song. The player, its tracks and channels
// keep raw pointers into this, so it stays at one heap address until the next Load().
struct Song
{
    SP::SSEQ Seq;
    SP::SBNK Bank;
    SP::SWAR WaveArc[4];
};
}

// Ticks before the seek target that are simulated with notes and envelopes; earlier ticks
// only run the sequence (like SND_SkipSeq), so notes held across the target are still sounding.
constexpr u32 SeekFullSimTicks = 768;

struct BgmRenderer::Impl
{
    // Guards Ply, Playing and OutputRate, which Render() uses from the audio thread.
    // Song and the parameter copies below are only touched by the emu thread.
    mutable std::mutex Lock;
    std::unique_ptr<SP::Player> Ply;
    bool Playing = false;
    double OutputRate = 48000;

    std::unique_ptr<Song> Loaded;

    // Driver-side parameters as last set by the game. The driver resets them when a
    // sequence starts (InitPlayer/InitTrack), so Load() resets them; Start() applies them.
    s16 ExtFader;
    u16 TempoRatio;
    s16 TrackFader[16];
    s16 TrackPitch[16];
    s8 TrackPan[16];
    u16 TrackMute;
    s16 Variables[16];
    u16 VariablesSet;
    u8 MasterVolume = 127; // hardware setting, survives Load()

    void ResetParams()
    {
        ExtFader = 0;
        TempoRatio = 256;
        memset(TrackFader, 0, sizeof(TrackFader));
        memset(TrackPitch, 0, sizeof(TrackPitch));
        memset(TrackPan, 0, sizeof(TrackPan));
        TrackMute = 0;
        VariablesSet = 0;
    }

    std::unique_ptr<SP::Player> NewPlayer(double rate) const
    {
        auto ply = std::make_unique<SP::Player>();
        ply->interpolation = SP::INTERPOLATION_4POINTLEGRANGE;
        ply->SetSampleRate(rate);
        ply->outputVol = MasterVolume == 127 ? 128 : MasterVolume;
        return ply;
    }

    void ApplyParams(SP::Player& ply) const
    {
        ply.extFader = ExtFader;
        ply.tempoRate = TempoRatio;
        memcpy(ply.trackExtFader, TrackFader, sizeof(TrackFader));
        memcpy(ply.trackExtPitch, TrackPitch, sizeof(TrackPitch));
        memcpy(ply.trackExtPan, TrackPan, sizeof(TrackPan));
        ply.trackMute = TrackMute;
        for (int i = 0; i < 16; i++)
            if (VariablesSet & (1 << i))
                ply.variables[i] = Variables[i];
    }
};

BgmRenderer::BgmRenderer() : P(std::make_unique<Impl>())
{
    P->ResetParams();
    P->Ply = P->NewPlayer(P->OutputRate);
}

BgmRenderer::~BgmRenderer() = default;

bool BgmRenderer::Load(const u8* mml, u32 mmlLen,
                       const u8* sbnk, u32 sbnkLen,
                       const u8* const swar[4], const u32 swarLen[4])
{
    {
        std::lock_guard<std::mutex> lock(P->Lock);
        P->Playing = false;
    }

    // Parse outside the lock so Render() keeps running (silently) meanwhile.
    std::unique_ptr<Song> song;
    if (mml && mmlLen && sbnk && sbnkLen)
    {
        song = std::make_unique<Song>();
        try
        {
            // The MML is used as-is; the SSEQ header is not needed. Pad with END so a track
            // running off the end (or a clamped bad jump target, see Track.cpp DataAt) stops.
            song->Seq.data.assign(mml, mml + mmlLen);
            song->Seq.data.insert(song->Seq.data.end(), 16, 0xFF);
            song->Seq.bank = &song->Bank;

            std::vector<u8> buf(sbnk, sbnk + sbnkLen);
            SP::PseudoFile file;
            file.data = &buf;
            song->Bank.Read(file);

            for (int i = 0; i < 4; i++)
            {
                if (!swar[i] || !swarLen[i])
                    continue;
                buf.assign(swar[i], swar[i] + swarLen[i]);
                file.pos = 0;
                song->WaveArc[i].Read(file);
                song->Bank.waveArc[i] = &song->WaveArc[i];
            }
        }
        catch (const std::exception&)
        {
            song.reset();
        }
    }

    std::unique_ptr<Song> oldSong = std::move(P->Loaded);
    P->Loaded = std::move(song);
    P->ResetParams();

    auto ply = P->NewPlayer(P->OutputRate);
    {
        std::lock_guard<std::mutex> lock(P->Lock);
        ply->SetSampleRate(P->OutputRate);
        std::swap(P->Ply, ply);
    }
    // old player and song are freed here, outside the lock
    return P->Loaded != nullptr;
}

void BgmRenderer::Start(u32 atTick)
{
    if (!P->Loaded)
        return;

    double rate;
    {
        std::lock_guard<std::mutex> lock(P->Lock);
        rate = P->OutputRate;
    }

    // Build and seek a fresh player outside the lock, then swap it in.
    auto ply = P->NewPlayer(rate);
    ply->Setup(&P->Loaded->Seq);
    P->ApplyParams(*ply);

    if (atTick > 0)
    {
        ply->skipNotes = true;
        while (ply->tickCounter + SeekFullSimTicks < atTick && !ply->seqEnded)
            ply->RunTick();
        ply->skipNotes = false;

        // Same per-period work as during playback, minus mixing. Bounded in case of tempo 0.
        double samplesPerClock = SP::SecondsPerClockCycle * rate;
        u32 maxPeriods = (atTick - ply->tickCounter + 1) * 240;
        for (u32 n = 0; n < maxPeriods && ply->tickCounter < atTick && !ply->seqEnded; n++)
        {
            ply->Timer(atTick);
            for (auto& chn : ply->channels)
                chn.SkipSamples(samplesPerClock);
        }
    }

    {
        std::lock_guard<std::mutex> lock(P->Lock);
        ply->SetSampleRate(P->OutputRate);
        std::swap(P->Ply, ply);
        P->Playing = true;
    }
}

void BgmRenderer::Stop()
{
    std::lock_guard<std::mutex> lock(P->Lock);
    P->Playing = false;
    for (auto& chn : P->Ply->channels)
        chn.Kill();
}

void BgmRenderer::Pause(bool paused)
{
    std::lock_guard<std::mutex> lock(P->Lock);
    P->Ply->SetPaused(paused);
}

bool BgmRenderer::Active() const
{
    std::lock_guard<std::mutex> lock(P->Lock);
    return P->Playing && !P->Ply->Finished();
}

// extFader is in the driver's centibel (0.1 dB) volume units and is added straight onto the
// channel attenuation, see Channel::UpdateVol and NitroSDK:
//   snd_seq.c:779-780       UpdateTrackChannel(): user_decay = DecibelSquare(track volume)
//                           + DecibelSquare(track volume2) + DecibelSquare(player volume);
//                           user_decay2 = track->extFader + player->extFader (each clamped >= -32768)
//   snd_exchannel.c:153-204 SND_ExChannelMain(): decay = DecibelSquare(velocity) + envelope
//                           + user_decay + user_decay2 (+ volume LFO); SND_CalcChannelVolume(decay)
//   snd_util.c:303-331      SND_CalcChannelVolume(): clamp decay to [-723, 0], VolumeTable[decay + 723]
//                           gives the 7-bit SOUNDxCNT volume, shift /2 /4 /16 below -60/-120/-240
// i.e. channel volume = VolumeTable(clamp(vel + env + vol + vol2 + mainvol + trackFader + extFader)).
void BgmRenderer::SetExtFader(s16 driverDecibel)
{
    P->ExtFader = driverDecibel;
    std::lock_guard<std::mutex> lock(P->Lock);
    P->Ply->extFader = driverDecibel;
    P->Ply->FlagTracks(0xFFFF, SP::TUF_VOL);
}

void BgmRenderer::SetTempoRatio(u16 ratio256)
{
    P->TempoRatio = ratio256;
    std::lock_guard<std::mutex> lock(P->Lock);
    P->Ply->tempoRate = ratio256;
}

void BgmRenderer::SetTrackFader(u16 trackMask, s16 driverDecibel)
{
    for (int i = 0; i < 16; i++)
        if (trackMask & (1 << i))
            P->TrackFader[i] = driverDecibel;
    std::lock_guard<std::mutex> lock(P->Lock);
    memcpy(P->Ply->trackExtFader, P->TrackFader, sizeof(P->TrackFader));
    P->Ply->FlagTracks(trackMask, SP::TUF_VOL);
}

void BgmRenderer::SetTrackPitch(u16 trackMask, s16 pitch)
{
    for (int i = 0; i < 16; i++)
        if (trackMask & (1 << i))
            P->TrackPitch[i] = pitch;
    std::lock_guard<std::mutex> lock(P->Lock);
    memcpy(P->Ply->trackExtPitch, P->TrackPitch, sizeof(P->TrackPitch));
    P->Ply->FlagTracks(trackMask, SP::TUF_TIMER);
}

void BgmRenderer::SetTrackPan(u16 trackMask, s8 pan)
{
    for (int i = 0; i < 16; i++)
        if (trackMask & (1 << i))
            P->TrackPan[i] = pan;
    std::lock_guard<std::mutex> lock(P->Lock);
    memcpy(P->Ply->trackExtPan, P->TrackPan, sizeof(P->TrackPan));
    P->Ply->FlagTracks(trackMask, SP::TUF_PAN);
}

// Like SND_SEQ_MUTE_NO_STOP (what NNS_SndPlayerSetTrackMute(TRUE) sends): no new notes, held notes continue.
void BgmRenderer::MuteTracks(u16 trackMask, bool mute)
{
    if (mute)
        P->TrackMute |= trackMask;
    else
        P->TrackMute &= ~trackMask;
    std::lock_guard<std::mutex> lock(P->Lock);
    P->Ply->trackMute = P->TrackMute;
}

void BgmRenderer::SetVariable(u8 index, s16 value)
{
    if (index >= 16)
        return;
    P->Variables[index] = value;
    P->VariablesSet |= 1 << index;
    std::lock_guard<std::mutex> lock(P->Lock);
    P->Ply->variables[index] = value;
}

void BgmRenderer::SetMasterVolume(u8 vol127)
{
    P->MasterVolume = vol127 & 0x7F;
    std::lock_guard<std::mutex> lock(P->Lock);
    // same as SPU::Write SOUNDCNT: 127 means unity
    P->Ply->outputVol = P->MasterVolume == 127 ? 128 : P->MasterVolume;
}

void BgmRenderer::SetOutputRate(double hz)
{
    if (hz <= 0)
        return;
    std::lock_guard<std::mutex> lock(P->Lock);
    P->OutputRate = hz;
    P->Ply->SetSampleRate(hz);
}

void BgmRenderer::Render(s16* stereo, int frames)
{
    if (frames <= 0)
        return;
    std::lock_guard<std::mutex> lock(P->Lock);
    if (!P->Playing || P->Ply->Finished())
    {
        memset(stereo, 0, frames * 2 * sizeof(s16));
        return;
    }
    P->Ply->GenerateSamples(stereo, frames);
}

}
