# Turret hot-path microprofiling

Source: `db1c4f0526b287b7aca11e0dce1a5c142123cffb`; previous implementation `68fd613617818692feb0bb9e99b9019860b147f8`. This change adds profiling only. Shipping targeting and hiring remain unchanged.

## Result

Planning is off the owner in every measured four-participant cell. Owner planning CPU and owner Callgrind planning instructions are exactly zero. Serial validation assigns all planning to the owner, with zero worker planning. This corrects the earlier claim that a multi-participant deferred join could execute planning on the owner. It waits instead.

**Combat still has appreciable owner preparation and publication work.** Detailed probes measure original scans at 9.84 µs/tick versus cached firing at 3.67. Source/fact preparation 3.53, index 2.87, admission remainder 1.94, publication/release 2.49 and join CPU 0.25 leave 14.75 µs/tick of owner turret scopes before incremental capture, approximately 4.91 more than scans. Worker planning adds another 5.30 µs/tick. The light whole-owner measurement adds 5.68 µs/tick, but its interval crosses zero.

## Measurement limits

**All 30 native processes had sampled competing host activity.** They are exploratory attribution, not quiet-host qualification. Rendering Callgrind/perf campaigns ran concurrently. No other process was paused, messaged or reaffinitized. Own runs use physical cores 8,10,12,14; monitor uses 0–7. Preserve every sample/outlier. Intervals describe these samples and cannot remove host contention. The user requested confining only this experiment and allowing other work to continue. No timing gate is claimed.

Linux x86_64, Ryzen 7 6800H, GCC 13.3, release -O3, no fast/PCH/unity. Ten paired seeds with one process warmup per cell and rotated original/shadow/cached order, 256 ticks each; K=R=1, L=8, margin=1, four participants. Timing, light owner clocks and detailed CPU probes run in separate processes: 90 observations per phase. Exact commands, binary hashes, thermal/process activity and seeds are in native-provenance.json and logs. No correctness exports or saves during timing.

Detailed per-shot clocks perturb cheap operations. The owner-only phase avoids those and pipeline clocks. Whole-owner includes initial admission, AI/order/network work, full headless ticks and final drains. `syncStep` is a nested part; preparation also happens at explicit read boundaries outside it. Its isolated improvement does not establish a shorter complete engine critical path. CPU excludes descheduled/sleep time; elapsed includes it. Worker costs count CPU on every executing worker.

Cached shots can change the reference trajectory. Original/shadow comparisons keep the original shots authoritative; use them for incremental capture. Cached full-engine comparisons include changed combat outcomes and scheduling.

## Native results

| Workload | Original → cached whole owner CPU µs/tick | Added (paired 95% interval) | Whole elapsed change |
|---|---:|---:|---:|
| Empty range (100 turrets) | 240.01 → 149.68 | -90.33 [-96.53, -85.00] | -21.48% [-28.42, -13.40] |
| Combat (100 turrets) | 440.31 → 445.99 | +5.68 [-5.42, +16.49] | -0.62% [-9.44, +6.93] |
| Ordinary (4 turrets) | 87.60 → 84.78 | -2.83 [-9.66, +3.53] | +1.77% [-0.54, +3.98] |

[Owner CPU chart](hot-path.png)

## Owner and worker component costs

All values µs/tick from detailed probes. Capture is the explicit read-boundary residual after admission, including requirement checks and non-turret producers. It is not all additional turret cost. Publication excludes join CPU but includes observation/output destruction. Join CPU already encloses owner planning in serial mode, so never add both. Admission remainder is all admission CPU less measured source/facts and index; it is not an exact shared-executor bookkeeping attribution.

| Scope | Empty | Combat | Ordinary |
|---|---:|---:|---:|
| Source/fact preparation | 7.31 | 3.53 | 2.15 |
| Index construction/maintenance | 2.25 | 2.87 | 2.45 |
| Admission remainder | 4.29 | 1.94 | 3.00 |
| Publication + release | 5.97 | 2.49 | 2.52 |
| Join CPU | 0.77 | 0.25 | 1.57 |
| Join elapsed (includes wait) | 1.91 | 0.30 | 17.65 |
| Cached firing (includes lookup/validation) | 60.20 | 3.67 | 2.78 |
| Worker planning | 7.28 | 5.30 | 1.98 |
| Owner planning | 0.00 | 0.00 | 0.00 |

