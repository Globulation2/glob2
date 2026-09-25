# Desktop regression follow-up for PR #208

The source revision is in [source-revision.txt](source-revision.txt). These results
extend the earlier [responsive evidence](../responsive/README.md).

## Changes and test integrity

- Fixed an actual presentation regression: resizing a loaded game before its first
  draw had no initialized camera bounds and lost the legacy desktop center tile.
  The original two center-tile assertions in `EngineSessionHarness` remain intact.
  Added exact world-center/zoom and unchanged-simulation checks for later zoomed resizes.
- Corrected two stale landscape-picker expectations. Commit `1c694e5ee7` (September 17)
  had deliberately reserved the wheel for grid scrolling and made arrow navigation
  follow displayed sort order. The old harness still expected wheel zoom and registry
  order. It now asserts both scroll directions, unchanged preview zoom/pan, no accidental
  confirmation, and navigation through displayed order. `MapPreviewHarness` retains
  and passes its independent anchored-zoom assertions.
- Fixed `WindowResizeHarness` readback on Retina: input/sample positions are window
  coordinates, while the GL buffer is in drawable pixels. Convert coordinates before
  sampling. The original cache colors, clipping, resize, context-lifetime and recreation
  assertions all remain. The previously failing macOS GL case now passes.
- Replaced browser `.fill()` substitutions with visible mouse focus, native Select All,
  Delete, empty-value assertion, real typed input and exact-value assertion. Password
  fields also assert password type. An initial Home/Shift-End helper passed Chromium
  but failed macOS Firefox because those native caret commands differ; the platform's
  Select All shortcut passes the Firefox recheck.

