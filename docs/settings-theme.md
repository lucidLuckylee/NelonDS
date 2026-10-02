# Rose / plum settings theme

The Android settings activity has its own palette, leaving emulator rendering and the library theme unchanged. It follows the existing app-wide Light / Dark / System preference. The settings home exposes that preference directly and lays the categories out as a grid of cards that fits the Thor's top screen without scrolling: four columns at widths of at least 720 dp (the landscape top screen is 832 dp wide), two columns from 360 dp (portrait), one below that. The Theme and RetroAchievements cards span two columns so the ten cards fill complete rows. Individual settings pages remain a single scrolling column.

The palette comes from https://coolors.co/fae3e3-f7d4bc-8f2d56-c98bb9-846b8a, with the accents pushed more saturated (berry #A8235F in light, orchid #E890CD in dark) while backgrounds stay soft. Light mode uses blush and peach backgrounds, near-white cards, and berry accents. Dark mode uses aubergine backgrounds, plum cards, and orchid accents. Cards have 22 dp corners, a low elevation and a border close to the surface colour; home-card icons sit in a tinted circle. Text/accent contrast against backgrounds, cards, icon circles and the focus fill ranges from 5.06:1 to 15.06:1.

Preference cards retain the existing Android preference widgets and persistence. Preference dialogs (e.g. Theme) use the same panel. Keyboard/controller focus adds a colored outline and a 120 ms elevation transition. The Compose on-screen controls page shares the same resource palette and has radio-group semantics. “Hide all controls” uses the existing setting; it hides buttons on both displays without disabling DS touchscreen input.

## Build

From `android/`:

```sh
nix develop --command bash -c 'cd melonDS-android && bash ./gradlew --no-daemon :app:assembleGitHubProdNelon -Pandroid.injected.build.abi=arm64-v8a'
```

The root `.gitmodules` registers the existing native dependency gitlinks at their actual paths in this combined repository. Before a fresh build, run `git submodule update --init` from the repository root.

## Device checks

- Switch between Light, Dark, and System; verify dialogs and the on-screen controls page.
- Navigate the home grid and individual settings with the D-pad, select, and Back.
- Check both Thor displays for clipping and scrolling.
- Verify on-screen controls can be hidden and restored while touch input remains active.

Device installation replaces `me.magnum.melonds.dev` and closes any running game session; save before updating.

## Validation completed (2026-10-02)

- `assembleGitHubProdNelon` passed, including release optimization and vital lint. The final incremental build completed in 4m 46s.
- Installed on the AYN Thor with `adb install -r -t`; the signing certificate matches the previous installation. The ABI-injection option marks the APK test-only, requiring `-t`, but the build type remains `nelon` with release native flags and LTO.
- Verified the settings home in system-dark and explicit light modes on the top display, and D-pad focus movement with the new outline and elevation.
- Screenshots: [dark](screenshots/settings-dark.png), [light](screenshots/settings-light.png), [focus](screenshots/settings-focus.png).
- Remote input stopped when the device switched to another game. No gameplay/audio regression testing was performed for this UI change.

Follow-up on the same day, after the four-column home grid, palette adjustment and pause menu:

- `assembleGitHubProdNelon` passed and the APK was installed on the Thor.
- Top display (landscape): the settings home fits without scrolling in light and dark; D-pad focus moves through the grid; the Theme dialog uses the rose panel; switching Light/Dark applies immediately.
- On-screen controls page: renders in the palette, scrolls, D-pad focus shows the outline.
- Pause menu: opens on Back during a game, Resume (D-pad and button), Back-to-resume, Settings, Load state and Exit all behave as before; light and dark checked.
- Screenshots: [dark](screenshots/settings-dark.png), [light](screenshots/settings-light.png), [focus](screenshots/settings-focus.png), [dialog](screenshots/settings-dialog.png), [on-screen controls](screenshots/settings-controls.png), [pause menu dark](screenshots/pause-menu-dark.png), [pause menu light](screenshots/pause-menu-light.png).
- Not checked on the device: the bottom display (468 × 537 dp, which would get the two-column grid and scroll) and hiding/restoring controls in a game.

## Pause menu

The emulator pause menu now uses the same rose/plum panel, card backgrounds, day/night colors, and focus animation. It preserves the existing option list and dispatch behavior, adds a prominent Resume game action, and arranges actions in two columns on the Thor-sized display. Back and outside-tap cancellation resume the game as before.
