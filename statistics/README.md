# Statistics redesign verification

- Tested source commit: `9772515800328531df5f92e45181293190077bf6`. Committing preserved the validated working tree byte-for-byte, fingerprint `db7f071dba6a7233aad7bc0ab95cc2888a7ac4523df3bcf786471d2b54a96cff`. Binaries were built with the source uncommitted on base `0d54ef3a99384059e68e78f03f774e062031a3ca`; their SHA-256 hashes are in revision.json.
- Latest master checked: `4dff11601cf73c311dd4b34034a53b0ed856f1fa`. Its later AI economy / simulation-version changes do not overlap this UI change and were left separate. The earlier desktop top-bar sizing fix is integrated in the tested base.
- Environment: macOS 27.2 arm64, Xcode Clang 21, development build `-O0 -g1`, existing native dependencies. No release-performance claim.
- Scope: presentation only; 200px desktop sidebar and centered 128px minimap, topic navigation, inline/full histories, mobile sheet integration, map overlays. No statistical arithmetic, sampling, save bytes, replay/network gates, simulation revision or runtime dependencies changed.

## Commands and results

```sh
PYTHONPATH="$PWD" python3 tools/dev_build.py -s -j8 tests build/darwin/client/debug/dev-dev_fast-true-linker-auto/src/glob2
python3 test/run_tests.py --build=build/darwin/client/debug/dev-dev_fast-true-linker-auto --filter 'GUIInteractionCoverage/*' --filter 'GameGUITouch/*' --filter 'GameSpeed/*' --filter 'PanelButtonHit/*' --filter 'UILayout/*' --artifacts artifacts/statistics/sidebar-200-final --junit artifacts/statistics/sidebar-200-final.xml
python3 test/run_tests.py --build=build/darwin/client/debug/dev-dev_fast-true-linker-auto --filter 'GameSpeed/live engine speed*' --junit artifacts/statistics/speed-200-rerun.xml
python3 test/run_tests.py --build=build/darwin/client/debug/dev-dev_fast-true-linker-auto --filter 'GameGUITouch/actual gameplay touch*' --junit artifacts/statistics/touch-200-rerun.xml
python3 data/check_translations.py --strict
python3 test/test_translations.py
python3 test/build_system/test_scene_boundary.py
git diff --check
```

The client and affected test binaries built successfully. The initial 73-case HUD/layout/touch run had 71 passing cases and two failures: a pause deadline exceeded during parallel display load, and a touch fixture that found no empty terrain outside a sheet after earlier camera gestures. The speed/pause/replay case passed in isolation without runtime changes. The touch helper was repaired to synchronize direct camera changes and reframe existing clear terrain; it does not alter the map or enqueue orders. The complete gameplay touch case then passed (155.9 seconds). Both rerun JUnit files and the original failure report are included.

The unchanged metric-series/catalog and save/history components also have 47 passing cases in wide-regression.xml (MetricSeries 6, MetricCatalog 15, TeamStatsSave 26); that older report includes unrelated initial HUD fixture failures, superseded by the final HUD evidence above. Strict translations passed with zero structural errors and registered English fallback keys. Translation unit checks passed 10 cases; scene-boundary checks passed 3 cases; whitespace checks passed.

Coverage includes metric reachability, real recorded zeros, waiting/legacy/early history, per-topic scrolling, mouse/tap chart inspection, Escape/Back and pause retention, input suppression, spectator eligibility, map-overlay toggles, and unchanged simulation checksum/order queue. Desktop centering is checked with equal 36px margins. Phone layouts use native SDL portable rendering at 360x720 and 720x360, with 100%/150% text; desktop includes enlarged German text.

## Visual evidence and acceptance

PNG captures use the native renderer and a frozen presentation fixture with 17 deterministic history samples at 512-tick intervals. The charts contain fixture data, rather than claiming a played-match result. navigation-walkthrough.mp4 is a stepped walkthrough assembled from those actual frames, not a continuous gameplay recording.

The maintainer approved the displayed 200px design and instructed merge in chat. No maintainer play-through or physical trackpad/touch evaluation is claimed. Browser input/builds (em++ unavailable), physical mobile devices and Windows were not verified. Native mouse and SDL touch paths were exercised. These platform and feel limits remain explicit for review.
