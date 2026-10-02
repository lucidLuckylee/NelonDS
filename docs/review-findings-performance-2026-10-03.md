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
