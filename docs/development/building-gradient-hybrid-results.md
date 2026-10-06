# Building-gradient hybrid experiments — October 2026

The hybrids reduce wasted CPU, but neither meets the original requirement of at
least 5% less engine time across the building-heavy workloads. Keep every policy
experimental and off by default. The staffing policy is the most useful current
compromise; the partial policy needs a different algorithm before promotion.

## What was implemented

* **Staffing** (`building-gradient-hybrid`): refresh in the background only when
  at least four living assigned workers use that building/movement class. Other
  destinations keep the existing synchronous lazy path. This is a deterministic
  demand proxy, rather than a measured query-history classifier. Pending jobs keep
  their deadlines when staffing changes.
* **Partial** (`building-gradient-partial`): build enough to answer captured worker
  positions, targets and adjacent steps, then resume the published frontier on
  demand using the captured terrain. Round-trip fields use the same resumable
  search kernel, including unequal source costs and propagation caps. Walking
  parents still finish fully when they have round-trip children.
* **Combined**: apply both rules. **Eager** is the previous scheduled full-field
  pipeline. **Lazy** is the experiment-off baseline.

Both flags require `building-gradient-pipeline`. First construction stays
synchronous. Publication remains deterministic after four ticks in the timed
comparison. Worker threads use private immutable inputs, never live buildings.
Saving and overflow spooling materialize partial private results without publishing
before their deadlines. Audits complete detached fields and leave live cursors
alone. The saved format is 139, network protocol 57 and simulation revision 16;
the existing minimum save version remains 58.

## Timing results

One executable, four asynchronous workers, four compute threads, delays of four
ticks, 2,048 measured ticks per continuation, ten balanced paired rounds for each
of three checkpoints. All five policies used `--compute-experiments none`, retaining
the unconditional hiring eligibility filter. Timing had scheduler instrumentation
and impact auditing disabled. There were 150 accepted observations, plus warmups;
the largest measured background load in an accepted observation was 0.161 cores.

Numbers are percentage changes relative to lazy. Negative means less time or CPU.
CPU is the sum consumed by all threads during the measured engine interval.
Intervals are paired 95% Student-t confidence intervals across ten timing repeats.
These intervals describe repeatability at these checkpoints, not the population
of all maps and seeds.

| Workload | Policy | Engine time change (95% CI) | CPU change (95% CI) |
| --- | --- | --- | --- |
| busy | eager | -9.42% [-9.93, -8.91] | +30.85% [+30.17, +31.53] |
| busy | staffing | -2.74% [-3.07, -2.41] | +4.84% [+4.52, +5.16] |
| busy | partial | -4.76% [-5.19, -4.32] | +17.80% [+17.33, +18.27] |
| busy | combined | -2.16% [-2.46, -1.87] | +3.25% [+2.93, +3.56] |
| mixed | eager | +3.98% [+3.29, +4.68] | +39.74% [+38.89, +40.58] |
| mixed | staffing | +0.33% [-0.24, +0.90] | +0.28% [-0.26, +0.83] |
| mixed | partial | +9.26% [+8.74, +9.77] | +27.45% [+26.71, +28.19] |
| mixed | combined | +0.23% [-0.29, +0.74] | +0.19% [-0.26, +0.63] |
| small | eager | -21.11% [-21.43, -20.78] | -9.34% [-9.74, -8.94] |
| small | staffing | -17.97% [-18.29, -17.64] | -10.22% [-10.57, -9.86] |
| small | partial | -17.98% [-18.49, -17.47] | -9.49% [-10.09, -8.89] |
| small | combined | -15.73% [-16.27, -15.18] | -9.90% [-10.51, -9.29] |

Staffing reduces the busy workload's extra CPU from 30.85% to 4.84%, while its
speed improvement falls from 9.42% to 2.74%. It avoids the eager pipeline's mixed
workload regression and retains much of the small workload gain. Combining partial
search with staffing reduces busy CPU further, but also gives up additional speed.

Partial search alone reduces busy and mixed CPU relative to eager, yet regresses
mixed engine time by 9.26% relative to lazy. This implementation still finishes
walking parents for round trips, scans fields to seed searches, sorts deferred
sources, and reconstructs frontiers at publication. Work saved in propagation does
not eliminate those costs. The data establish the regression; they do not apportion
it precisely among those mechanisms.

## Did measurement code cause the extra CPU?

A separate same-binary ablation paired scheduler instrumentation on/off, with ten
rounds per checkpoint. For the four-tick eager pipeline:

| Workload | Instrumentation engine-time overhead (95% CI) | CPU overhead (95% CI) |
| --- | --- | --- |
| Busy | +0.213% [-0.151%, +0.576%] | +0.123% [-0.350%, +0.596%] |
| Mixed | +0.131% [-0.324%, +0.585%] | +0.295% [-0.210%, +0.800%] |
| Small | +0.316% [-0.464%, +1.096%] | +0.401% [-0.268%, +1.070%] |

