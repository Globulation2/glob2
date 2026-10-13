# General executor wakeup qualification

Final clean source: `4a335c7837a9fa0f5cde39bfdcb86748031955d5`. Scheduler change: `cef305e34`; separate growth-test repair: `4a335c783`. Before baseline: `7588574389595d6fd51efeac293e55afdfc68ac5`. Master fetched at `277510840054b1416f2defd7d86fe1431dd0ddc0`; newer merge execution is not claimed. No simulation rules, save bytes, minimum save version, replay gates or sim revision changed. All tested per-tick checksums remain identical.

## Change and progress argument

This is an executor policy for every deferred producer, not a team-statistics special case.

- A deferred submission counts its newly runnable jobs under the existing scheduler mutex. A no-lane group contributes its job count. An idle ordered lane contributes one head; successors queued behind issued work contribute zero. Repeated groups on one lane see the prior group's issued sequence and do not count duplicate heads.
- Zero newly runnable jobs need no submission wakeup. One wakes a worker. Multiple runnable jobs retain the existing broadcast to preserve parallelism.
- A worker returning from its callback checks the shared queue immediately. Advancing its lane or returning an early-resolved continuation makes at most one new head runnable, so it consumes that work without broadcasting. External completion/resumption has no returning worker and still wakes one.
- Deferred readiness is worker-independent: every worker can claim a ready lane head or no-lane job. To make notify_one safe, presentation owner/trace-drain waiters use a separate graphicsReady condition variable. Worker notifications cannot be consumed by an owner with a different predicate. Scene completion, graphics slot release and graphics cancellation/completion notify that separate barrier as required.
- Blocking run generations, presentation admission/capacity transitions, maintenance registration, barriers and shutdown retain their existing broadcasts. This deliberately conservative policy does not optimize all multiple-job wakeups.

The predicate and queue updates remain under the existing mutex, and waiters recheck their predicates. A queued lane head progresses through either a returning worker or an external-resolution notification. No readiness counters, waiter registries, timing clocks, pools or extra normal-execution metrics were added. The extra condition variable separates waiter roles.

## Correctness evidence

- Native macOS ARM64 release standalone executor/read-phase suite: 47 cases, 138108 assertions passed. Twenty full repetitions also passed.
- Normal repository development build and registry runner: all 47 executor/read-phase cases passed, JUnit in dev-tests.xml.
- Linux ARM64 GCC 11.4 release standalone same source harness: 47 cases, 138108 assertions passed.
- macOS ThreadSanitizer: 46 cases/138103 assertions passed, no race diagnostics. The existing huge trace-allocation failure test aborts in the sanitizer allocator even with allocator_may_return_null=1; it passes in ordinary native/Linux runs and was excluded from the supported sanitizer run. Failure logs are retained.
- Supplemental native compile with Emscripten threaded policy macros exercises serial graphics-owner waits. Before and after both pass 46 cases after excluding the same existing test that assumes native graphics-worker ownership. Before and after both fail that assertion identically (74 assertions). This is policy-path emulation, not an actual browser/Wasm verification.
- Initial expanded engine run: 158 passed, 2 failed, 1 display skip. One failure was a conservation fixture comparing current world stock with deliberately delayed published statistics. Its live-ledger correction is in the separate 4a335c783 test-only commit; all 22 resource-growth cases pass after correction, including historical saves and zero/shared-worker continuation.
- Final clean release engine qualification: 123 passed, 0 failed, 1 display skip across AI pipeline, shared snapshots, gradient pipeline/device-service lifecycle, resource growth, team-statistics persistence and labour. See engine-final.xml and engine-final.log. Additional preparation cases passed in the expanded run, apart from the OpenCL limitation below.
- Three paired release advancing trials, 2048 ticks/configuration, 32 configurations (two/four teams, no AI or Numbi/Castor, zero/two workers, K=0/1/2/8), seed 1234567. The diagnostic benchmark uses EngineRun's Deferred stepping rather than extra Complete end-of-tick preparation.
- All 16640 before/after per-tick checksums match across 32 x 520-tick continuations. Each binary's 8320 zero/two-worker pairs match. Final clean rebuilt binary was traced again and matches every original record.

## Existing OpenCL limitation

GradientPreparation/modified terrain costs remain exact when field obstacles change reports no GPU commit for its first modified-cost field. Diagnostic status is `OpenCL gradient failed to converge`, with zero budget declines/quarantines/cancellations; exact CPU recovery matches the scalar field. Restoring submission broadcasts in a diagnostic header (same class layout, logged OLD_WAKE_POLICY broadcasts) reproduces the same convergence error and assertion. The diagnostic source/object substitution is archived; it is a controlled wake-policy comparison, not a full build of an older commit. No GPU algorithm or assertion was weakened in this scheduler change. This backend execution-path test remains a recorded failure.

## Release performance

