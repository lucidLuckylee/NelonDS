# Cloud review 3 (2026-10-02): parked voices, frameskip, Android merge

Review of branch `nelonds` after the parked-voice rewrite and frameskip. Verbatim findings; status column added during triage.

## Parked voices

1. Stale fader glide is parked with the voice and replays on resume (medium). BgmRenderer.cpp:427 moves the current voice into the parked slot with its Steps queue intact. The tracker replays fades at 1x pace, so at 8x a 30-frame fade-out arrives in 60 ms of wall clock but the host still has about 0.45 s of glide queued when PAUSE_SEQ parks the voice. Trigger: any fade followed by a pause of the same player, then a resume without the game re-sending the fader. SndCmdTracker.cpp:620 only resets the fader when it changed while parked. Heard: field BGM comes back at its pre-fade level and dips to silence over up to a second, then fights the game's fade-in. Fix, after the kill loop at BgmRenderer.cpp:437: `slot.V.ResetFader(slot.Params.ExtFader); slot.V.ApplyFader();`
2. Local sequence variables set while parked are lost (low). SndCmdTracker.cpp:368 drops PLAYER_LOCAL_VAR for non-host players, and UnparkHost (628-632) only re-reads the globals. Fix: copy the 16-entry loop from EnterHostMode (557-562) into UnparkHost.
3. SKIP_SEQ on a parked player is ignored (low). SndCmdTracker.cpp:302 only handles the host. Fix: on SKIP_SEQ for a parked player, drop the parked voice and clear Parked so the resume re-adopts from the driver tick.
4. Known limitation, no change asked: savestate load and Android rewind drop parked voices (SndCmdTracker.cpp:937); a paused field theme is re-adopted from the driver's position.

Walked and found correct: pause of the host parks and keeps it muted; a second PAUSE, STOP, INVALIDATE_SEQ/BANK, START, PREPARE on a parked player drop the slot and clear the flag; PLAYER_PARAM, TRACK_PARAM, MUTE_TRACK, ALLOCATABLE_CHANNEL, MASTER_VOLUME while parked are stored and reapplied; resume while another player is hosted waits silently and unparks when the host leaves; fast-forward off keeps a paused parked player muted and unparks on resume; a resumed-but-waiting player with fast-forward off is handed back to hardware next frame; a failed Park degrades to re-adoption from RAM; Reset, cart change and disable drop every slot. No doubling path found.

## Frameskip

No correctness bug found. Verified: only renderer calls are gated (GPU2D latches, affine increments, DMA, display FIFO, GPU3D.VBlank run on skipped frames); VRAM/OAM/palette dirty bits accumulate across skips; Start3DFrame clears RenderFrameIdentical after a skip; threaded 3D posts only when a render is pending and savestates set Rendering3D before PostSavestate; capture frames and the one after are always rendered; Android forces a render for screenshots, Qt savestate save forces the next frame; the rule has no hysteresis but cannot limit-cycle.

## Android merge

Cores identical except the OpenGL ES port, CMake ENet handling, and three residues:
- melonDS-android-lib/src/SPU.cpp:245 resets Mute = false where desktop (SPU.cpp:251) has Mute = true, and the lib lacks the destructor free at desktop 230-235 (one SPU output buffer leaked per session). Likely leftovers.
- melonDS-android-lib/src/ARM_InstrInfo.cpp:389 keeps the Android maintainer's JIT change (r15 always a source register). Pre-existing, probably intended.
- melonDS-android-lib/src/frontend/qt_sdl/EmuThread.cpp:443 still has the old round-to-nearest rule. Not built on Android.

JNI and MelonInstance hooks match desktop semantics.

## Status (triage 2026-10-02)

- Finding 1: fixed, desktop 32ea398d / android-lib b5834592.
- Finding 2: fixed, same commits.
- Finding 3: fixed, same commits.
- Finding 4: kept, known limitation.
- SPU.cpp residue: fixed, android-lib b5834592 (copied from desktop).
- EmuThread.cpp residue: synced, android-lib b5834592.
- ARM_InstrInfo.cpp: kept, intended Android JIT change.