This does not support scheduler measurement code explaining the previously observed
30–38% CPU increase. These are measured intervals, not a probability that an
arbitrary definition of “meaningful” overhead holds. Fresh-oracle impact auditing
is substantially more expensive and was excluded from all primary timing runs.
The switch measures scheduler clocks, counters and diagnostic queue scans. It does
not undo the decision-evaluator refactoring or remove every disabled-audit branch;
those paths are shared by the five-policy same-binary comparison.

**Correction to the earlier report:** its `--compute-experiments hiring` setting
also enabled the legacy frontier prepass. Retained counters confirm it ran. That
setting was equal on both sides of the instrumentation ablation, so the overhead
comparison remains paired, but the earlier description “frontier disabled” was
incorrect. The five-policy experiment above disables it everywhere. Code now
prevents that prepass running alongside the scheduled building pipeline.

## Tail latency, waits and memory

The following p95 tick times come from separate instrumented 2,048-tick diagnostic
runs, one per policy/checkpoint. They are descriptive, with no repeat-level confidence
interval, and must not be pooled into the instrumentation-off timing experiment.

| Workload | Lazy p95 ms | Eager | Staffing | Partial | Combined |
| --- | --- | --- | --- | --- | --- |
| busy | 4.719 | 3.646 | 4.438 | 4.047 | 4.581 |
| mixed | 2.865 | 2.700 | 2.814 | 2.915 | 2.867 |
| small | 0.792 | 0.653 | 0.665 | 0.794 | 0.783 |

Largest observed deadline wait across these diagnostics: 0.585 ms. Wait counters include the executor wait-call overhead, even when a job is already complete.

Peak admitted building-job storage in the diagnostic runs was 11.625 / 7.000 /
12.125 / 7.500 MiB for eager / staffing / partial / combined on busy, and at most
7 MiB on mixed and 5.5 MiB on small. There were no queue-budget synchronous
fallbacks in these checkpoints; synthetic tests cover that path. Mixed staffing
submitted no background building jobs, explaining its baseline-like results.
Published fields and search scratch are excluded from the 64 MiB admission budget.

The timing harness retained large verification traces before launching children.
Its process peak-RSS observations are about 506 MiB on busy, 161 MiB on mixed and
176 MiB on small. On small they are identical across policies within each round;
treat them as recorded high-water marks, not evidence that incremental policy
memory is zero. A fresh, lightweight harness sampled `/proc/<pid>/status` VmHWM every 50 ms after exec. Its one-run observations follow; these are whole-process peaks, including loading, without confidence intervals. Timing is not inferred from these runs.

| Workload | Lazy peak MiB | Eager | Staffing | Partial | Combined |
| --- | --- | --- | --- | --- | --- |
| busy | 506.13 | 505.52 | 505.70 | 505.80 | 506.11 |
| mixed | 160.41 | 160.76 | 160.65 | 160.86 | 160.67 |
| small | 53.29 | 55.77 | 55.03 | 60.11 | 58.14 |

The lightweight small-workload peaks are substantially below the original harness high-water marks. Partial frontiers also add published-cache storage: small peak RSS rose from 53.29 MiB for lazy to 60.11 MiB for partial. The busy and mixed loading peaks obscure incremental field storage.

## Correctness and audit scope

Native macOS and Linux passed the engine invalidation/scheduling cases (14,599
assertions), randomized path-search cases (25,209 assertions) and shared resource/
building executor cases (140,338 assertions), including deliberately slow workers
and simulated thread-creation failure. Compatibility tests cover experiment
configuration, hiring hunger gates and save safety.

Linux comparisons covered all three checkpoints and all five policies at 0, 1, 2,
4 and 8 workers, plus auditing on/off: detailed traces, replay bytes, saves and
final checksums matched within each configuration. Nine policy/checkpoint
comparisons also matched those four outputs exactly between native macOS and
Linux. Partial and eager entity traces matched; combined and staffing entity
traces matched. Experiment-off entity traces matched the preserved pre-change
executable. These are finite verification continuations, not a universal proof.
Native macOS and Linux each additionally passed all 28 partial-policy save/resume checks across delays 2/4/8, every phase, and resumed worker counts 0/4. Other platforms were not executed.

Each of the fifteen checkpoint/policy fresh-oracle audits ran 512 ticks, separately
from timing. Comparisons hold published resource parents constant. The lazy
baseline also uses stale cached fields, so its discrepancy rate is an essential
control. Counts are decisions, not independent match samples; repeated decisions
by the same unit are correlated. These short audits do not establish p95 trip or
hiring-delay safety over the independent 10,000-tick cohort.

