# Changelog

## Unreleased — 2026-10-30

Changes prepared against upstream `GetLostIn/CTR-NitroFueled-PC` main at
`0cf8def` (v0.4). These are source changes, not a new official release.

### Input and split screen

- Add a launcher preset for player 1 keyboard/mouse and player 2's first available SDL
  controller, with the same assignments available in the in-game Controllers tab.
- Reserve `port.1=keyboard_mouse` exclusively for PC input; `port.2=auto` claims an available
  controller without driving player 1. Specific controller selections remain supported.
- Transfer an already-open controller when assigning it between players, preserving trigger
  calibration. Handle player presence and controller disconnect/reconnect.
- Read mouse buttons on the window thread through an atomic snapshot. Suppress player 1's
  PC input while unfocused, typing, or using the settings overlay. Mouse movement does not steer.
- Add remappable `mouse.<action>` bindings and launcher controls for them.
- Add the PC racing preset: W accelerate, S brake/reverse, A/D steer, Space/right mouse
  drift, Shift/middle mouse drift boost, E/left mouse item, Enter confirm, Backspace back,
  Escape pause, arrow keys for menus. Controller bindings remain independent.

### Prompts and local game setup

- Add a reproducible Xbox font-prompt installation tool using the community GPL archive/font
  code and Xelu's CC0 icon sheet, with attribution and original license headers.
- Patch and verify all six font copies across `languagestartup.pak`, `permanent.pak` and
  `update.pak`, including the different base-game character codes and atlas positions.
- Write private archive copies and replace directory entries atomically so the launcher's
  hard-linked original base/update files remain intact. Static diagrams/videos are unchanged.
- The tested local installation uses the player's extracted CUSA13795 base + 1.21 update,
  launched through `CTR-NitroFueled-PC.exe`; no game files or personal paths are added here.

### Runtime and diagnostics

- Fix a mutex/condition-variable lifetime race: pin the native object for each host call,
  reject destruction while in use, retain dead wrappers for copied guest handles, and return
  guest errors for stale handles. This addresses the observed winpthreads access violation.
- Add an **experimental**, default-off conservative GPU synchronization option in the Windows
  launcher/runner (`gpu_safe_mode=1`): disable the draw pipeline and speculative preparation
  and wait for GPU fence completion. Environment overrides are rebuilt per launch/restart.
  Vulkan recording threads are a separate setting and are not disabled by this option.
- Keep GPU assertions intact. A later two-player race failed on an unsupported surface-format
  combination; a subsequent conservative-mode launch also faulted. **The renderer crash is
  unresolved; this option is not a proven fix.**

### Validation

- Windows runner and launcher built with the local MSYS2 CLANG64 toolchain.
- SDL virtual-controller input test passes: PC bindings, input isolation, focus/overlay
  suppression, held-trigger reassignment and reconnect.
- Mutex regression test passes: deterministic lock/destroy overlap, condition-wait lifetime,
  four-thread contention, stale/static/recursive handles. The deterministic race failed on
  the original code before the fix.
- Two GPU environment/restart tests pass. Full Python suite on Windows: 111 tests, 28 skipped,
  one failure and three errors. The same four Linux/symlink-dependent failures reproduce on
  unchanged upstream (109 tests), so this is not an all-platform test pass.
- Installed Xbox font bytes verified against rebuilt expected content in all three archives;
  Xbox prompts and a running two-player race were observed. Sustained race stability has not
  been established after the later renderer crash.

See [implementation and investigation notes](docs/sacredfire/18-pc-split-screen.md) and
[licenses and proprietary-content notices](THIRD-PARTY-NOTICES.md).
