# PR 1194: release performance against current master

Compared master `64c792a61c4b5a7cdbfc49f33e7c846127645056` with rebased PR `c48b34539d56948d91decb197c27d76b3f4ec65e` (master is its direct parent). The rebase preserves master snapshot checksums/protocol 72, uses save/replay format 169 for delayed cold fields, and sim revision 70. The 64-request building overflow repair is retained. Historical save floor remains 58.

## Findings

Every measured workload improved at both participant counts. With 8 participants, median simulation time decreased 2.8–27.6%; p99 tick latency decreased 9.8–68.0%. Across these six equally weighted workloads, the geometric mean changes are **−14.9% simulation time, −9.0% measured simulation CPU, −45.9% p99 tick time, +0.3% peak RSS**. With 1 participant, geometric mean simulation time decreased 10.8%. These are measured time reductions, not percentages of throughput increase.

This is an end-to-end AI-game comparison, not a fixed-work kernel benchmark. Routing latency changes future decisions and populations. Oazis ends with 254 units on master versus 218 on PR; Playground12k ends with 224 versus 240, and24k with 222 versus 226. The Playground improvements occur while the PR ends with more units, but these measurements do not isolate each cause. No play-feel acceptance is asserted.

## Eight compute participants

All times are medians of five measured repetitions. Lower is better. Simulation time excludes startup/load/output setup, includes the tick loop and final compute/hiring drain, and includes all ticks in the run window. CPU excludes the configured 128 warmup ticks for loaded games; fresh games use zero warmup ticks. Peak RSS covers the whole process, including startup.

| Workload | Master simulation s | PR simulation s | Change | Master p99 ms | PR p99 ms | CPU change | Peak RSS master/PR MiB | End units master/PR |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| SmallForTwo · 2 players | 0.2066 | 0.2008 | -2.8% | 0.061 | 0.055 | -5.7% | 60.3 / 60.6 | 87 / 89 |
| FourSquares1 · 4 mixed AIs | 0.3592 | 0.3465 | -3.5% | 0.135 | 0.113 | -4.9% | 75.2 / 76.1 | 63 / 70 |
| Archipelago · 5 mixed AIs | 0.5186 | 0.4362 | -15.9% | 0.476 | 0.173 | -13.7% | 79.8 / 79.2 | 54 / 51 |
| Oazis · 11 mixed AIs | 1.6356 | 1.4487 | -11.4% | 1.682 | 1.403 | -5.9% | 179.2 / 179.3 | 254 / 218 |
| Playground · tick 12,000 | 0.5201 | 0.3899 | -25.0% | 0.710 | 0.245 | -11.3% | 116.5 / 116.5 | 224 / 240 |
| Playground · tick 24,000 | 0.5548 | 0.4019 | -27.6% | 0.847 | 0.271 | -12.2% | 132.5 / 133.0 | 222 / 226 |

## One compute participant

| Workload | Master simulation s | PR simulation s | Change | p99 change |
|---|---:|---:|---:|---:|
| SmallForTwo · 2 players | 0.3605 | 0.3359 | -6.8% | -2.6% |
| FourSquares1 · 4 mixed AIs | 0.8415 | 0.8013 | -4.8% | -3.0% |
| Archipelago · 5 mixed AIs | 1.3118 | 1.0901 | -16.9% | -38.5% |
| Oazis · 11 mixed AIs | 5.0369 | 4.6321 | -8.0% | -2.9% |
| Playground · tick 12,000 | 1.2492 | 1.0926 | -12.5% | -22.6% |
| Playground · tick 24,000 | 1.3870 | 1.1792 | -15.0% | -25.8% |

## Workloads and method

