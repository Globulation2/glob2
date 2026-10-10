# Shared adaptive gradient controller — first milestone

Source: `9f3c92c834c17f4f15f09a3f0c65edb2997cefd7`. Integrated base: `69ef3e1b2bf453aa280a69be72f83167b1511d9d`. PR: https://github.com/Globulation2/glob2/pull/1045. This report covers established-plan execution and passive accounting only; stages 4–6 are not implemented.

**Not qualified for default enablement; accounting remains opt-in.** There is no learned selection, live comparison, feature scan, alternative CPU algorithm, retained probe input or probe dispatch. Unknown automatic categories use CPU. Explicit OpenCL selects the validated Frozen8 plan only on eligible workers after asynchronous compilation succeeds. Required work never starts a calibration or kernel tournament.

## Implementation and bounds

- Shared family/batch decisions identify complete plans and carry versions and runtime generations. Semantic eligibility excludes owners and resumable searches before policy lookup. GPU failure preserves original seeds for CPU recovery.
- `--compute-threads=8` remains seven workers plus the owner. Worker-only processing uses that pool, never owner fallback or the designated presentation worker. Required jobs and presentation take precedence.
- Each worker samples one in 32 requests into a 32-entry ring. Slots above 31 are not sampled. Full rings drop observations; inputs are not retained. One processing pass consumes at most eight scalar observations. The profile table is fixed-size.
- The final implementation adds no observation-triggered pool wakeups. Processing uses returning workers and ordinary scheduling opportunities. Idle tail samples may remain unprocessed and are discarded during lifecycle cleanup. Required work has no dependency on their completion.
- Configuration invalidates old observations and preserves outstanding established-plan preparation. Map replacement installs a separate policy. Plan retirement and backend failure invalidate applicable observations.
- GPU compilation runs once on the worker-only path. CPU execution remains available while it runs. Driver compilation is nonpreemptible and separately timed; ordinary executor teardown still joins its threads. This is not a claim of bounded driver-initialization latency.
- Queue delay, selected-plan execution/service time and fixed-deadline publication waits are distinct. Upload time overlaps host preparation; dispatch time includes host checks. Summed field time is not game latency or process CPU.

## Predeclared measurement protocol

`predeclared-protocol.json` records the gates before final-revision measurements: three adjacent accounting-off/on pairs per fixed CPU/GPU plan and map; rotated/reversed order; 5,000 ticks; 1,000 warmup ticks; eight compute slots throughout. No timing outliers were trimmed. The eight fixtures are reused accounting controls, **not unseen holdouts**. All initial/final checksums and save bytes must match the previous validated CPU revision.

Cold startup can run CPU while the GPU compiles, so it is not automatically a fixed-GPU comparison. The separately predeclared warm supplement uses the existing 1,000-tick boundary, records readiness there without waiting, and measures through final required-work draining. Unready warm GPU pairs are unqualified. `run_ns` still reports the entire run; the original whole-run CPU gate is retained. Process peak RSS and whole-process CPU also include startup/saving and are not pure warm-window costs.

Gates: large-map geometric-mean overhead ≤1%, large-case median ≤3%, other-case median ≤5%, p99 median increase ≤10%, added publication wait ≤0.1 ms/tick, retained controller allocation ≤512 KiB, peak RSS median increase ≤2 MiB, completed-pass processing elapsed time ≤0.25% of measured runtime. Large-map confidence uses three paired-pass log aggregates and one-sided 95% Student-t bounds (df=2, t=2.920); these two one-sided bounds are not a conventional two-sided 95% interval. Other gates are descriptive medians/ranges with only three pairs.

## Final results

96 candidate observations plus eight old-revision CPU references completed. Every result was exact. 44 of 48 pairs qualify for warm fixed-plan comparison; all excluded observations and their readiness states are retained in `analysis.json`. There were no reserved-core overlap exclusions. Negative percentages are observed reductions, not evidence that accounting accelerates the algorithm.

Large-map estimates:

| Scope | Geometric mean | One-sided 95% lower / upper | 1% gate |
|---|---:|---:|---|
| cpu | +0.52% | -4.56% / +5.87% | inconclusive |
| opencl | -0.72% | -3.21% / +1.84% | inconclusive |
| whole-cpu | +0.78% | -4.48% / +6.34% | inconclusive |

