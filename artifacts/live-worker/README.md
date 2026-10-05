# Live walking-worker prototype verification

Tested commit: **0a959f04c55d20c947bbc1a5ddb35a8eaa1d437e**. Base: **72f8c9373fbdd9bf6206cfdf4903d12595e91de1**. Current fetched master **3908fe832a62ba8690076ff027b219200d262923** adds coverage-upload tooling only; `git merge-tree --write-tree HEAD origin/master` was conflict-free (tree `68a34646099a3df2cd38b3a2ed8764cc154999f0`). Native render dependencies and sources relevant to this prototype did not change in that newer base.

Environment: macOS 26.6.2 arm64, Apple M3, Apple LLVM 21.0.0, Python 3.14.7, Blender 3.6.23 arm64 from the official Blender archive. Pinned SDL3 3.4.16, SDL_image 3.4.6, SDL_ttf 3.2.2, SDL_net 3.2.0 and libwebp 1.6.0. Native flags `-g -std=gnu++20 -Wall -fPIC -O3`, `release=1 server=0`, OpenGL. Viewer renderer: `Apple M3`, `2.1 Metal - 90.5`, Cocoa, 1024x960. Pillow 12.2.0 and NumPy used for fixture preparation and image comparison.

## Results

- The original 32-sample assumption did not reproduce the source's alternating gait halves. The implementation uses 64 control samples (two eight-frame halves), model-space heading rotation and eight small translation calibrations. This is control data, not baked vertex poses. Original sources and shipped meshes are unchanged.
- Rig: 430,731 bytes, DEFLATE level 6 96,379 bytes. Baked walking worker: 17,502,756 bytes, same compression 16,181,493 bytes. Raw reduction 97.54%; compressed reduction 99.40%. This is one walking-worker clip, not the entire skin package.
- All 256 reference poses have identical topology and UVs. Maximum projected coordinate error 0.000064906 logical pixels (limit 0.05); maximum normal-vector error 0.000068767 (limit 0.001).
- Four new native rig cases pass (3,677 assertions in the focused standalone run). Registered tests cover malformed/transactional loading, finite controls, periodic keys, bounded cache/identities, interpolated translation and continuous heading.
- Final focused native runner: nine cases pass (four rig, five existing SkinMesh). Engine runner: two existing ColonySkinPreview cases pass. No failures/skips. The dedicated focused binary uses the same compiled test objects, native TestMain/provenance, libraries and flags as the ordinary test build; see `link_focused_tests.py` and build logs.
- Existing viewer opacity and cache diagnostics pass, including paint/material invalidation, region changes, texture address reuse and atlas overflow. CI policy contracts: 20 cases pass. Python compilation and `git diff --check` pass.
- 192 comparison PNGs contain all 256 reference poses with neutral, numbered-checker and mixed-material paint; four additional captures cover fractional phases and wrapping. Neutral, checker and mixed-material samples were visually inspected: matching silhouettes and paint placement, no obvious broken sockets or lighting. Renderer pixels need not be byte-identical; per-capture differences are retained in `capture-comparison.json`.

## Crowd measurements

Five alternating pairs per configuration, 512 workers, four paint variants, five warm-up frames then 40 measured frames per run. The final measurement was run with no concurrent builds, display tests or interactive viewer. Each configuration pools 200 measured frames per renderer; all individual times, per-run p95s and counters are in `crowd-final-benchmark.json`. No general hardware/platform performance claim is made.

- 32 distinct poses: pooled baked p95 3.224 ms, live 3.489 ms (+8.2%).
- 128 distinct poses: pooled baked p95 5.886 ms, live 5.966 ms (+1.4%).

Both configurations meet the prototype's pooled warmed p95 threshold of +25% on this machine. Per-run values vary and remain available for inspection. Median forced-cold deformation is approximately 54 ms for 32 poses and 211 ms for 128 poses; first live frames cost about 57 ms and 214 ms respectively. The 128-entry geometry cache uses 20,307,968 bytes (5,076,992 bytes at 32 entries), because each single-pose GSK object owns topology and UV arrays as well as vertices. Storage reduction does not imply memory reduction. Cold reconstruction is a clear bottleneck; this prototype is not a recommendation to replace production meshes yet.

## Exact commands

Run from the repository root. Blender is downloaded locally under ignored artifacts; substitute its executable path below.

```sh
blender --background --factory-startup --disable-autoexec -t 1 --python-exit-code 1 --python tools/skins/export_live_worker.py -- --output artifacts/live-worker
artifacts/pr220/python/bin/python tools/skins/prepare_live_worker.py artifacts/live-worker
GLOB2_SDL3_PREFIX=build/sdl3/prefix scons -j8 release=1 server=0 skin-preview
GLOB2_SDL3_PREFIX=build/sdl3/prefix scons -j8 release=1 server=0 tests skin-preview
GLOB2_SDL3_PREFIX=build/sdl3/prefix scons -j8 release=1 server=0 engine-tests skin-preview build/darwin/client/release/test/unit-support_TestMain.o
GLOB2_SDL3_PREFIX=build/sdl3/prefix scons -j8 release=1 server=0 build/darwin/client/release/test/unit-tools_skins_LiveWorkerRig.o build/darwin/client/release/test/unit-tools_skins_LiveWorkerRigTest.o
python3 artifacts/live-worker/link_focused_tests.py
python3 test/run_tests.py --build-dir artifacts/live-worker/focused --binary unit --filter 'LiveWorkerRig/*' --filter 'SkinMesh/*' --display-jobs 1 -j4 --junit artifacts/live-worker/focused-tests.xml --artifacts artifacts/live-worker/focused-render --write-inventory artifacts/live-worker/focused-inventory.json
python3 test/run_tests.py --binary engine --filter 'ColonySkinPreview/*' --display-jobs 1 -j4 --junit artifacts/live-worker/native-tests.xml --artifacts artifacts/live-worker/native-render --write-inventory artifacts/live-worker/native-inventory.json
build/darwin/client/release/src/skin-preview artifacts/live-worker artifacts/live-worker/final --live-verify
build/darwin/client/release/src/skin-preview artifacts/live-worker artifacts/live-worker/view --live-capture
build/darwin/client/release/src/skin-preview artifacts/live-worker artifacts/live-worker/opacity --validate-opacity
build/darwin/client/release/src/skin-preview artifacts/live-worker artifacts/live-worker/cache --validate-cache
build/darwin/client/release/src/skin-preview artifacts/live-worker artifacts/live-worker/crowd-final --live-benchmark
python3 -m unittest discover -s test/build_system -p test_ci_policy.py
```

## Limits and retained failures

The ordinary full `tests` build fails in existing base tests `FertilityFieldTest.cpp`, `GradientTest.cpp` and `MapQueryTest.cpp`, which access now-private `Map::tiles`; these files are unchanged in the prototype. Error logs are retained, with the focused build/link workaround fully recorded rather than modifying unrelated map tests. The complete native suite is not claimed to pass.

Interactive viewer startup was exercised, but manual key-by-key UI acceptance was not completed: the computer-use app inventory does not recognize the unbundled SDL executable. Rendering, capture, numerical verification and benchmark modes were exercised. Browser/mobile/Windows/Linux, real gameplay, all other models/actions, memory/performance acceptance on other hardware, production rollout and simulation determinism comparisons were not tested. No simulation, save/replay/network format or production graphics code changes are introduced, so no SIM_REVISION bump is needed. A maintainer's visual acceptance remains appropriate before any broader conversion.