- Four new games at seed 11942026: SmallForTwo 64×64 / 2 Nicowar; FourSquares1 128×128 /Castor, Nicowar, Warrush, Numbi; Archipelago 128×128 /Nicowar, Castor, Warrush, Numbi, Nicowar; Oazis 256×256 / 11 mixed Nicowar, Castor, Warrush and Numbi controllers. Each runs 8192 ticks.
- Two common format 164 Playground 128×128 / 8 Nicowar saved checkpoints: ticks 12000→16096 and 24000→28096, with 128 warmup ticks excluded from CPU/tick-quantile counters. The same input bytes are loaded in both versions. Input SHA256 and full player lists are in `manifest.json`.
- macOS 26.6.2 ARM64, Apple M3 / 8 physical cores / 24 GiB RAM; Apple Clang 21, Python 3.14.7, existing development dependency prefixes. Ordinary matching `scons release=1 -j4` clients, default release feature profile, no fast/PCH/unity build. Both use CPU routing. Generated BuildConfig headers are identical after normalizing their checkout paths; full build logs and binary hashes are included.
- One process at a time, participants 1 and 8 (zero/seven workers verified). Per workload, one warmup run per variant is excluded, followed by five measured runs. Variant order rotates and reverses each round. Total 144 runs: 24 warmups + 120 measurements. No compiler/tests/profilers run alongside timed processes. There was a 45-second cooldown after verification; no thermal/performance warning was reported. Normal desktop background activity remained.
- Timing uses `--benchmark-warmup`, compute telemetry off and no checksum/replay/save export. `run_ns` measures simulation; process CPU and RSS come from wait4; measured simulation CPU comes from the engine process CPU clock; per-tick p95/p99 are the engine's exported quantiles. Wall/startup, p50/p95/p99, coarse worst-tick buckets, CPU, RSS, tick counts, populations and logical counters are retained. RSS is whole-process peak, not simulation-only allocation. Histograms have power-of-two bins; no precise worst-tick maximum is inferred.
- `timing/measurements.jsonl` contains every raw sample, command and full result; each run also retains its engine log/result. `summary.json` preserves all five simulation samples plus paired-ratio bootstrap intervals. Five samples on a shared desktop are a limited estimate of variability; no universal performance bound is claimed.
- The short pilot and full-window preflight ran while validation compilation was active and are explicitly excluded from timing results. Both versions reached every selected cap with matching seed, game rules, controller lists and requested/actual participant sizing.

## Correctness and rebase validation

- All 24 release verification runs completed. For each workload and each revision, 1 and 8 participants match **all three per-tick streams**: replay component, snapshot, and full-world checksums. No equality is expected between master and PR trajectories. Timing runs also require repeated final checksums and tick outcomes to agree within each revision across participant counts.
- Verification source was the source-identical pre-fixture-amend head 6262c72b6; the final release binary hash is unchanged after fixture regeneration/amendment. Timed runs and final native tests use final commit c48b34539.
- Native focused verification: 156 engine cases passed, 0 failed, 1 display-only market-panel skip; 38 selected unit cases passed. Includes building overflow FIFO/save regression, cold publication, historical runtime formats 164–168, save/load continuity, snapshot checksums, hiring, markets, guard areas, replay/version boundaries, script compatibility, save safety and refreshed goldens. Python sim-contract tests: 9 passed. Golden regeneration: 8 tagged cases plus the rally fixture, all passed. Sim-revision contract against master and whitespace checks passed.
- No new Windows/Linux/browser/mobile execution, release packaging or gameplay-feel review. Results cover headless CPU simulation on this M3, not rendered frame rates or other hardware. Broader acceptance remains open; PR stays draft.

## Reproduction

Build each pinned revision in its own checkout with `PYTHONPATH=. scons release=1 -j4`. Focused validation used `PYTHONPATH=. python3 tools/dev_build.py engine-tests unit-tests -j6`. Logs retain compiler flags/dependency prefixes. `campaign.py` reuses `test/benchmark_parallel_compute.py`'s process runner; manifest records exact map/save paths and hashes. Replace the two checkout/binary paths below when reproducing elsewhere.

```sh
python3 artifacts/pr1194-performance/campaign.py --mode timing \
  --master /Users/bradley/.codex/worktrees/pr1194-perf-base/glob2/build/darwin/client/release/src/glob2 \
  --pr build/darwin/client/release/src/glob2 \
  --master-cwd /Users/bradley/.codex/worktrees/pr1194-perf-base/glob2 \
  --output artifacts/pr1194-performance/timing
```

Use `--mode verify` with a new output directory for per-tick comparisons. The source-canonical input fixtures are included under `inputs/`; relocate the absolute paths in `manifest.json` for another machine without changing hashes. Exact focused test/build commands are in `commands.json` and JUnit records identify the selected cases.

## Evidence layout and superseded attempts

`final-build-provenance.json`, `host.json`, and mode metadata pin revisions, binaries, configuration and inputs. `verify-all/measurements.jsonl` records exact digests of all three raw checksum streams. The portable archive includes losslessly compressed snapshot/full-world per-tick traces. The very large replay component breakdown streams are retained locally and omitted from the portable archive; their original SHA256 comparisons remain in the verification JSON. The included full-world/snapshot traces provide accessible per-tick evidence. Build binaries are not included; rebuild from the pinned commits.

The first verification helper expected one sidecar; master now emits three. That helper attempt stopped on this harness assumption, not an engine mismatch; `verify.log` retains it, and `verify-all` is the complete passing rerun. The initial pre-rebase release build and release-test build were stopped when the requested rebase/build plan changed; only completed final build logs support results. No timing from interrupted builds or correctness runs is included.