The [original baseline CI run](https://github.com/Globulation2/glob2/actions/runs/36078336281)
for `458dc357968118d30ee8030b1dcc3227dd66c022` already failed the identical picker
zoom assertion on both Linux jobs. Its revision/job metadata and Ubuntu 22.04
[failure excerpt](logs/baseline-linux-failure-excerpt.log) are attached. This establishes
that the picker test mismatch predates the responsive commits.

No save/map fixtures, golden outputs, simulation rules, or format/version gates changed.
The only production change in this follow-up is the first-draw camera fallback.

## Native results

- All **203 CppUnit cases** pass, plus **22 standalone executables** from `test/SConstruct`.
  See [unit-results.json](unit-results.json) and individual logs.
- Full desktop settings matrix: six configurations pass (640×480, 800×600, 1000×700,
  1280×900 OpenGL, 1000×700 software, and 640×480 expanded labels).
- Full custom setup visual harness, engine session suite, game-speed/replay suite,
  selection lifetime, screen lifecycle, GL/software aspect checks, text rendering,
  standalone preview gestures, and corrected GL/software resize checks pass.
- Software map-render/resize suite passes with SDL's dummy driver. The normal macOS
  desktop constrained the required 1800×1100 logical window, and the OpenGL fixture
  explicitly requires an unscaled drawable. Those unsuccessful runs are retained;
  they do not establish GL map-render coverage. Linux CI uses the required Xvfb surface.
- All six responsive harnesses pass again after the camera fix.
- macOS, browser and iOS simulator builds succeed. No new physical-device or iOS
  runtime qualification is claimed.

[desktop-results.json](desktop-results.json) retains the initial unsuccessful invocations
alongside the corrected final runs; a failed initial entry is not hidden by its rerun.
[Native screenshots](native-screenshots.zip) and [build logs](build-logs.zip) are attached.

## Reproduce

```sh
scons -C test -j8
(cd test && ./TestsRunner)
# Run each generated standalone *Harness and *Test executable in test/ as well.
scons release=1 custom-setup-test map-preview-test aspect-test resize-test \
  text-raster-test map-render-resize-test speed-tests settings-tests \
  selection-test screen-test session-test -j8
python3 test/run-settings-tests.py
python3 test/run-game-speed-tests.py
mkdir -p artifacts/desktop-regression/setup
GLOB2_USER_DATA_DIR="$PWD/artifacts/desktop-regression/profile-setup" \
  build/darwin/client/release/src/CustomGameSetupHarness artifacts/desktop-regression/setup
GLOB2_USER_DATA_DIR="$PWD/artifacts/desktop-regression/profile-session" \
  build/darwin/client/release/src/engine-session-test desktop-session
build/darwin/client/release/libgag/src/WindowResizeHarness gl
build/darwin/client/release/libgag/src/WindowResizeHarness software
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  GLOB2_USER_DATA_DIR="$PWD/artifacts/desktop-regression/profile-map" \
  build/darwin/client/release/src/MapRenderResizeHarness desktop-map
```

Substitute the appropriate build platform directory on Linux or Windows. Native
screenshot arguments in these legacy harnesses are repository-relative paths.
## Browser results

- **90/90** save, editor, import, persistence and single-player checks pass.
- **42/42** multiplayer checks pass, including native/browser TCP and WSS simulation
  checkpoints, login/password editing and credential/version boundaries.
- Both runs cover Chromium, Firefox and WebKit on the rebuilt browser client.
- Initial interrupted run: 46 passes, two Firefox helper failures, one interrupted
  case and 83 unrun cases. Its original log and failure screenshots/context are retained.
  Both failed cases pass after using native Select All; the full rerun above is clean.

```sh
cd browser
GLOB2_TEST_RENDERER=software npx playwright test tests/single-player.spec.js \
  tests/editor-storage.spec.js tests/campaign-editor-storage.spec.js \
  tests/import.spec.js tests/storage.spec.js tests/replay-save.spec.js --workers=3
GLOB2_TEST_RENDERER=software npx playwright test tests/multiplayer.spec.js
```

CI now runs the complete six-configuration desktop settings matrix, responsive and
native editing flows on Firefox/WebKit, and responsive WebGL flows on Chromium.
The former quick settings check and startup-only cross-browser subset did not cover
these regressions adequately. All 30 build-system checks pass after the CI edits.


## Final graphics and simulation checks

The final graphics run passes **57 checks** across Chromium, Firefox and WebKit:
desktop right-click selection/sidebar behavior, browser shortcuts, missing-motion
click coordinates, resize, small/wide initial viewports, WebGL context recovery,
software fallback, responsive editing and retained placement/save/load state.
Three software-only immediate-scale checks intentionally skip under WebGL; a
separate software invocation passes all **3**. Logs retain both results.

The final native and all three browser traces are byte-identical over 1,500 ticks,
seed 42, using the same initial save and orders. See [checksum-comparison.json](checksum-comparison.json)
and [simulation-evidence.zip](simulation-evidence.zip), which includes the save,
replay and four traces. The SHA256 remains
`110443ab4a136dff6621f32dbeed150a15c18394791874e3774ef6cbe27c3c48`.

```sh
cd browser
GLOB2_TEST_RENDERER=webgl2 npx playwright test tests/input.spec.js \
  tests/rendering.spec.js tests/viewport.spec.js tests/responsive-presentation.spec.js --workers=3
GLOB2_TEST_RENDERER=software npx playwright test tests/viewport.spec.js -g 'interface scale'
npx playwright test tests/determinism.spec.js
```

[Browser screenshots](browser-screenshots.zip) include wide desktop and responsive
views from these runs. Cross-platform CI and physical-device qualification are
separate from these local results; the PR remains a draft.

## Platform CI confirmation

Both **Ubuntu 22.04 and Ubuntu 24.04 desktop jobs pass** on revision `7b6f7e565`,
including the formerly failing setup harness, session/camera tests, full settings
matrix and GL/software map rendering under Xvfb. Both Linux variants and map-generator
jobs also pass, as do all three Android builds. Windows and browser CI were still
running when this evidence was published; the PR remains draft.

[Job/step results](ci-jobs.json) and [check snapshot](ci-checks.json) link to the
[CI run](https://github.com/Globulation2/glob2/actions/runs/36182887703).
The native checksum artifacts downloaded from both successful Ubuntu jobs match
macOS and all three browser traces byte-for-byte. Both Linux traces are included
in the simulation archive and checksum manifest above.
