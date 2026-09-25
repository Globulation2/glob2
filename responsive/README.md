# Responsive presentation evidence for PR #208

Source revision is recorded in [source-revision.txt](source-revision.txt).
This directory covers the responsive extension; files in the parent directory
belong to the earlier mobile-port review and are not new validation of this revision.

The final source revision was rebuilt for macOS, iOS simulator and browser.
The six native harnesses, 30 build-system checks, responsive browser flows on
both renderers, and native/browser checksum comparison were rerun after the final
keyboard-confirmation change. The broader 129-check regression sweep, 42 multiplayer
checks, 39-check acceptance run and desktop pixel comparison cover the preceding
implementation in this same commit series; their original logs are retained.

## Results

- Native macOS release build and the policy, renderer, screen lifecycle, menu,
  setup/settings and gameplay harnesses pass. The desktop settings suite also
  verifies all three persisted preferences and missing/invalid preference fallback.
- iOS simulator release build passes with Xcode 27.0. Runtime validation is **not
  established**: the task-owned iOS 26.5 simulator stalled during startup and app
  launch, including a bounded retry after shutdown/reboot. No current iOS checksum
  comparison is claimed.
- The 129-check browser regression sweep produced 128 passes and one Firefox
  replay-load timeout. That exact check passed in isolation in 36.5 seconds.
  Both the sweep and rerun logs are retained; the timeout remains a stability limit.
- All 42 browser multiplayer checks pass across Chromium, Firefox and WebKit,
  including native/browser simulation checkpoints over TCP and WSS and version
  rejection before transmitting credentials.
- Forced software and WebGL responsive flows pass in all three browser engines:
  native Unicode editing and selection, rotation, simulated keyboard occlusion,
  touch preview, rotation, cancellation, save, browser reload and game load.
  The final software run has 9 passes (six responsive flows plus three
  simulation traces); the preceding broader acceptance run has 39 passes.
  Final WebGL run: 6 passes, in `logs/browser-host-webgl.log`.
- All 30 build-system checks, strict translation validation, translation tests
  and font coverage pass. Browser SDK calls stay inside the browser host.
- Native UI screenshots are in [native-ui-screenshots.zip](native-ui-screenshots.zip);
  browser screenshots and saved games are in [screenshots](screenshots/) and [saves](saves/).

Physical Android Chrome, iPhone Safari and native phone review remains outstanding.
The Android NDK/SDK is absent on this host, so Android compilation was not repeated.
Windows/Linux simulation checks were not run locally. Browser emulation is not
physical-device coverage. This evidence does not authorize merging the draft PR.
CI was still running at publication; its snapshot is in
[ci-checks-at-publication.json](ci-checks-at-publication.json).

## Simulation continuity

[simulation-evidence.zip](simulation-evidence.zip) contains the initial save,
native replay, and native/Chromium/Firefox/WebKit per-tick checksum sidecars.
All four traces are byte-identical over 1,500 ticks, seed 42, from the same
`games/cross-replay.game` fixture and orders. Hashes and sizes are in
[checksum-comparison.json](checksum-comparison.json).

Reproduce from the repository root:

```sh
mkdir -p artifacts/responsive-ui/determinism/native-profile
GLOB2_USER_DATA_DIR="$PWD/artifacts/responsive-ui/determinism/native-profile" \
GLOB2_REPLAY_PATH="$PWD/artifacts/responsive-ui/determinism/native.replay" \
GLOB2_CHECKSUM_SIDECAR=1 build/darwin/client/release/src/glob2 \
  --nox games/cross-replay.game 1500 1
cd browser
npx playwright test tests/determinism.spec.js
```

Verify the attached archive without running the game:

```sh
python3 - <<'PY'
import hashlib, zipfile
with zipfile.ZipFile('simulation-evidence.zip') as archive:
    reference = archive.read('native.replay.checksums')
    for browser in ('chromium', 'firefox', 'webkit'):
        assert archive.read(browser + '.replay.checksums') == reference
    print(len(reference), hashlib.sha256(reference).hexdigest())
PY
```

## Desktop presentation comparison

The baseline is PR head `458dc3579`, built in an isolated worktree with the same
Emscripten SDK. Both runs use Chromium, software rendering and 1200×900 CSS pixels.
Setup uses fixed wall time 12,345 seconds to give the generated preview the same seed.

[desktop-comparison.json](desktop-comparison.json) records exact crop coordinates:
72,120 menu-button pixels and 1,004,224 setup-panel pixels match exactly, with no
changed pixels. The animated main-menu scenery is excluded from the pixel
comparison. Full before/after screenshots are in [screenshots](screenshots/).

## Reproduction commands

```sh
scons release=1 -j8
scons release=1 mobile-input-test portable-renderer-test screen-test \
  responsive-menu-test mobile-presentation-test gameplay-touch-test settings-tests -j8
python3 mobile/ios.py build --environment simulator --release
python3 data/check_translations.py --strict
python3 test/test_translations.py
python3 test/test_font_coverage.py
scons target=web release=1 -j8
cd browser
npx playwright test tests/responsive-presentation.spec.js tests/determinism.spec.js \
  tests/rendering.spec.js tests/input.spec.js tests/single-player.spec.js \
  tests/editor-storage.spec.js tests/campaign-editor-storage.spec.js \
  tests/replay-save.spec.js tests/storage.spec.js tests/import.spec.js
npx playwright test tests/multiplayer.spec.js
GLOB2_TEST_RENDERER=software npx playwright test tests/responsive-presentation.spec.js
GLOB2_TEST_RENDERER=webgl2 npx playwright test tests/responsive-presentation.spec.js
```

Run the generated native harness binaries in `build/darwin/client/release/`.
Give each UI harness its own `GLOB2_USER_DATA_DIR` under `artifacts/`.
The settings harness arguments used here were `glob2-settings-test-responsive
1200 900 software ARTIFACT_SCREENSHOT_DIRECTORY`.

Build output is in [build-logs.zip](build-logs.zip); individual test results are in
[logs](logs/). Deliberate failed-write diagnostics inside passing save tests are
fault-injection evidence, not unreported test failures.
