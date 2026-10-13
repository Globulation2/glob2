# Cached turret targets: optimization evidence

**Complete optimization campaign; live activation remains blocked.** The measurements below test commit `5ca02001d4de81e437597e47b52228082e29659e`, optimization commit `04a8a24a489aae6d4d5a80e78143459e3bd77be4`, integrated with master `3e2ba6c07450c94ce0f997220479fd309b71e461`. A later integration with master `277510840` is reported separately under `merged-head/`; never pool the two binaries.

## What changed

Cooldown forecasting now controls refresh admission. At boundary t, the next turret step uses t. The generation serves t+K through t+K+R−1, inclusive. Skip only if the existing cooldown recurrence cannot become ready by that final tick. Positive cooldown crossing zero and the last served tick are explicitly tested; absent ammunition does not suppress refresh.

Preparation reuses captured unit performance and experience, captures active area-attack modifications at the boundary, shares immutable damage rows per boundary, avoids unused render effect capture, and reserves output storage. No mutable shared query caches or additional planner are introduced. The shared completed-tick snapshot union remains the source of input. First-valid consumption, shipping selector and hiring are unchanged. Actual cached firing remains confined to the dedicated research benchmark.

Four variants isolate the optimizations: baseline (both switches off), forecast only, preparation only, combined. Baseline is a control inside the final framework, not a byte-identical historical binary; common source-vector reservation and signature plumbing are not separately attributed. Every comparison pairs against the original authoritative selector in the same executable.

## Protocol and reproduction

Release GCC 13.3, Linux x86_64, Ryzen 7 6800H. Build: `GLOB2_SDL3_PREFIX=/home/bradley/glob2/build/sdl3/prefix python3 tools/dev_build.py dev_fast=0 release=1 -j4 engine-tests unit-tests turret-engine-benchmark`. No fast/PCH/unity performance builds. Exact binaries, compiler flags, source hashes and logs are archived.

`reserved-run.py` records exact commands and environment choices. Each cell uses one process warmup, ten matched seeds, rotated original/shadow/cached order, 256 ticks/sample. Four variants × two capacities × four participant counts × three workloads × three modes × ten seeds produce 2,880 timing observations and 2,880 separate CPU-probe observations. All 64 accepted cohorts exit zero with no sampled blocking contender. Entire contended cohorts are retained in `contended/` and provenance histories and repeated; no outlier is excluded based on its cost.

Logical CPUs 8–15 were reserved cooperatively. Masks: participant 1→8; 2→8,10; 4→8,10,12,14; 8→8–15. Adjacent logical pairs are SMT siblings, so eight participants use four physical cores. Original and cached runs use identical masks; the host monitor uses 0–7. No other process affinity was changed. This is affinity, not kernel shielding: desktop activity, shared bandwidth/cache and power/thermal interference remain possible. Sampled activity and thermal readings are retained in `reserved-provenance.json`. These results are not pooled with earlier unrestricted runs.

Timing includes actual Numbi/Castor orders and validation, ordinary hiring, gradients/growth, turn checksum calculation, incremental shared capture, producer work, publication and final drain. It excludes setup, renderer, transport/pacing, saves, checksum exports and diagnostic output. The combat fixture finds targets in 49.96% of eligible original searches, above the 10% requirement. Confidence intervals use 10,000 matched-pair bootstrap resamples; mean ratios and paired median changes are separate fields in `summary.json`.

## Whole-engine results: four participants

Positive means slower or more CPU. The original baseline is remeasured for every variant/capacity; compare percentage changes within cells, not raw costs across campaigns.

| Variant | L | Empty-range mean | Combat mean | Ordinary mean |
|---|---:|---:|---:|---:|
| baseline | 4 | -23.82% [-24.94, -22.56] | +5.13% [+2.45, +7.45] | +0.83% [-0.09, +1.87] |
| baseline | 8 | -22.68% [-24.47, -21.02] | +7.04% [+5.89, +8.34] | +2.81% [+1.05, +4.49] |
| forecast | 4 | -19.82% [-23.52, -16.46] | +1.02% [-1.72, +3.82] | +4.47% [-2.04, +12.10] |
| forecast | 8 | -22.62% [-23.97, -21.36] | +2.79% [-0.02, +5.67] | +0.73% [-1.74, +3.18] |
| preparation | 4 | -23.37% [-24.71, -22.00] | +6.12% [+5.42, +6.85] | +0.98% [+0.06, +1.89] |
| preparation | 8 | -23.79% [-25.67, -22.58] | +6.83% [+5.35, +8.61] | +1.19% [-0.91, +3.01] |
| combined | 4 | -23.67% [-24.99, -22.27] | +3.29% [+2.72, +3.86] | +1.78% [+0.72, +2.87] |
| combined | 8 | -20.23% [-23.81, -14.29] | +1.52% [+0.94, +2.19] | +1.82% [-0.16, +4.21] |