Five paired, randomized trials after the local release build finished. Workload: eight-batch horizon, 5000 batches/trial, per-job synthetic integer work, workers 0/1/2/4, single ordered job, single independent job, 16-job ordered group, 16 independent jobs, and mixed ordered/independent groups; both continuous and paced submission. Every expected job completes. A first set taken during build load is retained separately as loaded evidence and is not used below. Other host load remains uncontrolled; these are measurements, not universal speedups.

Representative medians (us/batch):

| Two background workers, continuous queue | Before | After |
| --- | ---: | ---: |
| Single ordered job | 3.431 | 2.305 |
| 16 jobs on one ordered lane | 34.981 | 24.195 |
| 16 independent jobs | 20.731 | 20.079 |
| Mixed lane/independent groups | 26.436 | 26.458 |

Four-worker continuous mixed groups: 29.541 -> 29.975 us/batch. Zero-worker single ordered job: 1.061 -> 1.037. Wide/mixed throughput is broadly unchanged within the observed variation; do not generalize the ordered-chain gain to all work.

Instrumented paced single-lane submission, medians (us/submission; added clocks perturb these costs):

| Workers | Before lock/body/unlock+wake | After lock/body/unlock+wake |
| --- | --- | --- |
| 1 | 0.020 / 0.024 / 0.788 | 0.020 / 0.023 / 0.618 |
| 2 | 0.019 / 0.023 / 0.750 | 0.020 / 0.022 / 0.448 |
| 4 | 0.026 / 0.024 / 1.662 | 0.020 / 0.025 / 0.774 |

After includes blocked-lane submissions which intentionally emit no wakeup. Most savings concern synchronization; slot bookkeeping stays small.

Advancing-match medians vary in both directions. Two-team AI K8 owner total: 208.849 -> 203.890 us/tick; four-team AI: 300.919 -> 306.554. Submission remains small, around 0.14–0.20 us/tick with active workers. Unchanged capture and worker-calculation timings also move with host load. No overall match speedup or no-regression percentage is claimed from these trials. Full phase costs are in advancing-medians.json.

Callgrind 3.18.1/GCC 11.4 Linux ARM64, zero-background-worker probes with empty callbacks isolate instruction costs from scheduling noise. Single lane: instructions -0.74%, conditional branches +0.41%, indirect branches -12.42%. Mixed groups: instructions -0.96%, conditional branches +0.10%, indirect branches -11.84%. Runnable-width classification adds conditional work; removing completion broadcasts reduces indirect calls. These serial probes do not quantify sleeping-worker kernel/scheduler costs. Simulated cache dimensions are in raw files and are not Apple hardware cache measurements.

## Commands and scope

Ordinary release production rebuild through tools/dev_build.py dev_fast=0 linker=default release=1; exact requested targets/link commands in release-build-args.json and release-link-args.json. Extra engine fixture objects compile with existing registry flags, including -fno-access-control for preparation/growth. Sources, diagnostic modifications, release build logs and final clean binary hashes are archived. The deferred fixture differs only in using syncStep(..., Deferred) plus consumeClientEvents instead of HeadlessGame::step.

Native harness: `clang++ -std=c++20 -O3 -g -pthread -Isrc/common -Ilibgag/include -Itest/support -I/opt/homebrew/include artifacts/scheduler-wakeups/main.cpp src/common/ComputeExecutorHarness.cpp -o <binary>`; `<binary> --test-suite=ComputeExecutor,ReadOnlyPhase`.

Sanitizer uses -O1 -fsanitize=thread and `--test-case-exclude='trace storage allocation failure*'`. Supplemental policy compile adds -D__EMSCRIPTEN__ -D__EMSCRIPTEN_PTHREADS__ and excludes the documented native-worker ownership assertion for both controls.

build-bench.py, run-bench.py and profile-final.py and raw logs contain native performance orchestration. Linux benchmark uses g++ -std=c++20 -O3 -g -pthread -I/work -DEXHEADER='"before.h"' or '"after.h"'; `valgrind --tool=callgrind --branch-sim=yes --cache-sim=yes --callgrind-out-file=<file> <binary> 1 <shape> 0 20000 0`, shapes 0 single ordered lane / 4 mixed groups.

Final engine registry command: `python3 test/run_tests.py --build-dir artifacts/scheduler-wakeups --binary engine --filter 'GradientPipeline/*' --filter 'ResourceGrowth/*' --filter 'TeamStatsPipeline/*' --filter 'TeamStatsSave/*' --filter 'AIPipeline/*' --filter 'WorldSnapshot/*' --filter 'TeamLabourStats/*' --no-display --junit artifacts/scheduler-wakeups/engine-final.xml`.

Actual Windows/browser/mobile/full Linux engine and supported-platform cross-engine per-tick comparisons were not run. Linux coverage here is the real executor harness; macOS covers the integrated engine. The PR remains draft. No runtime/development dependency changes were needed.
