# PR1176 final extraction evidence

Final source: `8925fdbadef4c91563c75ac99a817b376852b19f`; base: `fea49d4fd278fa920df66571c02de1333678a740`. The first Linux performance campaign tested `5bb958e735d5e1edfb3e522aa95b115f9f0f8fec`; its affinity-limited auto policy was subsequently removed. Do not attribute that policy or its measurements to the final head.

## Linux policy decision

Ordinary GCC13.4 release builds (`release=1 pch=0 unity=0`), same SDL prefix and frozen advancing AI saves. Threadripper2950X; six reserved logical CPUs12–14,28–30 (three complete SMT cores). Each cell has four alternating measured pairs and one discarded fresh-process warmup pair. Engine window20000–24096, CPU gradients, team-timeline telemetry and replay writing. Fixed3 means both binaries use3 participants; original auto means baseline32 versus candidate6.

| Scenario | Engine CPU change | Engine wall change | Peak RSS change |
|---|---:|---:|---:|
| playground-164298751-compute-3 | -0.62% | -1.71% | -22.52% |
| playground-164298751-compute-auto | +10.82% | +4.73% | -29.08% |
| playground-902381047-compute-3 | -2.48% | -2.79% | -23.68% |
| playground-902381047-compute-auto | +10.85% | +5.75% | -30.52% |
| oazis-164298751-compute-3 | -0.30% | -0.25% | -10.55% |
| oazis-164298751-compute-auto | +9.58% | -0.18% | -17.75% |
| oazis-902381047-compute-3 | -0.50% | -0.40% | -11.04% |
| oazis-902381047-compute-auto | +6.08% | -1.91% | -18.23% |

Percentages are paired geometric ratios; confidence intervals and medians are in `linux-original-summary.json`. CPU and wall intervals have different startup/teardown boundaries, documented in raw metadata. The mixed-count aggregate is not a retained-change speedup claim.

Keep compact telemetry: fixed3 reduced peak RSS by about36–48MB, with small CPU savings. Drop affinity-limited automatic sizing: +6.1–10.8% CPU, Playground +4.7–5.8% wall; Oazis wall change ranged from inconclusive −0.2% to −1.9%. The user selected existing hardware auto plus explicit budgets. No universal best participant count is inferred.

## Native verification

- Final Linux head:23 engine cases and80 unit cases passed. Baseline/final counts1/3/6 produce the same842-line golden trace;12 affinity cases validate hardware auto and explicit1/3/6.
- Final macOS arm64:23 engine cases passed;78/80 unit cases passed. The two failures (OpenCL convergence and weak-control credit accounting) reproduce on clean master441870ef1. Relevant OpenCL files are unchanged between that baseline andfea49d4fd. Successful macOS GPU execution is not established.
- Linux dedicated OpenCL acquisition, injected-wait failure/fallback and participant oracle checks passed at5bb958e73. These OpenCL/explicit-count paths are unchanged in8925fdbad.
- Python tournament suites:75/75 passed; real Linux engine allocation smoke:four games, bounded shares and unchanged parent affinity.
- Standalone resource policy:6cases/73assertions each, native and two browser-policy macro builds. These are not actual browser runtime tests.
- No actual Windows/Android/iOS/browser qualification. No full hosted CI claim; cheap checks passed. Simulation feel evaluation remains maintainer-owned.

## Contents and reproduction

- `linux-evidence.tar.gz`: all raw paired measurement rows, per-run result JSON, complete input audit, selected complete engine logs, build/source metadata, JUnit, checksums and allocation receipts. The internal index identifies deliberately omitted repeated logs and duplicated tournament bundle data.
- `linux-inputs.tar.gz`: four frozen final saves; paths andSHA256 values appear in the original manifest. Relocate paths in both args andfixture_sha256 when reproducing.
- `macos-evidence.tar.gz`: final native checks, baseline-failure diagnosis, policy probes, commands and review record.

Extract both Linux archives into one directory. The source-controlled runner is `test/benchmark_resource_refactor.py`; the manifest, each recorded child command, and input hashes are retained. The cpuset audit confirms restoration. The final user instruction moved all subsequent computation to localhost; no further timing was run on therig.local.

## Final-head local release comparison

Both binaries were built sequentially in the same owned build directory, copied to distinct paths, and recorded before restoring the baseline source checkout. This keeps compiler flags, dependency paths and shader-directory defines identical; ordinary `scons release=1 pch=0 unity=0` only. Apple M3/8 logical CPUs, macOS arm64, matching AppleClang and direct-library hashes. macOS native correctness checks separately used dev_fast; no dev-fast binary was timed.

Six cells, four alternating measured pairs plus one discarded warmup pair each; same frozen Linux-generated saves and tick windows. Fixed3 uses3 participants on both binaries; auto uses8 on both. No compilers were active. This is an unreserved desktop: host-interference and effective-frequency measurements are unavailable, so no isolated-core or cross-host aggregate claim is made.

| Scenario | Engine CPU change | Engine wall change | Peak RSS change |
|---|---:|---:|---:|
| playground-164298751-compute-auto | -0.97% | -1.56% | -12.80% |
| playground-902381047-compute-3 | +0.06% | -0.30% | -15.69% |
| playground-902381047-compute-auto | +1.10% | -1.60% | -15.94% |
| oazis-164298751-compute-auto | +0.46% | -0.53% | -8.18% |
| oazis-902381047-compute-3 | -0.25% | -0.40% | -8.84% |
| oazis-902381047-compute-auto | -0.12% | -1.01% | -8.71% |

CPU is effectively neutral overall (equal-cell paired ratio1.00044, 95% repeat-noise interval0.99610–1.00503). Observed wall reductions are0.3–1.6%; some intervals include no change. Peak RSS reductions8.2–15.9% are the clearest retained benefit. Final input audit verified; performance gate within_limits. These results support retaining compact telemetry without claiming a universal tick-rate gain.

`macos-performance.tar.gz` contains all raw rows and per-run results, audits, manifests, build receipts/library hashes, the minimally adapted runner, and representative complete engine logs. The runner adaptation only reports unavailable affinity as null on macOS; timing and input checks are unchanged. Complete frozen inputs are already in linux-inputs.tar.gz.