Incremental capture on the unchanged reference trajectory (shadow minus original):
- Empty range (100 turrets): +3.00 µs/tick [+0.69, +6.05].
- Combat (100 turrets): +0.33 µs/tick [-1.09, +1.76].
- Ordinary (4 turrets): +0.29 µs/tick [-0.72, +1.27].

## Function-level attribution

Callgrind 3.22, collection enabled for all threads, instrumentation starts only for the measured region after setup/warmup and stops before reporting/teardown. Thread 1 explicit dump is owner; final dumps of threads 2–4 are workers. Instruction counts do not depend on native scheduler delays. Their synchronization paths can still vary. Counts below are inclusive and overlap; index/fill are inside observe/admission, and complete is inside publication. Do not sum them.

| Owner instructions/tick | Empty | Combat | Ordinary |
|---|---:|---:|---:|
| Admission | 57,868 | 34,776 | 21,109 |
| Requirements checks | 657 | 3,012 | 144 |
| Index (inside admission) | 15,745 | 24,947 | 15,643 |
| Chunk fill (inside index) | 10,507 | 20,467 | 10,266 |
| Publication incl. complete | 45,950 | 10,473 | 5,093 |
| First-valid kernel | 10,341 | 2,226 | 414 |
| Published lookup/source check | 24,686 | 1,301 | 764 |

Combat detail from the owner calling tree:

- Admission: 8,902,593 instructions over 513 calls; 72 observations. Index is 6,386,465 (71.7% of admission), of which fillChunk is 5,239,579 (58.9% of admission). Captured snapshot projection is small; the expensive part is rebuilding occupied chunks.
- Publication: 2,681,201 instructions over 256 ticks. It calls resolveBuilding 23,438 times: 21,700 retained cleanup checks plus 1,738 output checks. That child costs 1,148,462 instructions. Source compatibility costs 232,892 over 1,738 calls. Completed observation destruction costs 343,568 instructions over 72 generations.
- Original scans: 23,775,842 owner instructions. Cached firstValid kernel 569,830 and published lookup/source validation 333,174; planning 8,567,736 worker instructions, zero owner planning. These smaller kernels exclude surrounding benchmark dispatch and bullet machinery.
- Full shared capture is 60,564,182 original versus 60,627,833 cached instructions. Most of that capture is already required by other producers. The difference is 63,651 instructions across the region; it is not a native µs estimate.

Cache/branch simulation is retained separately under cache/. It is a model, not hardware-counter timing or proof of the cause of CPU differences. Cached combat owner D1 read misses rise 15.39M→16.48M and modeled last-level read misses 1.18M→1.26M across the whole region, which also has changed combat trajectories. Do not attribute that entire difference to the index. Function-level modeled counters are in combat-owner-memory.txt.

## Validation and provenance

- 54 focused TurretTargeting/WorldSnapshot/SharedWorkerLifecycle cases, 201,598 assertions pass.
- Original/shadow parity and cached participant-count determinism pass: 9,216 assertions; repeated with light owner clocks enabled, also 9,216.
- Serial metric validation: owner planning equals total planning and worker planning is zero in all nine cells. Four-participant metrics: owner planning zero in all 90 detailed cells.
- Latest master fetched: 277510840054b1416f2defd7d86fe1431dd0ddc0, unchanged. Commit db1c4f052 contains only derived profiling counters and benchmark/documentation changes; no simulation-version or save-format change.
- Two frozen benchmark binaries and exact source patches are included. Root instruction profiles use the first harness; cache combat pair uses it too. Ordinary/empty cache pairs and native measurements use the harness adding light owner clocks. Each paired Callgrind comparison uses the same harness; profiling is off in those regions. The cache runner originally hashed the binary path after each process; it was replaced during the second run. Its corrected invoked hash is backed by the emitted source-tree hash matching the frozen first binary; original raw postprocess records are retained. Future runner hashes before invocation.
- No live qualification, persistence repair, cross-platform equivalence or general enablement in this pass. Previously known cached continuation failures remain.

## Recommendation

Move index construction off the owner first, retaining immutable snapshot and index-budget ownership. Then reduce retained-list cleanup scans with explicit lifetime/invalidation handling while preserving supersession and source compatibility. Avoid redesigning shared capture on the basis of the small combat delta. Repeat the light owner and detailed CPU protocol on a quiet host before claiming a reliable hot-path improvement. This pass measures the costs; it does not implement those next optimizations.