Every case below reports the median and full pair range. GPU cases with fewer than three qualified pairs cannot establish the intended repetition-level result.

| Map | Fixed plan | Qualified pairs | Warm runtime | Tick p99 | Whole-process peak RSS difference |
|---|---|---:|---:|---:|---:|
| central-chokes (512²) | cpu | 3/3 | +1.55% [-1.01%, +5.66%] | -3.24% [-4.59%, +2.35%] | -3.95 MiB [-5.57 MiB, -3.35 MiB] |
| central-chokes (512²) | opencl | 3/3 | +0.63% [-3.56%, +0.69%] | -3.47% [-6.23%, -1.84%] | -7.77 MiB [-39.52 MiB, +6.82 MiB] |
| maze-routes (256²) | cpu | 3/3 | +3.35% [+2.87%, +9.70%] | +16.68% [-1.18%, +16.78%] | -3.18 MiB [-3.38 MiB, -0.89 MiB] |
| maze-routes (256²) | opencl | 3/3 | +3.48% [+1.36%, +7.92%] | -1.91% [-2.34%, +12.50%] | +3.96 MiB [+0.88 MiB, +5.35 MiB] |
| mountain-passes (512²) | cpu | 3/3 | -1.99% [-4.55%, -0.12%] | -8.23% [-8.53%, -6.72%] | +20.80 MiB [-4.90 MiB, +29.34 MiB] |
| mountain-passes (512²) | opencl | 3/3 | -1.02% [-3.11%, +1.28%] | +1.62% [-11.16%, +7.34%] | -8.98 MiB [-11.11 MiB, +10.33 MiB] |
| ocean-islands (256²) | cpu | 3/3 | +3.89% [+1.57%, +5.77%] | -0.12% [-3.56%, +4.37%] | -0.85 MiB [-3.39 MiB, +2.87 MiB] |
| ocean-islands (256²) | opencl | 3/3 | -0.71% [-7.89%, +0.78%] | -3.62% [-10.98%, -3.32%] | -1.50 MiB [-11.48 MiB, +1.82 MiB] |
| open-small (128²) | cpu | 3/3 | -2.57% [-14.07%, -1.41%] | -19.82% [-33.70%, -12.55%] | +0.24 MiB [-0.25 MiB, +0.40 MiB] |
| open-small (128²) | opencl | 0/3 | unqualified | unqualified | unqualified |
| river-network (512²) | cpu | 3/3 | -0.31% [-2.64%, +8.78%] | -2.04% [-3.08%, +4.70%] | -0.98 MiB [-27.86 MiB, +6.34 MiB] |
| river-network (512²) | opencl | 3/3 | -0.61% [-1.26%, +0.58%] | -3.35% [-3.38%, -1.05%] | -5.12 MiB [-7.11 MiB, +14.13 MiB] |
| urban-canals (256²) | cpu | 3/3 | +0.76% [-2.31%, +4.64%] | -1.05% [-6.21%, +5.54%] | +0.12 MiB [-2.60 MiB, +0.42 MiB] |
| urban-canals (256²) | opencl | 3/3 | +1.74% [-1.63%, +3.61%] | +3.82% [-12.90%, +26.12%] | +0.24 MiB [-6.58 MiB, +3.17 MiB] |
| wet-small (128²) | cpu | 3/3 | -0.31% [-2.55%, +5.63%] | -4.97% [-8.33%, +2.67%] | +0.26 MiB [+0.00 MiB, +1.74 MiB] |
| wet-small (128²) | opencl | 2/3 | -0.50% [-2.58%, +1.58%] | -2.08% [-8.61%, +4.44%] | +1.33 MiB [-0.27 MiB, +2.93 MiB] |

Worst qualified individual warm-runtime slowdown: +9.70%. Maximum completed-pass processing elapsed fraction: 0.0256%. Reported object/profile storage per active policy (`sizeof` payload): 203,824 bytes; allocator overhead is represented only in process RSS. No tuning inputs or GPU probe dispatches were retained/submitted. Publication waits, CPU time, pending/dropped observations, GPU preparation/transfers/readback, initialization and every raw timing are retained in the results rather than summed into a synthetic latency score.