| Workload | Policy | Worse legal step / movement decisions | Changed hire / hiring decisions | Changed resource / resource decisions |
| --- | --- | --- | --- | --- |
| busy | lazy | 311 / 11,090 | 56 / 17,018 | 0 / 336 |
| busy | eager | 333 / 11,215 | 4 / 16,259 | 3 / 329 |
| busy | staffing | 365 / 11,226 | 7 / 16,132 | 2 / 327 |
| mixed | lazy | 11 / 1,974 | 51 / 37,535 | 1 / 89 |
| mixed | eager | 42 / 1,979 | 0 / 37,235 | 0 / 95 |
| mixed | staffing | 11 / 1,974 | 51 / 37,535 | 1 / 89 |
| small | lazy | 315 / 5,783 | 52 / 6,464 | 0 / 213 |
| small | eager | 281 / 5,748 | 48 / 6,038 | 0 / 209 |
| small | staffing | 305 / 5,687 | 36 / 6,260 | 0 / 215 |

Equal-cost movement alternatives are counted separately in the retained summaries, rather than classified as worse steps. Complete denominators, reachability failures, score distributions, rejection reasons and censored episodes are in `impact-summary.json`. These comparisons diagnose staleness; differences between already diverged matches do not establish causal delivery or hiring delay.

## Independent match outcomes

The predeclared cohort completed all 96 continuations: seeds 201–232, eleven
Maxima players on Oazis, a common experiment-off warmup to tick 16,000, followed
by 10,000 ticks under lazy, eager or staffing. No seed was excluded or stopped
early. Staffing was selected for this comparison from its timing/CPU compromise,
before examining the outcome results. Each seed, summed across its eleven teams,
is one paired statistical unit. These are 32 independent seeds on one map/AI
composition, not evidence covering every match type.

The baseline and eager final checksums also matched all 64 retained original
cohort controls. The new comparison reproduced the earlier eager delivery,
construction and starvation averages. Outcomes are separate from timing; concurrent
execution durations are not used as performance observations.

Delivered/harvested counters and construction completions are measured as deltas
from the same warm checkpoint. Delivery counts accepted resource units, rather
than merely arrival events. Construction includes new buildings and upgrades,
excluding repairs. Starvation is the recorded cause-specific death count.

| Outcome | Eager mean change (95% paired-seed CI) | Staffing mean change (95% paired-seed CI) |
| --- | --- | --- |
| Delivered resources | +0.47% [-0.81, +1.76] | +0.93% [-0.31, +2.17] |
| Harvested resources | +0.28% [-0.99, +1.54] | +0.83% [-0.35, +2.01] |
| Completed buildings/upgrades | +1.50% [-0.32, +3.31] | +1.49% [-0.05, +3.04] |
| Starvation deaths per match | +3.16 deaths [-3.89, +10.20] | -1.16 deaths [-6.87, +4.56] |

Staffing does not show a statistically established delivery or construction
improvement. Its delivery and construction intervals remain above the −2%
screening margin in this cohort. Starvation's absolute difference is inconclusive;
its interval permits a few additional deaths. Per-seed death percentage changes
are unstable when baseline death counts are small, so absolute death differences
are the primary survival summary.

This does not establish the p95 trip-duration or additional hiring-delay criteria.
The new hybrid has not had a full 10,000-tick fresh-oracle population audit or new
512-tick discrepancy-specific causal forks. The short audits, censored episodes
and older eager cases cannot fill that gap. Playtesting, topology fixtures and
stronger survival/tail attribution remain prerequisites for promotion.

## What the next algorithm needs to improve

The staffing classifier demonstrates that selective scheduling can retain some
parallelism without doing most of the eager work. Assigned staffing is still a
coarse proxy: hiring queries can be expensive even for an understaffed building,
and many nearby users can need only a small part of a field.

The next worthwhile search experiment should avoid both finishing whole walking
parents for round trips and rebuilding untouched frontier queues at publication.
That requires a coordinated, resumable walking/round-trip search that introduces
resource sources only when their walking costs are settled. It must certify queried
answers before using them and preserve the current propagation caps and tie rules.
That algorithm is not implemented here. Saved query history could subsequently
choose the execution policy using deterministic demand and coverage, rather than
wall time or available cores. Building type alone is a weaker classifier.

## Reproduction and review status

The code remains on the draft scheduled-building-gradient PR; nothing is promoted
or enabled by default. Integration with the newer upstream market-routing changes
remains pending; this measured branch has not been rebased onto that overlapping
work. Replay compatibility and simulation revision changes require coordinated
integration rather than reusing these numbers blindly.

Ignored `artifacts/building-gradient-hybrid/` retains specifications, exact commands,
seeds, source/fixture/binary hashes, paired timing rows, diagnostic CSVs, audit
summaries, correctness records, build logs and reproduction drivers. Full raw
remote evidence is retained on `devlaptop.local` under
`/home/bradley/glob2-building-gradient-20261004-9520/evidence/`. Original source,
original binaries and historical evidence were preserved. Completed busy replay
files were losslessly compressed and checked against their original byte hashes.

Main Linux comparison/outcome executable SHA256:
`01594ecc2518975826f79925e39ab9ab766937fb58de5990ae3f1437bac9f5d8`.
Instrumentation ablation executable SHA256:
`7f09b57b0931837a9a8e965765e497032125513eeb120b17ff69849ea295c1fe`.
