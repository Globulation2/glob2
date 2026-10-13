# Worker-finalized smoothing verification

Final revision: `5363a28d9d57960933211d64c1bbcd8a022eb8b8`; fetched master: `3e2ba6c07450c94ce0f997220479fd309b71e461`.
macOS ARM64, Apple Clang 21.0.0/libc++; dev via tools/dev_build.py, release with dev_fast=0 linker=default release=1. Link inputs and compile commands are attached; dependencies unchanged.

## Behavior

Workers finish aggregate smoothing from the latest 31 published raw samples plus the current sample. Only full-aggregate submissions (every 32 source samples per team) capture the ring. K=0/1 are exact; K>1 omits K-1 pending samples and substitutes older published rows. Published raw samples stay consecutive and never contain smoothed approximations. Delayed maxima may omit or prolong a peak and change AI decisions.

Owner publication updates changed measurement fields in place, retains spatial diagnostics until their 512-tick update, accumulates labour deltas and installs finalized aggregates. It does not perform the normal 32-row reduction or copy unchanged diagnostics out and back. Histories keep source ticks and pending samples keep fixed deadlines.

Format 157 stores an explicit finalized flag on aggregate samples. Old 155/156 pending aggregates load as raw, retain exact owner reduction, and preserve that mode on current-format resave. Format-155 digests remain read-and-discarded. No old world snapshots/windows are needed to restore completed pending results. Minimum save version stays 58; replay floor 157, protocol 69, SIM_REVISION 52 with refreshed golden record and trace. New-game delay remains 8; rules still allow 0–8.

## Correctness

- Native selected suite: 136 cases, 132 passed, 4 display cases skipped, no failures.
- Release selected suite: 77 cases, 76 passed, 1 display case skipped, no failures.
- Final focused native pipeline/golden: 17 cases passed; release pipeline: 16 cases passed.
- Includes extracted/live equivalence, K=0/1/2/8 with zero/one/multiple workers, early/late completion, snapshots/observer generations, old save migrations, pending save/resave, current/old raw aggregate persistence, labour/cargo/spatial/custom catalogs, pause/reset/failure and AI continuation.
- New peak tests demonstrate exact K=1, missing recent peaks at larger K, older peaks lingering and eventual expiry without compounding.
- Golden regeneration and normal verification passed; sim-version check passed.
- Release advancing full checksums: 32 scenarios × 520 ticks = 16,640 raw per-tick checksum records. All 16 pairs match across zero/two workers (2/4 teams, K=0/1/2/8, controlled/Numbi-Castor matches). Heavy checking completes pending computation but does not publish early. Final checksums also agree across repeated profiling on/off trials and worker configurations.

Limitations: full-engine Linux/Windows/web/mobile checksum comparisons, display/visual tests, packaging, sanitizers, hosted CI and maintainer play review unavailable/not run. Keep draft. The MCP repair remains separate (#1157) and this PR remains stacked on it. No new runtime dependencies.

## Release profiling

Five paired before/after static trials at 2048 ticks, seed 1234567, 2 teams/256 static units, two workers, K=8. Before binary corresponds to 2d612a65c (SHA in revisions.json). After binary uses the new implementation. Additional advancing 2/4-team matches start with 128 units/team, stocked inns/swarms, wheat/wood/stone; no controllers or alternating Numbi/Castor, AI order delay 8. K=0/1/2/8 and zero/two workers. Exact invocations are in profile.py. Compile/test activity from this task finished before profiling; VM/other host activity remains a wall-clock limitation. Measurements are observations, not evidence of a general speedup.

Times in microseconds per advancing tick. Worker calculation sums both teams, includes only published batches; the last K unpublished jobs remain omitted. Capture is shared across consumers, not an isolated incremental statistics cost. Allocation/setup includes aggregate-window capture. Deadline waits include queue/backlog/scheduling effects and serial fallback; they must not be added to worker time as independent work.

| Static K=8, profiled | Total owner | Shared capture | Setup/window | Submission | Publication | Deadline wait | Worker calculation |
|---|---:|---:|---:|---:|---:|---:|---:|
| before | 7.153 | 4.496 | 0.066 | 1.224 | 0.998 | 0.000 | 4.217 |
| after | 7.101 | 4.519 | 0.083 | 1.285 | 0.872 | 0.000 | 4.280 |

Unprofiled static before owner median: 6.996 us.

Unprofiled static after owner median: 6.992 us.

Publication decreased about 13% in this paired static workload (1.00 → 0.87 us). Aggregate-window capture slightly increases setup and worker reduction replaces owner work, so total owner time remains essentially unchanged (~7 us). This remains an update/reducer, not a pointer-only append.

| AI matches, 2 workers | K | Total owner | Shared capture extraction | Setup/window | Submission | Publication | Deadline wait | Worker calculation |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 2 teams | 0 | 170.010 | 62.963 | 0.110 | 0.000 | 0.986 | 0.000 | 4.455 |
| 2 teams | 1 | 177.604 | 51.232 | 0.115 | 0.117 | 1.167 | 53.328 | 5.019 |
| 2 teams | 2 | 173.306 | 55.053 | 0.126 | 0.103 | 1.292 | 6.554 | 4.902 |
| 2 teams | 8 | 171.989 | 54.983 | 0.125 | 0.096 | 1.247 | 0.000 | 6.338 |
| 4 teams | 0 | 235.955 | 104.541 | 0.178 | 0.000 | 2.236 | 0.000 | 8.801 |
| 4 teams | 1 | 257.136 | 92.894 | 0.180 | 0.242 | 2.811 | 46.661 | 10.724 |
| 4 teams | 2 | 247.512 | 95.035 | 0.181 | 0.123 | 2.508 | 17.895 | 10.762 |
| 4 teams | 8 | 247.605 | 98.500 | 0.205 | 0.140 | 3.381 | 0.000 | 16.570 |

Short deadlines exposed substantial waiting with the existing shared executor: K=1 ~47–53 us/tick; K=2 ~7–18 us/tick in these AI cases, despite calculation time being much shorter. With workers enabled, deferred joins wait for completion; the queue includes other non-preemptible engine jobs. These observations do not isolate backlog versus host scheduling, and no executor behavior was changed in this follow-up. Keep default 8; smaller K remains configurable. Changing K can change AI orders/trajectories, so whole-match numbers are not causal before/after speedup estimates.
