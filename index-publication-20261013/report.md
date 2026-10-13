# Worker index preparation and publication simplification

Tested source `7e9d63af602e6f3bf970ddff951fd2d0da0be35b`, base `db1c4f0526b287b7aca11e0dce1a5c142123cffb`.

## Implementation

- Build the occupied index once per admitted generation in the shared executor, before its team planning jobs read it. Reuse the previous **logical admission**, ensuring its index preparation first if necessary. This preserves incremental work without selecting a baseline from worker readiness. No new jobs or threads are added; team planning stays sequential within one job per due team.
- Retain only the prepared index after its jobs join; release captured world leases. A small once-prepared descriptor coordinates immutable results, not a mutable per-team query cache. Preparation errors are retained and propagated at deterministic publication.
- Remove the per-tick published-table cleanup scan. Validate incoming outputs and current sources as before; one record per building slot replaces obsolete incarnations. Reclaim dead records if admission would exceed the shortlist budget. This may retain unusable records between reuse/pressure/teardown, but they cannot fire. Generation/preparation metadata and spare vector capacity remain conservatively charged under32MiB; occupied indices retain the existing atomic32MiB bound.
- Add overlapping logical-reuse and repeated slot-reuse tests and separate background index CPU counters. Shipping targeting, hiring, first-valid firing, four-tick age/responsiveness rules and snapshots captured from the existing union are unchanged.

## Rejected intermediate attempt

The first worker move reused the last joined index. Admission happens before publication at the same logical boundary, so that baseline was older than the prior owner-built index. Combat index CPU nearly doubled (3.25 owner→5.91–6.10 workerµs/tick in the archived stale-baseline campaign); whole-engine combat elapsed rose4.7% versus the previous prototype. It was not advanced to live qualification. The final logical predecessor fix restores one incremental preparation per admission. All failed/intermediate measurements, source patches and binaries are retained under superseded/ and stale-baseline/. A preliminary failed worker-failure assertion expected zero retained bytes; it was updated to account for the256-byte cached preparation allowance, while still proving no outputs/pending leases survived.

## Protocol and provenance

Linux x86_64/Ryzen7 6800H/GCC13.3 release-O3, no fast/PCH/unity. Four participants, L8, K=R=1, margin1. Ten paired subprocess seeds, rotated binary ordering and internal original/shadow/cached ordering. Each process warms each fixture/mode before measuring256 ticks. Timing, light whole-owner CPU and detailed probes are separate phases:270 observations in each phase. All90 process runs retained; owner0/30, detailed7/30 and timing1/30 had sampled non-desktop overlap. Own runs confined to8,10,12,14, monitor0–7; no other work was changed. Thermal/process activity is archived. The single timing overlap was before-timing-r0. No exclusion or outlier trimming is used; these results do not substitute for a fully quiet qualification campaign.

Before is the frozen db1c4f052 implementation. The index-only control is a controlled relink of the final logical worker-index implementation with the db1c4f052 publication function. Its public headers and all other objects match the combined candidate; its common harness banner describes the candidate, so index-control-provenance.json and the exact control source are authoritative. Both individual changes can therefore be assessed. Source patches, frozen binaries and exact compile/link/run commands are included.

Whole-region owner CPU includes initial admission, AI/order handling, all headless ticks and final drains. Component probes perturb cheap operations and are not interchangeable with the lighter owner phase. Join CPU encloses owner fallback work; background index/plan CPU must be split by actual executing thread to avoid double counting. Publication includes resource release and excludes join CPU. Complete process CPU includes all workers and executor overhead. Captured/fired trajectories are identical across these optimization variants; original versus cached remains a distinct targeting scheme.

## Whole-engine results

Changes below compare combined to previous prototype; paired95% bootstrap intervals.

| Workload | Owner CPU before→after µs/tick | Owner change | Engine elapsed change | Process CPU change |
|---|---:|---:|---:|---:|
| Empty | 132.06→127.46 | -3.48% [-5.57,-1.20] | -4.33% [-10.05,+0.05] | -3.72% [-7.25,-1.15] |
| Combat | 407.27→391.44 | -3.89% [-5.89,-1.83] | -3.55% [-6.41,-1.33] | -1.64% [-2.46,-0.88] |
| Ordinary | 74.49→72.79 | -2.28% [-3.74,-0.48] | -1.34% [-3.85,+0.74] | -1.82% [-3.07,-0.48] |

[Whole-engine chart](engine-delta.png)

## Individual changes

