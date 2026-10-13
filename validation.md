# Local validation

Source: codex/compute-wait-telemetry, uncommitted changes over 2d5cf67e8ea20ad8ebb983c9b207e24234e0356c. Source fingerprint and compiler/platform metadata are in provenance.json.

## Build and tests

Linux x86_64, GCC 13.3.0; SDL prefix /home/bradley/glob2/build/sdl3/prefix. Development build uses -O0 -g1, dev_fast=1, linker=auto.

Build: `GLOB2_SDL3_PREFIX=/home/bradley/glob2/build/sdl3/prefix python3 tools/dev_build.py unit-tests engine-tests software-render-benchmark build/linux/client/debug/dev-dev_fast-true-linker-auto/src/glob2 -j8`. Final log: build-source-final.log; succeeded.

Registry commands use `python3 test/run_tests.py --build-dir build/linux/client/debug/dev-dev_fast-true-linker-auto --binary unit|engine`, with filters ComputeExecutor, GradientPipeline, BuildingGradientPipeline, ReadOnlyPhase (unit) and AIOrderScheduler, SharedWorkerLifecycle, SimulationReadPhase, ResourceGrowth, TurnEngineHarness, SoftwareRenderer, SceneExtract, CommandLine (engine). Final JUnit: unit-integrated-junit.xml (65 cases), engine-integrated-junit.xml (97 cases); zero failures, errors or skips. Includes historical save formats, save continuation and unchanged committed match checksum trace.

`python3 tools/test_compute_trace.py`: five tests passed. `git diff --check`: passed.

## Simulation equivalence

`python3 artifacts/compute-telemetry/check_modes_final.py`: nine combinations of Off/Waits/Trace and 1/2/4 threads passed. Per-tick checksums, replay bytes and final save bytes match exactly. Fixture: generator15, map seed 4242, 128x128, four teams, game seed 19, maxima/cortex/nicowar/maxima, AI delay 4, GPU off, 256 ticks. Save continuation to tick 320 in all modes on four threads matches uninterrupted execution. Commands and artifacts: check_modes_final.py, modes-final.log, games-final/. Common checksum SHA256: 787238e7b51d7fe0a946ab0b0b92c241f0272f1a57face8b680011f9be6244b6.

## Performance evidence and limits

Final disabled worker/callback specializations compiled with `g++ -std=c++20 -O3 -DNDEBUG -pthread -Isrc/common -Ilibgag/include -c artifacts/compute-telemetry/off-path.cpp`. off-path-final-symbols.txt has no clock or chrono references; disassembly retained in off-path-final.asm. Source specializations exclude diagnostic updates, allocations and per-job mode checks.

Paired release microbenchmark samples and confidence intervals are retained in benchmark-samples.jsonl and benchmark-summary.jsonl. Twelve repetitions rotate mode order and pin four CPUs. The control removes owner mode dispatch from current Off execution. CPU ratios Off/control: empty 1.025 [0.990,1.066], tiny owner 0.758 [0.739,0.778], tiny worker 1.0004 [0.946,1.059], substantial 0.994 [0.937,1.060]. These measurements preceded the final nested-call guard on the empty submission shortcut; they do not establish final-code zero overhead. Other builds ran concurrently, making wall measurements noisy. Single-run enabled-overhead.json illustrates memory and CPU costs but is not a robust performance estimate. Bounded trace overflow is measured and exposed.

Windows, macOS, mobile and browser equivalence were not run. Representative retained-game release overhead, tick latency and robust enabled overhead remain unverified. No claim of zero overhead or release readiness. Evidence remains local and has not been published to a PR.
