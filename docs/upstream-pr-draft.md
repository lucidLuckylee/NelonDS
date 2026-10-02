Title: Keep background music at normal speed during fast-forward

Branch: lucidLuckylee/melonDS:realtime-bgm-fastforward → melonDS-emu/melonDS:master

---

## What this does

When fast-forwarding, game logic, graphics, sound effects and Pokémon cries run at the fast-forward speed as before, but background music keeps playing at 1× in real time, with correct instruments, loops, fades, pauses and track changes. It is on by default, with a checkbox under Interface settings ("Keep background music at normal speed during fast-forward"); it has no effect on games that don't use the Nitro SDK sound library and no cost outside fast-forward.

Closes #1476.

## How it works

Nitro SDK games don't touch the sound hardware from the ARM9. They send `SNDCommand` lists to the ARM7 sound driver over the IPC FIFO (PXI tag 7), and the list lives in main RAM. melonDS already emulates that FIFO in one place (`NDS::ARM9IOWrite32`, `0x04000188`), so a single branch per FIFO word gives us every sequence start/stop/pause, every fade (`PLAYER_PARAM` extFader), tempo ratio, track parameters, the channel masks each player may use, the shared-work block (player status, tick counters) and the per-frame `READ_DRIVER_INFO` snapshot, which contains exact SPU-channel ownership. No CPU hooks, works with the JIT, zero cost when no sound command is sent.

- `Sound/SdatIndex`: at cart insert, walks the Nitro FS for SDAT files and indexes every SSEQ by CRC of its MML bytes, with INFO/SYMB data. `AnalyzeMML`/`ClassifySeq` classify sequences (BGM, jingle, ambient loop, SFX, cry) from structure and the PLAYER record alone, so no per-game profiles are needed. The same classifier runs on the MML bytes in RAM when a sequence isn't in the index.
- `Sound/SndCmdTracker`: the FIFO sniffer and per-player state machine. While fast-forwarding, a BGM-classified sequence is handed to the host renderer, starting at the driver's tick counter. The ARM7 keeps playing it so everything the game can observe stays identical; `SPU::Mix` just drops the channels that sequence owns.
- `Sound/BgmRenderer` + vendored `Sound/SSEQPlayer` (WTFPL, from CyberBotX/fincs, vendored in-tree like `blip-buf` and `teakra`): renders the sequence from the SSEQ/SBNK/SWAR blobs copied out of main RAM, paced by the audio device. Driver volume mapping (centibel faders, `SND_CalcChannelVolume` table) is reproduced so loudness matches hardware. The player's modifications against upstream are reviewable separately: https://github.com/lucidLuckylee/SSEQPlayer/compare/master...melonds. The vendoring is its own commit so the remaining four commits are the actual review surface.
- Qt frontend: forwards the fast-forward state, mixes the renderer's output into the audio callback after `SPU::ReadOutput`, adds the `Audio.RealtimeBGM` setting.
- Track changes during fast-forward crossfade instead of cutting: the renderer keeps one outgoing voice, and fader commands are replayed at their 1x rate so a fast-forwarded fade keeps the shape the game intended.
- The host render was compared band by band against the emulated SPU path: it follows the user's `Audio.Interpolation` setting and the frontend's output skew, allocates notes only from the player's channel mask, and the vendored player was corrected against the Nitro SDK driver (attack envelope, pitch sweep, release threshold, PSG timer, pan law). Below 12.7 kHz it now sits within about 1 dB of the hardware path.
- `Sound/TimeStretch` + `SPU::ReadOutputStretched`: while fast-forwarding, the remaining emulated audio (effects, cries) is time-stretched (WSOLA, re-implementing the approach of #2738) so it plays N× faster at its original pitch instead of being dropped in chunks. Setting `Audio.FastForwardStretch`, default on, about 0.2 ms per 512-frame callback at 3×.

Design notes and the investigation (protocol verification against the NitroSDK source, classification results on retail SDATs, failure modes, on-device results): https://github.com/lucidLuckylee/melonDS/blob/realtime-bgm-notes/docs/realtime-bgm-design-notes.md

## Verified

- Headless (offscreen Qt) on Pokémon HeartGold: SDAT indexed (1372 + 829 sequences), both title sequences identified by CRC, driver snapshot recognised (`SNDWork` 4480 bytes), host renderer takes over and releases correctly.
- Audio A/B through the real frontend output (SDL disk driver): feature off vs on at 1× is waveform-identical; at 3× the hardware path plays the music 3× faster while the host path plays it at tempo ratio 0.996 / pitch ratio 0.998 relative to 1× within 0.3 dB loudness. Time-stretch at 3×: tempo 3.07, pitch 1.000.
- Fork CI (Ubuntu, Windows, macOS, BSD) green on the branch.
- On device (melonDS-android build of the same core patch, AYN Thor) with Pokémon White 2: overworld BGM continues at 1× during 3× and unlimited fast-forward while sound effects and cries speed up; fanfares, area changes and item pickups behave.
- `SdatIndex` fuzzed with ASan/UBSan on truncated, random and corrupted ROM images; `BgmRenderer` tested under ASan/UBSan/TSan with concurrent control and render threads.
- Savestates: tracker state goes in a new `NELO` section; savestates made before this change still load.

## Not covered / follow-ups

- Sequences inside SSAR archives are tracked but not rendered (games use SSAR for effects, not music).
- Fanfares and short environmental loops stay on the emulated clock by default (`RealtimeBgmSettings::JinglesAt1x`/`AmbientAt1x` exist in the core but are not exposed in the UI yet).
- Per-track fader/pitch/pan set before fast-forward starts are not replayed on mid-song entry.
- Slow-motion is not treated as fast-forward.
- Games that stream BGM (`STRM`) or use a non-Nitro sound engine are unaffected (feature stays inert).
- No CMake option to compile the feature out yet. Excluding the vendored player (its own commit), the change is about 2.4k lines. Easy to add if wanted.
- Time-stretch continuity at 3× was verified by tempo/pitch measurement; a listening pass for splice artefacts is still worthwhile. Slow-motion is not stretched.

## Licensing

`src/Sound/SSEQPlayer/` is kode54/SSEQPlayer (WTFPL), see `LICENSE.TXT` in that directory. All new melonDS files are GPL-3.0 with the standard header.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
