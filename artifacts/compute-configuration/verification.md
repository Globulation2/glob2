# Compute configuration verification

Tested commit: `e1d8bca98780df1374427cfbb6992107cba44a2b`. Base revision: `18f6926d08733a5a184030afe12b929006b57af3`.

Platform: macOS-26.6.2-arm64-arm-64bit-Mach-O (arm64), 8 logical CPUs. Toolchain:

```
Apple clang version 21.0.0 (clang-2100.3.34.2)
Target: arm64-apple-darwin25.6.0
Thread model: posix
InstalledDir: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin
```

Release C++20 build with `-O3`; simulation objects retain `-fno-fast-math -ffp-contract=off`. Dependency include/link versions and paths are retained in `build.log`, `build-final.log`, and `baseline/build.log`. Build caches were copied from the existing checkout and validated/rebuilt by SCons against the isolated source inputs. No concurrent building-gradient edits were carried into this branch.

## Commands and results

All commands run from the isolated compute-configuration checkout unless stated otherwise.

```sh
scons -j8 release=1 unit-tests engine-tests build/darwin/client/release/src/glob2
python3 test/run_tests.py --binary unit --no-display -j2 --filter 'ComputeExecutor/*' --filter 'GradientPipeline/*' --filter 'ReadOnlyPhase/*' --filter 'ClientChannels/*' --junit artifacts/compute-configuration/unit.xml
python3 test/test_cli_smoke.py --binary build/darwin/client/release/src/glob2 --artifacts artifacts/compute-configuration/cli --junit artifacts/compute-configuration/cli.xml
python3 test/run_tests.py --binary engine --no-display -j3 --exclude-tag benchmark --filter 'AIPipeline/*' --filter 'AIOrderScheduler/*' --filter 'GradientPreparation/*' --filter 'BuildingGradientInvalidation/*' --filter 'PathGradient/*' --filter 'SharedWorkerLifecycle/*' --filter 'SimulationReadPhase/*' --filter 'ResourceGrowth/*' --filter 'RuntimeResources/*' --filter 'EngineSession/*' --filter 'ReplayStepCounter/*' --filter 'MatchSetup/*' --filter 'TurnSession/*' --filter 'TurnMatchRecord/*' --filter 'TurnEngineHarness/*' --junit artifacts/compute-configuration/native.xml --write-inventory artifacts/compute-configuration/native-inventory.json
python3 test/check_parallel_compute.py build/darwin/client/release/src/glob2 --baseline BASELINE --output artifacts/compute-configuration/parity
python3 test/check_gradient_pipeline.py build/darwin/client/release/src/glob2 --skip-building --output artifacts/compute-configuration/gradient-parity
python3 test/benchmark_parallel_compute.py BASELINE build/darwin/client/release/src/glob2 artifacts/compute-configuration/benchmark-manifest.json --output artifacts/compute-configuration/timing --repeats 5
```

`BASELINE` was the production client built at the recorded base revision in a separate checkout using `scons -j4 release=1 build/darwin/client/release/src/glob2`. Its preserved executable is `baseline/glob2`; executable hashes and full commands are retained in the timing metadata and measurement rows.

- Build and final incremental build: passed.
- Unit: 24 passed, none skipped or failed.
- Native engine: 148 passed, five display cases skipped, none failed (153 inventoried cases).
- Production CLI: 12 passed, including byte-identical match verification results and golden traces across 1/2/4/8/auto.
- Engine parity: exact per-tick traces, replay bytes and final saves match the base at 1/2/4/8/auto; checkpoint continuation matches uninterrupted execution; historical v121 and runtime/Castor/Numbi saves continue identically.
- Periodic gradient parity: exact traces at delays 1/3/8 across shared pool sizes, plus eight-phase pending-work save continuation and invalid-option rejection.
- Platform CLI fixture parsing: 13 passed using existing dependencies and vitest 5.0.3. The temporary dependency symlink was removed.
- Parser standalone check, Python syntax checks, browser test JavaScript syntax and `git diff --check`: passed.
- Changed Python child-execution cleanup test: passed. The broader resource-refactor campaign suite is Linux-only on this host (`os.sched_getaffinity` unavailable); its 19 errors are outside the changed execution helper.

This coverage targets local scheduling, zero-worker fallback, immutable inputs/private outputs, owner publication, lifecycle/reload, historical saves, replay/network acceptance and simulation identity. The simulation revision and save formats are unchanged; the base's committed golden match trace still matches.

## Short-window timing

Two retained workloads, one warm-up plus five measured repetitions each, sequential child processes with rotated/reversed order. Traces and saves were verified separately. No builds or engine correctness suites ran concurrently with timing. Process wall/CPU/RSS are distinct measurements; startup and save loading remain included. Background desktop activity was present. These measurements do not establish full-match or cross-platform performance.

| Workload | Variant | Wall seconds | Process CPU seconds | Peak RSS MiB |
| --- | --- | ---: | ---: | ---: |
| mixed-ai | baseline | 0.4111 | 0.4067 | 70.47 |
| mixed-ai | compute-1 | 0.4114 | 0.4070 | 71.16 |
| mixed-ai | baseline-2 | 0.3721 | 0.4126 | 70.05 |
| mixed-ai | compute-2 | 0.3716 | 0.4121 | 70.58 |
| mixed-ai | baseline-4 | 0.3293 | 0.4312 | 71.28 |
| mixed-ai | compute-4 | 0.3288 | 0.4293 | 71.94 |
| mixed-ai | baseline-8 | 0.3385 | 0.5054 | 72.55 |
| mixed-ai | compute-8 | 0.3364 | 0.5028 | 73.38 |
| mixed-ai | compute-auto | 0.3379 | 0.5057 | 74.09 |
| runtime-ai | baseline | 0.3266 | 0.3227 | 60.25 |
| runtime-ai | compute-1 | 0.3259 | 0.3219 | 60.77 |
| runtime-ai | baseline-2 | 0.2969 | 0.3280 | 60.19 |
| runtime-ai | compute-2 | 0.2966 | 0.3279 | 60.56 |
| runtime-ai | baseline-4 | 0.2746 | 0.3428 | 60.77 |
| runtime-ai | compute-4 | 0.2727 | 0.3398 | 61.16 |
| runtime-ai | baseline-8 | 0.2883 | 0.4170 | 62.16 |
| runtime-ai | compute-8 | 0.2895 | 0.4242 | 62.41 |
| runtime-ai | compute-auto | 0.2879 | 0.4148 | 62.41 |

At matching explicit counts, medians are roughly unchanged (the largest CPU ratio here is about 1.02). Auto resolves to eight participants on this host. Compared with four participants, it uses approximately 18–22% more CPU and is approximately 3–6% slower in these short windows. This is a measurement of these inputs, not an automatic-sizing speedup claim or a reason to change the requested CPU-count default.

## Limits

Only native macOS arm64 execution was available. Linux, Windows, Android, WebAssembly execution and ThreadSanitizer were not run. Five display cases and interactive playtesting remain unverified. The full long-match corpus and the exhaustive building-depth/delay sweep were omitted because solver/depth rules, deadlines and publication policies are unchanged; focused building pipeline and in-flight save/lifecycle checks passed.
