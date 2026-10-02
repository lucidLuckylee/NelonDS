# Cloud review 4 (2026-10-03): performance sweep

Verbatim findings. Triage status is tracked at the bottom.

## Performance, ranked by impact

1. Up to ~1.4 s of frozen emulation (Qt) or lagging SFX (Android) after fast-forward ends. ReadOutputStretched grows the SPU output ring to at least one second (SPU.cpp:1296). When the emulator runs faster than the per-callback pull cap (Android 8x, Qt FastForwardFPS/TargetFPS), the ring fills to 65536 frames and BufferAudio drops the oldest; on exit the passthrough path drains the backlog at 1x. Qt audioSync() then blocks the emu thread until the ring is under 512 frames. Upstream's ring was 2048. Fix: drop the one-second floor (twice the per-callback pull), and on the stretch-to-passthrough transition discard all but the newest callback's worth.
2. WSOLA stretch runs under the SPU audio lock (SPU.cpp:1319); the emu thread takes the same lock in BufferAudio ~4000 times/s at 8x. Measured 150-207 us per 512-frame callback (1.4-1.9% lock duty). Single consumer; only cross-thread touch is InitOutput calling Stretcher.SetRate (SPU.cpp:1142). Move the rate check into the audio thread, run Process without the lock.
3. BgmRenderer holds its mutex for the whole render (BgmRenderer.cpp:639); the emu thread calls Bgm.Playing() every frame while hosting (SndCmdTracker.cpp:833) and forwards fader steps. Make Playing, Active and Tick relaxed atomics.
4. Work when disabled / per SFX: ParseDriverInfo runs every frame regardless of Enabled (SndCmdTracker.cpp:845), 4616 bytes byte-by-byte into a fresh vector (gate on Enabled, memcpy, member buffer). OnStart copies and CRCs the whole SBNK for every START/PREPARE incl. cries (SndCmdTracker.cpp:479); compute only when eligible for hosting. Every start logs at Info (SndCmdTracker.cpp:482); demote to Debug.
5. Host adoption hitch: CopyRAM byte-by-byte with mask (SndCmdTracker.cpp:151), Load copies SBNK/SWAR again (BgmRenderer.cpp:281), SWAV::Read converts into a third buffer. memcpy when not crossing the mirror; parse the tracker's vectors directly.
6. Seek cost grows with song age: Start(atTick) single-steps from zero (BgmRenderer.cpp:328). Record the loop period once the backward jump executed and seek modulo it.
7. SSEQ renderer maintains interpolation history with interpolation off (Channel.cpp:694). Skip in that mode; clear history on mode change.
8. Stretcher search window wider than documented: SearchRadius forced up to the synthesis hop (TimeStretch.cpp:52), 301 candidates in double. Use the documented radius, floats, skip when reference energy ~0.
9. Smaller: Qt callback does two string-keyed config lookups per callback and reads toml from the audio thread (EmuInstanceAudio.cpp:173), cache in updateRealtimeBgmSettings. ProducedRatio computed under lock, unused; remove or use to smooth the stretch ratio (which jumps per callback and warbles effects). EmuThread reads refresh rate via QWidget::screen() off the GUI thread (EmuThread.cpp:441); cache it. Android copyGpuFramebuffers adds two full-res blits per rendered frame; sample the array texture in the presentation shader instead.

## Correctness

Dangling ROM pointer after cart eject: NDS::EjectCart (NDS.h:372) bypasses SetNDSCart, tracker keeps the freed pointer; eject, enable feature, game starts a sequence -> Idx.Build reads freed memory. Hook EjectCart to call OnCartChanged(nullptr, 0), or resolve the pointer from the cart slot at build time.

## Verified clean

Frameskip path, SPU mute masking, per-frame tracker work when enabled, PXI sniffer, savestate section, Android JNI hooks. Android core byte-identical outside the OpenGL ES port.

## Status (triage 2026-10-03)

Commits: desktop (melonDS fork, realtime-bgm-fastforward) / android-lib (nelonds-android-lib); app e7e08c0d bumps the core. Monorepo: f86049b2 (melonDS), 25b24924 (android-lib).

- Finding 1 (ring backlog after fast-forward): fixed, desktop f0029745 / android-lib 73ca7ea0. The ring holds twice the per-callback pull (no one-second floor); the first passthrough read after stretching (SPU::ReadOutput, used by Qt's non-stretch path and by ReadOutputStretched near 1x on Android) keeps only the newest callback's worth.
- Finding 2 (stretch under the SPU lock): fixed, desktop 9d9d423a / android-lib 3bf76c19. InitOutput only flags the stretcher; ReadOutputStretched locks once per callback for grow + ring copy, then SetRate/Reset/Process run unlocked on the audio thread.
- Finding 3 (renderer flags): fixed, desktop b762eb05 / android-lib bc8d74e6. Playing/Active/Tick are relaxed atomics stored under the lock by Render() and by every control call that changes them.
- Finding 4 (work when disabled / per SFX): fixed, desktop da8b3313 / android-lib b6fb75e6. ParseDriverInfo only when enabled, into a member buffer; CopyRAM uses memcpy unless the range wraps the RAM mirror (also serves finding 5); bank CRC only for eligible sequences (EnsureIndex fills it in for players the index reclassifies); per-start log at Debug.
- Finding 5 (adoption hitch): fixed, desktop 3ca979dc / android-lib 54810881. Load parses the tracker's SBNK/SWAR vectors in place. SWAV::Read's per-byte conversion buffer: kept, not in this round.
- Finding 6 (seek cost): fixed, same commits. The seek compares the sequencer state after a backward jump with the state after the previous backward jump of the same tracks and then skips whole loop periods. Output byte-identical to the full seek on five HeartGold songs; seek to tick 200000 ~1 ms instead of ~8 ms.
- Finding 7 (interpolation history with interpolation off): kept, not in this round.
- Finding 8 (stretcher search window): kept, not in this round.
- Finding 9: config flags cached in updateRealtimeBgmSettings, refresh rate cached by MainWindow::onTitleUpdate on the GUI thread, ProducedRatio removed: desktop c27c3505 / android-lib 7bcc0c99. Ratio smoothing and Android copyGpuFramebuffers: kept, not in this round.
- Dangling ROM pointer after eject: fixed, desktop 7895e613 / android-lib 7a014b2d (NDS::EjectCart calls OnCartChanged(nullptr, 0); DSi::EjectCart goes through it).
