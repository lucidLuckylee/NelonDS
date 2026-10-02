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
    RealtimeBGM: sniffs NitroSDK sound commands on the ARM9->ARM7 PXI FIFO (tag 7),
    tracks per-driver-player state, decides which SPU channels carry BGM and
    drives the host BgmRenderer so music plays at 1x while fast-forwarding.
*/
#ifndef SNDCMDTRACKER_H
#define SNDCMDTRACKER_H

#include <array>
#include <vector>
#include "../types.h"
#include "SdatIndex.h"
#include "BgmRenderer.h"

namespace melonDS { class NDS; class Savestate; }

namespace melonDS::Sound
{

struct RealtimeBgmSettings
{
    bool Enabled = true;
    bool JinglesAt1x = false;   // fanfares are short and game-timed: keep them on the emulated clock
    bool AmbientAt1x = false;   // environmental loops stay on emulation clock
    int Interpolation = 0;      // melonDS AudioInterpolation value, see BgmRenderer::SetInterpolation
    double OutputSkew = 1.0;    // see BgmRenderer::SetOutputSkew
};

class SndCmdTracker
{
public:
    explicit SndCmdTracker(melonDS::NDS& nds);
    ~SndCmdTracker();

    void Reset();                               // on NDS reset
    void DoSavestate(melonDS::Savestate* file);

    // Called by NDS::SetNDSCart with the full ROM image (may be nullptr on eject).
    void OnCartChanged(const u8* rom, u32 romLen);

    // Called from NDS::ARM9IOWrite32 for every word written to IPCFIFOSEND (0x04000188).
    void OnPxiWord(u32 word);

    // Called once per emulated frame (end of NDS::RunFrame): consumes the READ_DRIVER_INFO
    // snapshot if one was requested, refreshes channel ownership and the mute mask.
    void OnFrame();

    // Frontend: fast-forward state and settings.
    void SetFastForward(bool on);
    RealtimeBgmSettings Settings;

    // SPU: channels whose output must be dropped from the hardware mix right now.
    u16 MuteMask() const { return CurMuteMask; }
    // SPU, at key-on of channel ch: true if the driver's live work area says the note belongs to the
    // host player. Decided per note so nothing leaks before the next per-frame snapshot.
    bool ChannelKeyOnMuted(int ch);
    int HostPlayer() const { return HostP; }   // driver player the renderer follows, -1 if none

    BgmRenderer& Renderer() { return Bgm; }

private:
    struct PlayerState
    {
        bool Active = false;      // sequence loaded on this driver player
        bool Prepared = false;    // PREPARE_SEQ seen, START_PREPARED_SEQ not yet
        bool Paused = false;
        u32 MML = 0, MMLLen = 0, Bank = 0;
        u32 CRC = 0;
        u16 ChanMask = 0;         // last ALLOCATABLE_CHANNEL
        s16 ExtFader = 0;
        u32 FaderFrame = 0;       // FrameCount at the last ExtFader change
        u16 TempoRatio = 256;
        SeqClass Cls = SeqClass::Unknown;
        const SeqInfo* Info = nullptr;   // nullptr if not found in index
        bool HostMode = false;    // host renderer is the audible source for this player
    };

    void HandleCommand(u32 id, u32 a0, u32 a1, u32 a2, u32 a3);
    void OnStart(int player, u32 mml, u32 offset, u32 bank, bool prepareOnly);
    void OnStop(int player);
    bool EnterHostMode(int player, bool fromStart);  // fromStart: play from tick 0, else resync to the driver
    void LeaveHostMode();
    void PickHost();              // fast-forwarding without a host: adopt the most recent eligible player
    bool Eligible(const PlayerState& s) const;
    void UpdateMuteMask();
    void ApplyOutputSettings();   // passes changed Settings.Interpolation/OutputSkew to the renderer
    bool ParseDriverInfo();       // returns true if a valid snapshot was parsed
    u32 TickCounter(int player) const;  // from SNDSharedWork, 0 if unknown
    u32 RamRead32(u32 addr) const;
    bool CopyRAM(u32 addr, u32 len, std::vector<u8>& out) const;
    bool CopySwar(u32 addr, std::vector<u8>& out) const;

    melonDS::NDS& NDS;
    SdatIndex Idx;
    BgmRenderer Bgm;
    std::array<PlayerState, 16> P;
    bool FF = false;
    int HostP = -1;               // driver player the renderer follows, -1 if none
    u16 CurMuteMask = 0;
    u32 SharedWork = 0;           // ARM9 address of SNDSharedWork
    u32 DriverInfoAddr = 0;       // ARM9 address of the last READ_DRIVER_INFO buffer
    bool DriverInfoPending = false;
    std::array<s8, 16> ChanOwner {};   // per SPU channel: driver player, -1 unknown (from driver info)
    bool ChanOwnerValid = false;
    u32 LiveWork = 0;             // ARM7 address of the driver's SNDWork, validated by ParseDriverInfo
    u32 DriverInfoReq = 0;        // buffer of the newest READ_DRIVER_INFO (DriverInfoAddr is the previous, completed one)
    int DriverInfoLogged = -1;    // last logged parse result
    std::array<u16, 16> TrackMute {};   // per player: tracks muted by MUTE_TRACK
    std::array<u32, 16> StartOrder {};  // per player: StartCounter value at its last start
    std::array<bool, 16> NoHost {};     // per player: host mode failed or finished for this start
    u32 StartCounter = 0;
    u32 FrameCount = 0;           // emulated frames, for the 1x pace of fader changes
    u8 MasterVol = 127;
    int AppliedInterp = -1;       // last values passed by ApplyOutputSettings
    double AppliedSkew = 0;
};

}
#endif
