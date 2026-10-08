# Production snapshot copy policy

Source `edb09d40204a1fdb3a6d0e5934ef34e9c344de60` versus preserved executable from `5430ef31c135667deeb8043c827d79b3fec4cda4`. Integrated master `06a106d3a888ef11482a955bddb713d584cd5570`; fresh master `a0a59e4a6478a2533ec0c10efb8027f0db1152a8` has a clean merge-tree. New master changes concern skin rendering/assets/build rules and scripting tests, not the simulation/snapshot implementation. No merge or rebase was needed.

Copy changed 16×16 chunks at at most 50% dirty; otherwise copy the entire contiguous array. No simulation, persistence or random-number behavior changes. The byte counter now includes multi-material stock sidecars, so historical byte totals are not directly comparable. No proposal-format, executor-placement or statistics changes in this follow-up.

## Reserved-core production measurements

Linux x86-64, Threadripper 2950X, GCC 15.2 release/O3. Physical cores 0–3 and siblings 16–19 reserved, engine affinity 0–3, performance governors. Fixed 1,024 ticks, one warm-up then ten pairs with alternating execution order; same saves, seed/orders and AI/gradient settings. Growth shared, delay 8, four executor slots. Outstanding computation is drained. Per-tick checksum runs are separate. Bootstrap 95% intervals describe these paired samples on this host; shared caches/memory bandwidth/package power are not isolated.

| Scenario | TPS change (95% interval) | Saved wall ms / 1,024 ticks (95% interval) | Process CPU reduction (95% interval) | Capture time saved ms |
|---|---:|---:|---:|---:|
| ai512 | +0.6% [-1.5, +2.0] | +8.48 [-21.53, +27.94] | -0.1% [-0.8, +1.8] | +32.55 |
| dense | +3.2% [+1.2, +5.3] | +14.67 [+5.60, +24.03] | +2.6% [+0.8, +4.8] | +125.85 |
| disabled512 | +0.4% [-1.5, +2.9] | +3.92 [-16.44, +30.09] | +0.5% [-4.0, +3.6] | +37.95 |
| multi | +38.6% [+32.0, +43.0] | +1502.38 [+1288.18, +1621.66] | +3.6% [+1.0, +6.4] | +1826.25 |

Stage timers are elapsed spans across overlapping pipeline work, not an additive CPU decomposition. Faster capture changes worker overlap and queueing; capture-time savings can exceed total engine savings. The prior isolated prototype measured +22.3% [17.1, 42.7] on the multi-material fixture; this production rerun is larger but its interval overlaps that earlier interval. Neither number should be generalized to other machines or workloads.

Full process wall/CPU/peak RSS, tick median/p95/p99, growth stage timers/counters and snapshot metrics are in `summary.json` and raw measurement rows. Growth counters and final world checksums match for all 88 timing invocations.

This comparison measures the snapshot optimization against the previous delayed implementation. It does not establish that delayed parallel growth beats original immediate growth: the earlier fair experiment, applying bulk copies to both original and delayed engines, found no established parallel-growth throughput advantage. See the retained `attribution-v2` report.

## Correctness and coverage

- 102 selected snapshot/resource/gradient/executor tests and 12 unchanged golden cases pass. Exact-half remains sparse; dense refresh preserves held snapshots and updates stamps correctly for subsequent sparse reuse. Stock-sidecar accounting and contents are covered.
- 72 engine verification runs: dense, multi-material, AI and disabled fixtures × delays 1/3/8 × previous shared executable plus new owner and shared 1/2/4/8 slots. World and replay per-tick checksum files match exactly within each scenario/delay.
- Initial new test incorrectly assumed resource-only captures could share across ticks without capturing terrain; the two unrelated assertions were removed. Initial failure logs are retained, alongside clean final logs.
- No simulation revision/golden/save/replay/network gate changes: this changes only copy strategy and diagnostics. Existing golden tests include save continuation. Actual Windows/macOS/ARM/browser/threadless execution remains unavailable; this follow-up does not close those PR-wide gaps.

## Reproduction and evidence

See `commands.txt`, `verification.json`, build logs, test XML/logs, per-tick traces, `timing/metadata.json`, measurement rows and cpuset/governor audits. Scripts are under `scripts/`; fixture hashes and paths are in timing metadata, and fixture saves remain in the earlier evidence directories. Source binaries are identified by SHA-256 rather than uploaded.

The local audited cpuset wrapper permits unrelated reservations on other cores, while requiring our effective/exclusive cores, valid partition, memory node, controllers and unprivileged child affinity to remain correct. It removes only its own cgroup. The production reservation wrapper is unchanged. All owned reservations and governors are restored after the run.