| Workload | Index-only owner CPU change | Publication addition owner CPU change |
|---|---:|---:|
| Empty | -0.84% [-2.87,+1.41] | -2.67% [-4.34,-1.03] |
| Combat | -1.53% [-2.88,-0.04] | -2.39% [-3.56,-1.22] |
| Ordinary | -2.42% [-3.00,-1.84] | +0.15% [-1.38,+1.81] |

## Component costs

µs/tick; detailed probes. Owner scopes exclude incremental capture. Worker index now includes the move; owner index is0. Worker planning stays off owner in all four-participant cells. Additional capture is a noisy paired residual and must not be interpreted as all shared snapshot cost.

| Workload | Owner scopes before→index→combined | Complete turret CPU scopes before→combined |
|---|---:|---:|
| Empty | 76.65→75.52→75.58 | 84.48→87.41 |
| Combat | 14.53→11.90→12.27 | 21.58→22.51 |
| Ordinary | 13.22→11.60→11.10 | 16.95→15.92 |

[Component chart](owner-scopes.png)

Combat combined: source/facts3.89, admission remainder1.85, publication/release2.38, join CPU0.35, firing/validation3.80µs/tick. Worker planning5.58 and index3.88 are outside owner scopes. Scope CPU still rises about4.3% in combat versus previous, while complete process CPU falls1.64%; other engine work/cache/scheduling and probe overhead prevent identifying those as the same measurement.

## Original targeting comparison

| Workload | Combined elapsed change vs original | Process CPU change vs original |
|---|---:|---:|
| Empty | -26.17% [-27.27,-24.91] | -15.34% [-16.92,-14.06] |
| Combat | +3.15% [+1.69,+4.32] | +1.52% [+0.60,+2.49] |
| Ordinary | +1.77% [-0.14,+4.09] | +1.53% [+0.60,+2.59] |

Combat remains3.15% slower than original targeting in this cohort. Ordinary elapsed estimate+1.77% has an interval spanning zero. The improvements above do not qualify the live trial or other participants/capacities.

## Callgrind

Thread-separated measured regions, profiling clocks disabled, setup/warmup/teardown excluded. One paired combat region per implementation; function counts inclusive and overlapping.

- Owner admission instructions:8,918,098→2,415,968 (−72.9%). No prepareIndex/buildIndex executes on owner in either worker-index version. All6.46M index instructions in combined execute on workers, close to the previous6.40M owner index instructions: logical reuse avoids the doubled traversal.
- Publication instructions:2,684,919→1,346,530 (−49.8%). resolveBuilding calls23,438→1,738 (−92.6%); all21,700 retained cleanup checks disappear. Current output/source compatibility checks remain.
- Native publication CPU only changes2.45→2.38µs/tick in detailed probes. Instruction counts are not native time: resource release, cross-thread allocation/cache behavior and clocks remain. The index-only control had more inclusive publication release instructions; raw calling trees are preserved rather than summed across overlapping children.

## Validation

-Committed-head rebuild:75 focused cases across TurretTargeting/WorldSnapshot/BuildingAreaEffects/SimulationReadPhase/SharedWorkerLifecycle,326,577 assertions;9,216 trace assertions and nine byte-identical committed/timed/before artifacts.
-56 focused TurretTargeting/WorldSnapshot/SharedWorkerLifecycle cases,202,468 assertions pass. New tests prove a single rebuild across4 overlapping generations/8 team jobs at1/2/4/8 participants, and constant shortlist storage through100 incarnations at1/4 participants.
-Each before/index/combined benchmark passes9,216 assertions for original/shadow parity and cached determinism at1/2/4/8 participants. Nine exported traces/saves are byte-identical across all variants.
-Numbi/Castor20-battle corpus passes159,000 assertions; actual pipeline L8 and reference trajectory remain unchanged.
-Save continuation original/shadow9/9 each. Cached continuation still fails cuts39/40 and passes the other7: existing live blocker is unchanged. These are recorded failures, not a passed persistence gate.
-Master fetched unchanged at277510840. Linux x86_64 only; supported-platform comparisons and final live replay/network/version qualification unavailable. No simulation-version/save-format change is required for these result-preserving research-cache changes; shipping execution stays original.

## Recommendation

Keep the corrected worker index and publication simplification as the prototype. They remove owner work without adding jobs, threads, mutable team caches or source epochs, and improve complete owner CPU across these workloads. Continue qualification separately: combat still regresses against original and cached save continuation remains unimplemented. Source/fact preparation and current compatibility checks are the remaining owner targets; measure them before further changes.
