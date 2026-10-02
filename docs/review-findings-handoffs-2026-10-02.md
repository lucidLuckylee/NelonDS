# Cloud review findings: handoff bugs (2026-10-02)

Source: claude --cloud review of branch nelonds vs main with docs/review-prompt-handoffs.md.

## Ranked bugs

1. A player that stops being the host keeps sounding on hardware at the fast-forward position (SndCmdTracker.cpp LeaveHostMode clears HostP and the mask at once; SPU.cpp tag only mutes while it equals the current HostP). (a) Host steal: host follows B, START of eligible D → B's sounding notes (tag B) become audible at FF speed, far ahead. (b) STOP/INVALIDATE of host X: SDK keeps each note's release tail (FreeTrackChannelAll nulls callback_data), HostP=-1, tails play sped-up under the host's 1x tail. Fix: 16-bit MutedPlayers set in the tracker; set in EnterHostMode; clear in OnStart(p) and when the feature is disabled (also clear channel tags); SPU mutes when tag in MutedPlayers; ChannelKeyOnMuted walks every muted player's track list; UpdateMuteMask uses ChanOwner[ch] in MutedPlayers.
2. Notes already sounding are not re-tagged when the host is adopted or switched (EnterHostMode never touches SPUChannel::HostMutedPlayer; snapshot is 1-2 frames old). Fix: after HostP = player, re-tag the 16 SPU channels with Cnt bit 31 set from the live SNDWork; drop the ChanOwner[ch] == HostP term from the mask, keep the idle-exclusive pre-mute.
3. The host is only picked at frame end (PickHost in OnFrame), so a STOP mid-frame leaves every active BGM player unmuted for up to a frame. Fix: at the end of OnPxiWord, if FF && Enabled && HostP < 0, PickHost().
4. Per-channel tags survive a savestate load. Fix: HostMutedPlayer = -1 for every channel on load in SPUChannel::DoSavestate, then re-tag per bug 2.
5. EnterHostMode releases the old host before it knows the new one loads (LeaveHostMode before the Bgm.Load check). Fix: evaluate ok first, return (setting NoHost) before LeaveHostMode.
6. Fallback mask with overlapping masks: "others" never shrinks because SE players never STOP. Fix: AND others with the playerStatus word (SharedWork+4) so finished players do not count; optionally mute shared channels.
7. Pause/resume applied to the outgoing voice (BgmRenderer Pause). Fix: only pause the current voice.

## Notes (not bugs)
- Re-adoption uses the hardware tick, which is (speed-1) x time-in-host ahead: inherent to the single-copy design.
- Fades queue at 1x pace; the outgoing voice finishes them under the next song (intended).
- Qt freezes the host renderer when the SPU ring is empty, Android keeps rendering: pick one policy.
- Key-on check verified against the SDK: callback_data is written at least one driver period before SOUNDxCNT; freed/locked/waveout channels have callback_data nulled; struct offsets match.
- Muted channels are also removed from capture inputs (game-visible, unused by Gen 4 Pokemon).
