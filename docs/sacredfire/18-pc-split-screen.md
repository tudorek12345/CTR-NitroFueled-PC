# PC split screen, prompts and crash investigation

Updated 2026-10-30. Based on Windows v0.4, CUSA13795 base + update 1.21.

## Running and controls

Open `CTR-NitroFueled-PC.exe` in the portable folder. It runs `run.py`, which launches
`bin/sacredfire.exe` against the prepared folder selected in `launcher.ini` (`game_dir`).
Game PKGs must already be extracted and prepared using the existing launcher workflow.
No game binaries or extracted assets belong in this source repository.

In Controls select **Split screen: keyboard/mouse + controller**. This writes
`port.1=keyboard_mouse`, `port.2=auto`; a specific SDL controller can be selected instead.
In Controls > Keyboard select **PC racing controls**, then restart the game. Enter confirms
for the keyboard player; confirm the title screen with the keyboard first to make it the first
on-screen player. Choose two players in Local Arcade and join with the controller.

| Action | PC racing preset |
| --- | --- |
| Accelerate | W |
| Brake/reverse | S |
| Steer | A / D |
| Hop/drift | Space / right mouse |
| Other drift button / timed boost | Left Shift / middle mouse |
| Item | E / left mouse |
| Confirm / back / pause | Enter / Backspace / Escape |
| Menu navigation | Arrow keys |
| Look back / camera | C / R (also mouse side buttons) |

Mouse movement never steers. `mouse.<PS4 action>=left,right,middle,x1,x2` accepts a comma-separated
list; empty disables a binding. These are aliases of the game's actions, not an automatic
drift/boost macro. Player 2 retains the independent controller mapping. Focus loss, text input
and the overlay neutralize PC input. Player presence follows controller connections.

## Installing Xbox font prompts

The optional tool needs Python 3 and Pillow (`python -m pip install Pillow`). Use the extracted
roots containing `archives`, not the PKGs. `--game` must be the separate merged folder already
prepared by the launcher. Base and update are the original extracted inputs:

```powershell
python tools/xbox_button_icons.py --base C:/Games/CUSA13795 --update C:/Games/CUSA13795-patch --game C:/Games/CUSA13795-1.21
```

If an extractor puts files under `uroot`, pass that subdirectory. Launch the modified prepared
folder through the launcher. The operation is idempotent; add `--verify-only` to compare all six
fonts without writing. Keep the original extracted inputs; preparing a fresh merged folder
restores the original prompts.

The tool adapts the community font patcher to the base-game glyph codes as well as update
private-use glyphs. Each icon is resized to that font's own rectangle, then tiled BGRA pixels
are rebuilt with the existing archive code. All replacement font bytes are checked before an
archive replaces its target directory entry. Only the three named archives become private
copies; originals linked elsewhere remain intact. Controller diagrams and prerecorded videos
are separate assets and keep their original prompts.

The tool and dependencies are source utilities; Pillow is not bundled into the portable
runner. See [third-party notices](../../THIRD-PARTY-NOTICES.md) for GPL code and CC0 artwork.

## Observed failures and limits

The initial two-player failure was an access violation inside `pthread_mutex_trylock`.
The old magic-value check could succeed just before another thread destroyed the native
mutex. Reference counts now pin mutex and condition-variable handles across native calls;
destruction reserves an idle object atomically and reports EBUSY for active users. Wrappers
remain allocated to make stale copied guest handles report EINVAL. Condition waits pin both
the condition and mutex. The deterministic regression pauses a native lock call after the
old validation window, then attempts destruction; it failed on the old implementation.

A later race, after changing tracks from Mystery Caves to Blizzard Bluff, ended in renderer
`SurfaceFormat` assertion `data_format=6`, `num_format=0`, exit 23. It was preceded by buffer
size-clamping warnings. This is separate from the mutex access violation; its root cause is
not established. The assertion remains enabled instead of substituting an invented format.

The optional Windows `gpu_safe_mode=1` sets `BB_DRAW_PIPE=0`, `BB_PREP_WORKERS=0`, and
`BB_HONEST_LABELS=1` for each launch. It does not disable Vulkan recorder threads or asynchronous
submission. It is a diagnostic experiment, off by default, and may reduce FPS. The first
conservative-mode validation launch also ended with a native read fault on the guest command
queue thread, exit 3, before a race. Therefore it is **not validated as a stability fix**.
Keyboard/controller separation and Xbox prompts were observed in a running two-player race;
the later crash prevents any claim of sustained stability.

## Checks

```sh
ninja -C out/gpu pad-input-test mutex-lifetime-test
# Run these with the build toolchain's runtime libraries on PATH:
out/gpu/pad-input-test.exe
out/gpu/mutex-lifetime-test.exe
python -m unittest discover -s tests -p test_gpu_safe_mode.py
```

Linux builds can run the input and mutex checks through `bash build.sh --test`.
The Windows native tests and both environment tests pass. The full Python suite has the
same four Windows failures on this change and untouched upstream: two symlink privilege
errors, a shell-runner dependency error, and a Linux host-driver discovery expectation.
Linux native tests were not executed on this Windows host. Live images, logs, dumps, toolchain
downloads and game-derived outputs are intentionally excluded from the commit.
