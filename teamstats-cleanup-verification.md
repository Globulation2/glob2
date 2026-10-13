# Statistics cleanup verification

Final revision: `2d612a65c1ffd4c7bb46810158eb1a66f6271070`.
Fetched master: `3e2ba6c07450c94ce0f997220479fd309b71e461`.
Base/dependency repair: `b1f1affb1ca39682c428003250ddac574a6608ed` (#1157).
macOS 26.6 / Darwin 25.6 ARM64, Apple Clang 21.0.0, libc++, Homebrew/managed dependencies recorded in build logs and link arguments. No runtime dependency changes.

## Correctness

- Full native selected suite: 134 cases, 130 passed, 4 skipped, no failures. Includes 43 unit cases and 87 engine cases. Production code at 4348fdd4b; subsequent commits only expand benchmark evidence and update the golden record.
- Release selected suite: 75 cases, 74 passed, 1 skipped, no failures (same production code).
- Final native pipeline/golden checks at the final revision: 15 passed, no failures/skips.
- Final release advancing full-checksum verification: 16 scenarios, 520 ticks each; 8 matched per-tick trace pairs across zero/two workers (2/4 teams, K=0/8, controlled colonies/Numbi-Castor). Includes pending-sample verification and 32/512 boundaries. Raw traces are attached. Heavy verification intentionally finishes calculation early without publishing early.
- Profiling on/off and all worker configurations produced the same final checksum within each workload/delay; all repeated profiles agree. AI orders differ between K=0 and K=8 as intended.
- Save compatibility includes format-155 digest read/discard, older-format migration, current pending resave, smoothing/history, text streams/corruption and replay floor 156. Minimum save version remains 58; network protocol 69; SIM_REVISION 51. Golden record regenerated and verified; sim-version script passes.
- MCP fixture source blob is identical between the tested integration and #1157 (`83c6b19ad2b092607a568efd10c1145cc0dd9686`). Repair PR tests are validated in this integration; this is not a separate full build of the repair head.

Omissions: Linux/Windows/browser/mobile full-engine per-tick comparisons, visual/display tests, packaging, sanitizers, hosted CI and maintainer play review. Docker Callgrind probes are not full-engine platform qualification. PRs remain draft.

## Release profiling

Five repetitions per configuration, 2048 advancing ticks, seed 1234567. Static: two teams/256 fixed entities and a shared base snapshot. Advancing: two/four colonies with 128 starting units/team, inn/swarm stocks, nearby wheat/wood/stone, and either no controllers or alternating Numbi/Castor; AI order delay 8. Validated AI orders are executed normally. Exact commands: `profile.py`; native binaries and revisions: `revisions.json`.

Times below are microseconds per advancing tick. Calculation is summed job time across teams for published batches, not owner elapsed time; the last K unpublished jobs are omitted. Deadline waits include calculation executed by zero-worker fallback, so worker and wait times must not be added as independent costs. Capture covers the shared union of consumers, not only incremental statistics extraction. Setup timing includes projection, batch/sample/phase storage and profiling-row preparation. Publication timing includes applying smoothing, counters and histories; deque retirement and other boundary overhead remain in total owner elapsed time.

### Final revision, host under unrelated compiler load

The final timing refresh encountered another compiler workload. Its larger wall times and deadline waits cannot be attributed to this PR. Shared capture includes measured lazy growth preparation.

| Workload (K=8, 2 workers) | Shared capture | Allocation/setup | Submission | Publication | Deadline wait | Calculation |
|---|---:|---:|---:|---:|---:|---:|
| Static 256 units | 11.097 | 0.137 | 3.753 | 2.250 | 0.000 | 8.959 |
| 2-team Numbi/Castor | 188.148 | 0.248 | 1.235 | 6.748 | 4.097 | 35.933 |
| 4-team Numbi/Castor | 286.118 | 0.388 | 3.512 | 13.778 | 5.108 | 66.872 |

### Earlier quiet trials, production code identical

Recorded at e2290801a, before the final benchmark-only trace/preparation logging additions. These are historical measurements, not substituted final-revision validation. Capture extraction excludes lazy preparation, which was not printed in that revision.

| Workload (K=8, 2 workers) | Shared capture | Allocation/setup | Submission | Publication | Deadline wait | Calculation |
|---|---:|---:|---:|---:|---:|---:|
| Static 256 units | 4.546 | 0.071 | 1.240 | 1.001 | 0.000 | 4.361 |
| 2-team Numbi/Castor | 40.624 | 0.059 | 0.099 | 1.056 | 0.000 | 4.732 |
| 4-team Numbi/Castor | 73.389 | 0.074 | 0.127 | 2.588 | 0.000 | 11.959 |

Without profiling, the static workload measured approximately 5.02 us synchronous versus 7.13 us K=8 in quiet trials; final loaded trials measured 11.28 versus 21.79 us. Numbi/Castor whole-tick results showed no clear offloading benefit: quiet 2-team K=0/K=8 medians were 153.69/155.61 us; 4-team 208.05/212.96 us. Different publication delays can change AI orders and trajectories, so these are workload observations, not causal speedup estimates.

### Allocation ablation

`static-pre-reuse-*.log` uses the retained pre-reuse release binary with the same profiling stages. `static-profile1-*.log` uses the one-spare-batch implementation. Final loaded runs alternate both binaries in each trial; the earlier quiet setup comparison uses the initial pre-reuse trials. No additional pool is introduced: a joined published batch is moved to a single spare, then reused at the next submission. Remaining-count initialization uses a relaxed owner-only store before executor submission.

- Final loaded allocation/setup median: fresh 2.144 us; reused 0.137 us.
- Earlier quiet allocation/setup median: fresh 0.940 us; reused 0.071 us.

## Callgrind: retained executor improvement

Fresh probes use the final executor and current master, both normalized to 74 slots; source copies and exact driver command are attached. Ubuntu 22.04 ARM64, pinned image/GCC/Valgrind, `-O3 -std=c++20 -pthread`, 20,000 two-job batches, serial fallback, depth 1/8/74. Both execute exactly 40,000 callbacks. Instruction and simulated conditional-branch counts are resilient to the host compiler load; cache and predictor simulation are not hardware measurements and this probe does not profile the full engine.

- Depth 1: instructions -44.00%, conditional branches -61.73%, simulated mispredictions -41.25%.
- Depth 8: instructions -37.69%, conditional branches -56.40%, simulated mispredictions -26.14%.
- Depth 74: instructions -0.59%, conditional branches -0.24%, simulated mispredictions -0.11%.

The sparse path retains substantial instruction/branch reductions; dense occupancy retains the original scan without a material regression. No full-engine speedup is inferred.
