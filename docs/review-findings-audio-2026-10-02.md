# Cloud review findings: thorough audio review (2026-10-02, second review)

Source: claude --cloud with docs/review-prompt-audio-thorough.md on branch nelonds (after the handoff fixes and the blob cache).

## Ranked findings
1. Blob cache keyed by MML CRC alone and preferred over a valid live bank (SndCmdTracker EnterHostMode). Retail games start one SSEQ with different banks (Platinum: SEQ_BICYCLE/SEQ_PL_BICYCLE share SEQ_BICYCLE.sseq with BANK_BASIC vs BANK_BGM_FIELD; SEQ_AIF_FIELD.sseq with three banks; SEQ_SILENCE_FIELD.sseq with two). Fix: at OnStart CRC the SBNK (bank is valid then) and record it in PlayerState; key the cache on (MML CRC, bank CRC); prefer the live RAM copy, fall back to the cache only when the live bank fails validation.
2. Time-stretch ratio from configured speed, not achieved (Qt: curFPS/targetFPS; Android: configured multiplier unless unlimited); ReadOutputStretched pulls ceil(len*ratio) regardless of ring fill → underruns on devices that cannot reach the configured speed; Qt also gates Render() on num_in >= 1 so BGM freezes. Fix: derive ratio from ring fill rate (produced frames over last N callbacks / output frames) or clamp to available; Qt renders BGM independently of num_in like Android.
3. MUTE_TRACK modes collapsed to "mute new notes"; SDK SND_SetTrackMute sends SND_SEQ_MUTE_STOP (release rate 127 and free). Fix: pass the mode through to Player::SetTrackMute(track, mode) (modes 2 and 3 exist in the vendored player); store mode per track for adoption replay.
4. Null dereference in vendored note-on for instrument records 4 (DIRECTPCM) and 5 (NULL): Track.cpp noteDef stays nullptr then dereferenced; reachable from a half-written SBNK. Fix: return -1 for records >= 4.
5. Stale stretcher state at every fast-forward start: passthrough path never resets the stretcher. Fix: Stretcher.Reset() on the passthrough-to-stretch transition.
6. Release tails of the previous song leak at FF pitch when START reuses the driver player (OnStart clears MutedP bit; SDK FinishPlayer releases old notes with instrument release rates). Fix: before clearing, set the tag of channels tagged with this player to a sentinel (-2) that SPU::Mix always mutes; key-on overwrites it.
7. Hard STOP fades the host over fixed 250 ms instead of releasing notes (Player::Stop(false) would match the SDK). Low.
8. Cache survives cart change. Fix: clear BlobCache in OnCartChanged.
9. SKIP_SEQ (5) and PLAYER_GLOBAL_VAR (11) not forwarded. Fix: SKIP_SEQ on host → re-seek Bgm.Start(TickCounter + ticks); global variables as indices 16-31.
10. Fidelity nits (low): tied notes restart the LFO (Track.cpp vs snd_seq.c tie keeps LFO); note_finish_wait missing for zero-length notes; ALLOCATABLE_CHANNEL per player vs per track; 768-tick seek window runs before mutes/track params are applied; no capture units on host; Release() cuts an older outgoing voice when two switches land within the fade.
11. Savestate NELO section has no version and differs between desktop (VarBool) and Android (Bool32). Fix: leading version u32, Reset() on mismatch; use the same width in both trees.
12. Upstream-readiness: TimeStretch.cpp uses M_PI without _USE_MATH_DEFINES (MSVC); SDAT index built even when the feature is disabled (gate or lazy); Audio.FastForwardStretch defaults true independently of Audio.RealtimeBGM (tie to the feature); comments cite NitroSDK file/line numbers and a disassembly provenance note in Channel.cpp:369 (upstream may object); Settings written from the UI thread without sync (torn double possible on 32-bit).

## Verified, no change needed
Struct offsets and both SNDWork sizes; key-on ordering (callback_data before SOUNDxCNT; stolen/locked/waveout/finished channels nulled; tags overwritten at key-on); state machine walk found no silent-with-no-host or doubled path beyond finding 6; NNS ForceStopSeq sends -723 before STOP; READ_DRIVER_INFO double-buffering; loudness/pan/envelope/LFO/sweep/timer/volume tables match the SDK; pause matches SND_PauseSeq; disabled is a no-op; real-time safety (Load/seek on emu thread, render under one callback's mutex, no audio-thread allocations after the first callback).

## Device tests suggested
1. Shared sequence, two banks (Platinum bicycle via SEQ_BICYCLE vs SEQ_PL_BICYCLE) during FF in either order.
2. Unreachable FF speed (8x in a heavy scene) with stretch on: choppy effects, frozen/jumping music on desktop.
3. Layer mutes (MUTE_TRACK) during FF; STOP+START without fade during FF for sped-up tails.