Cooldown admission is the large combat improvement. At L=4/four participants, admitted sources fall from 25,700 to 1,731.8 per 256-tick sample (93.3% fewer), and planning CPU falls from 54.84 to 5.06 µs/tick. Preparation alone reduces measured source/facts CPU from 19.79 to 11.82 µs/tick, but does not establish a whole-engine combat gain: +6.12% versus +5.13% baseline. At L=8, preparation reduces source/facts CPU from 19.39 to 11.34. Combining both reduces it to 4.09. Do not add independently measured percentages or claim preparation alone passes qualification.

Combined L=8/four participants: original→cached costs are empty 269.4→214.9 µs/tick, combat 457.2→464.1, ordinary 176.9→180.2. Process CPU changes are −11.50%, +0.36%, +1.52%. These point estimates pass the corresponding workload/CPU gates. Empty paired median change is −23.17%, CI [−24.66,−20.48]; the mean estimate is −20.22% because retained outliers matter.

All cached variants/counts remain below +15% process CPU. The combined variant passes the three workload point gates at participants 1, 4 and 8 for both capacities, but fails the empty-range improvement gate at participant 2: L=4 +2.9%, L=8 +2.3% mean. The executor schedule can dominate elapsed time despite reduced process CPU. There is no participant-independent qualification. Some control confidence intervals cross the ordinary-control limit; a point-estimate pass is not proof of a tight regression bound.

The combined four-participant combat runs fire 5,997 shots versus 5,966 original across ten repetitions (+0.52%), for both list capacities. Both trajectories end with summed unit counts 1,260 and turret counts 1,000. Empty and ordinary controls fire zero shots and retain matching counts. These short workload fixtures are not battle-balance qualification.

## CPU attribution, including workers

Separate probe runs measure original scans, paired incremental shared capture, source/facts preparation, occupied-index maintenance, all planning callbacks summed over every executor thread, publication, current-world consumption and admission residual. Publication excludes join CPU to avoid counting owner-executed callbacks twice. Join elapsed wait is a separate field. Shared executor dispatch/completion/join bookkeeping is not wholly attributed to the turret sum; whole-process CPU includes it. Every timer has overhead, especially cheap per-shot scopes. This is scoped targeting CPU, not an exact allocation of all engine CPU.

At four participants/L=8, combat scoped cost drops from 100.74 µs/tick in the control to 22.27 combined (77.9% less). Original scans cost 9.19 µs/tick, so background production still costs more total scoped CPU than original combat scans. Empty scoped cost drops 100.54→89.84, versus original scans 156.14. Ordinary scoped cost is 16.44 versus original scans 7.04. Incremental capture is a difference of paired measurements and can be slightly negative from noise; it is not clamped.

The ready-to-fire path alone drops original scan→consumption by about 61–63% at L=8/four participants. Summing the equally weighted empty/combat fixture scopes, the observed reduction is 61.8–66.4% across both capacities and all participant counts. Charging capture, source/facts, index, publication and admission to the owner changes the interpretation: combat owner scopes total 17.15 µs/tick versus 9.19 original scans; ordinary totals 14.58 versus 7.04. Thus the 30% hot-path targeting gate passes, but a broader all-producer owner-cost definition does not in combat/control. Full CPU and whole-tick tables remain decisive.

## Four versus eight entries and responsiveness

Shorter output does not eliminate candidate enumeration. No consistent total-CPU advantage for L=4 is established across participant counts. Both capacities traverse candidates; see the full CPU ledger. Combined observed peaks are 63,360 index bytes and 216,400 shortlist reservation bytes (under 0.27 MiB together). These are index/list accounting, not total process RSS or shared snapshot memory; enforceable caps remain 32 MiB each.

