#!/usr/bin/env bash
# Run from the source root at 1807fbf8e48cf57f3f211841eb1a2568804070b5.
# Native dependency versions and affinity are recorded in combined-environment.json.
set -eu
BIN=build/linux/client/release/src/glob2
OUT=artifacts/presentation
CCACHE=1 GLOB2_SDL3_PREFIX=/home/bradley/glob2-claude/build/sdl3/prefix taskset -c 0-11,16-27 scons -j12 release=1 server=0 optimized_assets=0 unit-tests engine-tests "$BIN"
CCACHE=1 taskset -c 0-11,16-27 scons target=web release=1 -j12 web-package

taskset -c 0-11,16-27 python3 test/run_tests.py --binary engine --jobs 12 \
 --filter 'ClientChannels/*' --filter 'WorldSnapshot/*' --filter 'SceneExtract/*' \
 --filter 'SharedWorkerLifecycle/*' --filter 'GameGUISelection/*' --filter 'GameGUITouch/*' \
 --filter 'EngineSession/*' --filter 'TurnEngineHarness/*' --filter 'GUIInteractionCoverage/*' \
 --filter 'GUIOrderCoverage/*' --filter '*Save*' --filter 'FarmAreas/*' --filter '*Gradient*' \
 --filter 'CortexGeometry/*' --filter 'AIPipeline/*' --filter 'AIOrderScheduler/*' \
 --filter 'SimulationReadPhase/*' --filter 'ReadOnlyPhase/*' \
 --junit "$OUT/combined-engine.xml" --artifacts "$OUT/combined-engine"
python3 test/run_tests.py --binary unit --filter 'ComputeExecutor/*' --filter 'SceneBuffer/*' \
 --filter 'PerformanceTelemetry/*' --filter 'FogFade/*' --junit "$OUT/combined-unit.xml"
python3 test/build_system/test_scene_boundary.py
GLOB2_SYNC_RAND_STRICT=1 taskset -c 0-11,16-27 python3 test/check_sim_thread.py "$BIN" \
 --baseline "$BIN" --candidate-env GLOB2_SIM_THREAD=1 --output "$OUT/combined-sim-equivalence"
GLOB2_SYNC_RAND_STRICT=1 taskset -c 0-11,16-27 python3 test/check_sim_thread.py "$BIN" \
 --baseline "$BIN" --candidate-env GLOB2_SIM_THREAD=1 --candidate-env GLOB2_SNAPSHOT_VERIFY=1 \
 --candidate-args '--compute-threads 1' --output "$OUT/combined-fallback-equivalence"
taskset -c 0-11,16-27 python3 test/run-browser-determinism.py "$BIN" "$OUT/combined-browser-native-reference"

for cpus in 2 4; do
 if [ "$cpus" = 2 ]; then affinity=14,15; else affinity=12-15; fi
 for trial in 1 2 3; do
  taskset -c "$affinity" python3 test/run_tests.py --binary engine --tag benchmark \
   --filter 'EngineSession/snapshot client frame latency*' \
   --junit "$OUT/combined-frame-${cpus}cpu-$trial.xml" --artifacts "$OUT/combined-frame-${cpus}cpu-$trial"
 done
done
GLOB2_SCENE_BENCH=1 taskset -c 12-15 build/linux/client/release/test/glob2-engine-tests --test-suite=ScenePerformance

cd browser
# Playwright config owns the server; run invocations sequentially.
GLOB2_TEST_RENDERER=software taskset -c 0-11,16-27 npx playwright test \
 tests/runtime-build.spec.js tests/viewport.spec.js tests/session-reload.spec.js \
 --project=chromium --grep 'threaded startup|running match|same saved game repeatedly' --reporter=line
# The initial resize case timed out in main-menu startup; this is its isolated rerun.
GLOB2_TEST_RENDERER=software taskset -c 0-11,16-27 npx playwright test tests/viewport.spec.js \
 --project=chromium --grep 'running match' --reporter=line
GLOB2_TEST_ENTRY_PATH='/?threads=0' GLOB2_TEST_RENDERER=software taskset -c 0-11,16-27 npx playwright test \
 tests/viewport.spec.js tests/session-reload.spec.js --project=chromium --project=firefox \
 --grep 'running match|same saved game repeatedly' --reporter=line
GLOB2_TEST_RENDERER=software taskset -c 0-11,16-27 npx playwright test \
 tests/runtime-build.spec.js tests/viewport.spec.js --project=firefox \
 --grep 'threaded startup|running match' --reporter=line
taskset -c 0-11,16-27 npx playwright test tests/determinism.spec.js --project=chromium \
 --grep 'complete per-tick simulation trace|verifies the committed match record' --reporter=line
GLOB2_CHROMIUM_ANGLE=swiftshader taskset -c 0-11,16-27 npx playwright test tests/rendering.spec.js \
 --project=chromium --grep 'WebGL2 draws a playable match' --reporter=line

cd ..
python3 - <<'CHECK_TRACES'
from pathlib import Path
native=Path('artifacts/presentation/combined-browser-native-reference/native.replay.checksums').read_bytes()
for variant in ('serial-1','threaded-1','threaded-2','threaded-4'):
 assert (Path('artifacts/browser-determinism/wasm')/variant/'wasm.replay.checksums').read_bytes()==native
expected=Path('test/fixtures/multiplayer/FourSquares1.verify-trace.txt').read_bytes()
assert Path('artifacts/presentation/combined-browser-native-reference/verify-match.checksums.txt').read_bytes()==expected
for name in ('verify-match.checksums.txt','verify-threaded/verify-match.checksums.txt'):
 assert (Path('artifacts/browser-determinism/wasm')/name).read_bytes()==expected
CHECK_TRACES