Descriptive gate exceedances:

- maze-routes / cpu: p99 median exceeds the predeclared limit
- maze-routes / opencl: peak RSS median exceeds the predeclared limit
- mountain-passes / cpu: peak RSS median exceeds the predeclared limit

The numerical limits were not relaxed after seeing results. Qualification also requires sufficient readiness coverage and the predeclared aggregate confidence bound. The implementation therefore retains conservative established plans and does not advance to learned selection or live exploration.

## Superseded accounting implementation

The complete first cohort is retained in `accounting-with-wakeups/`. Its large-map warm estimates were +0.74% for CPU and +2.65% for GPU, with inconclusive 1% confidence gates and individual runtime/RSS exceedances. It did not qualify. The final simplification removes observation-triggered and post-processing pool wakeups; no kernel, plan assignment, sampling parameter, workload or acceptance threshold was tuned. The before/after cohorts do not prove wakeups caused all observed variation. Earlier interrupted/source-integration attempts are separately retained and are not pooled with final timings.

## Correctness and integration

- 929 release unit cases passed; 20 display cases were intentionally skipped. One unchanged image-loading test fails: `ImageAssets/16-bit RGBA rounds normalized channels to the exporter reference`. It also fails in isolation with the selected SDL image dependencies. The full suite is **not green**. The earlier unoptimized farming CPU-time failure passes in release.
- All 43 targeted controller, kernel, executor, pipeline and read-only-phase unit cases passed, including every compiled GPU plan against an independent oracle, original-seed recovery, owner exclusion, required/presentation priority, stale observations and pending preparation across reconfiguration.
- 171 relevant release engine cases passed; one display-only measurement screenshot case was skipped. Coverage includes historical saves, current binary/text continuation, resumable gradients, replay acceptance, shared AI/resource scheduling and fixed publication behavior.
- 84 CLI comparisons passed at 128²/256² with CPU/OpenCL/automatic modes and 1/4/8 slots: per-tick checksums, replay bytes, save bytes and cross-backend continuation match the preceding validated revision. Performance measurements always use eight slots. The 512² performance cases additionally match initial/final checksums and save bytes.
- All 15 CLI smoke tests passed. Documentation checks passed.
- ThreadSanitizer: 480,000 concurrent observations, ring overflow, plan retirement and reconfiguration passed. The stress harness explicitly wakes an eligible test worker to drain final observations; production does not wait for this optional drain.
- The embedded OpenCL kernel source is byte-identical to the previously validated optimized kernel. Existing isolated-kernel results are historical evidence, not a new whole-game speed claim.

`source-manifest.json`, `native-validation.json`, `validation-summary.json`, JUnit files and exact-run manifests identify commands, source hashes, binaries, compiler/dependency prefixes and results. Release uses GCC 15.2, `release=1`, `-O3`, eight build jobs, no fast/PCH/unity runtime comparison. Current master was fetched again: its newer platform CLI adapter/macOS packaging changes merge cleanly and do not affect these Linux runtime inputs; see `final-base-audit.json`. No unrelated merge/rebase was performed.

## Limits and reproduction

Only Linux x86-64 / NVIDIA RTX 2070 SUPER / driver 580.178.04 was exercised. Windows, macOS, other GPU vendors, mobile/browser execution and cross-platform per-tick equivalence were not verified. There is no controlled rendered-game contention study or full early/middle/late-game qualification. The two GPUs share desktop use; a cooperative lock does not establish exclusive GPU ownership. Hardware performance-counter access was unavailable, so there is no direct cache-miss or memory-bandwidth measurement claim. Whole-process CPU/RSS and end-to-end timing are interference indicators, not a complete causal decomposition.

Exact commands and all fixtures/hashes are in the archived scripts and manifests. CPU affinity is reserved for timed work and restored afterward; receipts are included. Build artifacts/executables and disposable profiles are excluded from the archive. Previous kernel and PR evidence: https://github.com/Globulation2/glob2/pull/1045#issuecomment-6092851189.
