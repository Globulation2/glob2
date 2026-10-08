# Preliminary mechanism results — PR 963

These are instruction/allocation results, not wall-time speedup claims. Full acceptance remains pending. All work is local to therig/Linux x86-64.

| Idea | Measured result | Assessment |
|---|---|---|
| Hiring scans | Owner instructions reduced 5.48–26.75% across five fixtures, four participants | Strong candidate |
| Statistics/checksums | Owner instructions reduced 1.46–25.00% across five fixtures, four participants | Strong candidate |
| Empty queue drains | Sparse fixture queue allocations 40,960 → 32,784 over 4,096 ticks; about 20% of drains empty | Proven allocation saving; wall-time value pending |
| Player reservation | Sparse fixture observation allocations 12,288 → 4,096 over 4,096 ticks | Proven small allocation saving |
| Hiring-vector reuse | Allocations under updateAllBuildingTasks: hiring 19,137 → 19,107; established 32,157 → 30,746; dense 54,049 → 49,658 | Workload dependent; larger benefit in dense colonies |

Instruction windows are 512 ticks. Allocation windows are 4,096 ticks. All figures exclude loading, generation, and final teardown. The dense vector allocation comparison uses one participant; the other allocation rows use four. Sparse queue row uses one participant; allocation totals are consistent across participant controls. Allocation counts under hiring include descendants; raw filtered stacks show attribution.

Baseline da57b459f1eb20b9b4a08d25eb502711dd81986b; GCC 15.2.0, release -O3, profile=0, retained debug symbols, no -pg. Every candidate is built directly against the same baseline. Fixture metadata and exact production patches accompany the measurements. The candidate names in builds.json identify initial development commits; source.patch identifies the exact production delta after commit repacking.

Completed individual 4,096-tick comparisons preserve per-tick checksum traces, replay bytes and final save bytes. Focused tests passed 130 cases per candidate; two additional queue reset/concurrent-producer tests are being validated. GCC 13 combined continuations matched GCC 15 at one and four participants for all five fixtures. Full GCC 13 integration validation against newer master and native paired timing acceptance remain in progress. No ARM64 claim is made.