Actual scheduled producer shadow evaluation on 20 stock Numbi/Castor battles (ten seeds × two fixtures, 1,024 ticks each), with 868 original firing opportunities per configuration:

| Variant | L | Same reference target | Missed windows | p95 added delay | Max completed delay |
|---|---:|---:|---:|---:|---:|
| baseline | 4 | 756/868 (87.10%) | 0/868 | 0 ticks | 2 ticks |
| baseline | 8 | 758/868 (87.33%) | 0/868 | 0 ticks | 2 ticks |
| combined | 4 | 756/868 (87.10%) | 2/868 | 0 ticks | 4 ticks |
| combined | 8 | 758/868 (87.33%) | 2/868 | 0 ticks | 0 ticks |
| forecast | 4 | 756/868 (87.10%) | 2/868 | 0 ticks | 4 ticks |
| forecast | 8 | 758/868 (87.33%) | 2/868 | 0 ticks | 0 ticks |
| preparation | 4 | 756/868 (87.10%) | 0/868 | 0 ticks | 2 ticks |
| preparation | 8 | 758/868 (87.33%) | 0/868 | 0 ticks | 2 ticks |

Misses are infinite added delay for percentile calculation, not dropped observations. No windows are censored here. Cooldown admission introduces two missed shadow windows (0.23%), still below the 5% threshold. Original firing arms its cooldown even if the hypothetical consumer waits; forecasts on that reference trajectory can suppress subsequent hypothetical refreshes. These data describe availability on the reference trajectory, not the outcome of different cached shots. L=8 gains only two agreements (0.23 percentage points). Earlier adversarial one-tick air windows miss 16/16 for delayed lists; those remain failed controls and preclude claiming universal responsiveness. Dedicated whole-engine opportunity fixtures pass with 100% agreement/zero misses; they offer obvious targets and are less discriminating.

Recommendation: retain both measured capacities for further work; do not promote a live configuration. Both combined L=4 and L=8 pass the measured stock delay/miss gates and selected participant workload point gates. Their agreement difference is below one percentage point; age is identical and CPU ordering is not robust, so the plan’s tie-break cannot confidently select L=8 on agreement alone. Smaller L=4 has lower reservation cost, while L=8 removes the two completed two/four-tick delays in this corpus. Resolve the total-CPU tie and short-window coverage after persistence and final integration testing.

## Correctness and remaining gates

Pre-integration validation: 76 focused engine cases/326,059 assertions; 71 compatibility cases/138,372 assertions; two whole-engine cases/25,336 assertions on final measured head; all eight stock corpus runs/159,000 assertions each. Correctness artifacts include original/shadow identical orders, per-tick traces and final saves, cached determinism across participants 1/2/4/8, custom geometry/wrapping, unit incarnations/flight/conversion, blockers, source compatibility, cooldown boundaries, failures and shared lifecycle. `compatibility.log` was an accidental zero-case engine invocation and is retained but is not evidence; the actual compatibility executable/log passed 71 cases.

Original/shadow continuation passes all nine cuts. Cached continuation still diverges at cuts 39 and 40 around the first firing opportunity; seven other cuts pass. Published/pending lists and logical admissions are not serialized. This is a live-activation blocker, not a shipping save regression. The pipeline remains benchmark-only and the save compatibility floor is unchanged. No simulation-version change is needed for unchanged shipping targeting; future live activation needs independent experiment registration, version/golden/replay/network acceptance and persistent logical state.

Linux x86_64 GCC13 only. Cross-platform execution equivalence, future live replay/network acceptance, renderer/transport costs, unmerged hiring-experiment interference, and statistically qualified changed-shot battle outcomes remain unavailable. Existing normal hiring is exercised and unchanged. Preserve the four-tick budget and first-valid firing rule. The complete matrix and charts show a useful optimization, not completion of the original live-trial goal.

All raw timing/probe CSVs, host observations, seeds/commands, scripts, original contended cohorts, binaries, traces, orders and saves are in the split archive; `summary.md`, `summary.json`, `timing.csv`, charts and this report are also published directly. The merged-head supplement carries its own provenance and acceptance results.
