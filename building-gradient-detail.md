# Scheduled building-gradient experiment

The pipeline is implemented and remains off by default. Four-tick refreshes reduce engine time consistently in the measured checkpoints. A separate 32-seed comparison finds no average delivery regression on the tested building-heavy setup. Completed trips are modestly longer in the smaller audited cohort, and the larger cohort clears the average construction screening margin; hiring tails remain uncertain. Promotion still requires reviewing the diagnostic continuations and playing the result.

The delivered feature includes the newer typed-terrain implementation. The older prototype measurements below retain their original source and binary hashes; they are not pooled with the new terrain-integrated study. A second, predeclared 32-seed cohort on the integrated candidate has completed, and the matching timing matrix is complete. Its final results and the promotion verdict are in building-gradient-report.md.

## Current candidate: independent 32-seed confirmation

![All 32 paired seeds and their averages](./scheduled-v138-economy-statistical-overview.png)

Seeds 201–232 were declared before running the study. Each uses Oazis with eleven Maxima players, warms with the feature off to tick 16,000, and forks for 10,000 ticks with off or four-tick scheduling/four workers. All 64 continuations completed. No impact auditing or performance claims are drawn from these runs. This cohort uses the terrain-integrated candidate and stays separate from the original 101–132 cohort.

| Measurement | Off mean per match | Delayed mean per match | Average paired change | 95% paired-seed interval |
|---|---:|---:|---:|---:|
| Delivered resource units | 7,743.84 | 7,775.34 | +0.47% | −0.81% to +1.76% |
| Gathered units, including markets | 7,725.69 | 7,743.56 | +0.28% | −0.99% to +1.54% |
| Completed new buildings plus upgrades | 213.84 | 216.84 | +1.50% | −0.32% to +3.31% |
| Completed new buildings | 146.81 | 148.66 | +1.40% | −0.37% to +3.17% |
| Completed upgrades | 67.03 | 68.19 | +1.92% | −1.48% to +5.32% |
| Starvation deaths | 37.09 | 40.25 | +3.16 deaths | −3.89 to +10.20 deaths |

The average delivery and construction intervals clear the proposed −2% screening margin on this setup. They do not establish an improvement, and this remains one map/AI composition. Nine of 32 seeds lose more than 2% of deliveries; eight lose more than 2% of combined construction. Repairs are excluded from construction and reported separately (12.19 versus 13.47 per match). Starvation remains uncertain and warrants review: the interval includes both fewer deaths and roughly ten additional deaths per match. Death comparisons use absolute differences because small baseline counts make percentages unstable. The primary relative throughput summaries give each seed equal weight; pooled delivery totals change by +0.41%.

The measured Linux binary is `9ec468fe5b96bdc67a3235b9f23e392350d6da02bec82e0ee455cc50d57c18dd`, source tree `04f28cf67c1949cf4b576a7c63b9b1b116a05bc6d65d5820ae7227804643a90d`. The final source tightens replay acceptance and refreshes browser fixtures; it keeps the measured simulation rules unchanged. The final native equivalence checks verify that distinction.

## Original prototype timing (before the terrain integration)

![Timing averages and the complete 32-seed delivery comparison](./scheduled-v135-final-statistical-overview.png)


Quiet Linux laptop, identical checkpoints and AI settings, hiring filter retained, frontier prepass removed, no impact auditing. There are 360 accepted measurements: three workloads × four delays (including off) × three worker counts × ten balanced repetitions, plus 36 warmups. Every timing round passed the background-load limit without a retry; the largest accepted background load was 0.30 CPU cores.

The workloads contain the following populations one tick after their starting checkpoints. Construction sites are reported separately from the engine's building count.

| Workload | Starting tick | Teams | Units | Buildings | Construction sites |
|---|---:|---:|---:|---:|---:|
| Busy | 16,000 | 11 | 808 | 175 | 27 |
| Mixed | 12,000 | 11 | 150 | 133 | 84 |
| Smaller | 30,000 | 2 | 432 | 61 | 13 |

Four ticks of delay with four workers:

| Workload | Average engine-time reduction | 95% paired-round interval | Average tick-loop CPU change | Median p95 tick, off → delayed |
|---|---:|---:|---:|---:|
| Busy | 12.98% | 12.48% to 13.47% | +25.17% | 4.662 → 3.280 ms |
| Mixed | 11.68% | 11.20% to 12.15% | +24.75% | 2.881 → 2.316 ms |
| Smaller | 11.61% | 10.97% to 12.26% | +0.94% | 0.763 → 0.624 ms |

All ten paired rounds improved in every workload. Intervals describe repeated runs of these checkpoints, not variation across arbitrary maps.

Average engine-time reductions across the complete matrix:

| Delay / workers | Busy | Mixed | Smaller |
|---|---:|---:|---:|
| 2 / 1 | 9.69% | −6.60% | 8.08% |
| 2 / 2 | 12.21% | 0.31% | 8.85% |
| 2 / 4 | 11.79% | 1.11% | 8.65% |
| 4 / 1 | 12.40% | 6.34% | 11.44% |
| 4 / 2 | 13.78% | 10.92% | 12.32% |
| 4 / 4 | 12.98% | 11.68% | 11.61% |
| 8 / 1 | 16.02% | 10.82% | 4.83% |
| 8 / 2 | 15.66% | 12.25% | 5.48% |
| 8 / 4 | 14.86% | 11.96% | 4.83% |

Four ticks is the most consistent delay across workload types. Two workers also perform well. Additional workers do not give a monotonic improvement.

Average tick-loop CPU occupancy rises from 1.10 to 1.58 cores in busy, 1.23 to 1.74 in mixed, and 1.09 to 1.25 in smaller. These are CPU-time/wall-time ratios over the measured tick loop, including worker threads. The scheduler opens useful overlap, but the engine still has substantial serial work; four gradient workers do not imply four cores remain occupied throughout a tick.

At four ticks/four workers, median total deadline wait over 2,048 ticks is 0.44 ms (busy), 11.66 ms (mixed), and 1.31 ms (smaller). The largest individual wait is 0.011 ms, 2.113 ms, and 0.447 ms respectively. The busy queue peaks at 43 bundles and 16.25 MiB of accounted buffers; all runs remain below the 64 MiB limit and use no synchronous overflow fallback. Its median p95 queue depth is 16 bundles and p95 buffer occupancy is 7.75 MiB.

The CPU column sums process CPU consumed during the tick loop, including background worker threads. Its 95% paired-round intervals are +24.54% to +25.79% (busy), +23.94% to +25.56% (mixed), and +0.21% to +1.66% (smaller). Including setup and save loading gives smaller process-wide increases of 20.83%, 20.63% and 0.83%; both measures remain in the summary. Background builds complete full fields where demand-driven searches could stop early. Snapshot and build CPU, coalescing, discarded results, queue distributions, worker variants and RSS are retained in the timing summary. Peak RSS is dominated by loading the same cached save; it does not establish that the pipeline adds no memory.

## Original prototype: separate 32-seed validation

Seeds 101–132 were declared before observing this cohort's results. Each runs the same Oazis map with eleven Maxima players, warms with the feature off to tick 16,000, then forks identical checkpoints for 10,000 ticks with the feature off or four-tick refreshes. All 32 pairs completed; no impact auditing was enabled. These are independent gameplay seeds on one map, not 32 different map designs.

| Measurement | Off mean per match | Delayed mean per match | Average paired change | 95% interval |
|---|---:|---:|---:|---:|
| Delivered resource units | 7,918.22 | 8,008.50 | +1.17% | −0.02% to +2.35% |
| Gathered units, including markets | 7,887.78 | 7,908.25 | +0.27% | −0.89% to +1.43% |
| Completed buildings plus upgrades | 216.94 | 222.41 | +2.58% | +0.99% to +4.17% |
| Completed new buildings | 148.63 | 150.34 | +1.17% | −0.44% to +2.77% |
| Completed upgrades | 68.31 | 72.06 | +6.06% | +2.71% to +9.41% |
| Starvation deaths | 37.44 | 32.94 | −4.50 deaths | −10.03 to +1.03 deaths |

The delivered-unit interval clears the −2% screening margin for this setup. It does not establish a positive delivery benefit, or generalize to other maps or AI mixes. Construction counters from the same retained runs also clear the average −2% margin: completed new buildings plus upgrades improve by 2.58%, and new buildings alone have a lower interval bound of −0.44%. Repairs and virtual flags are excluded from construction throughput. Most of the combined increase comes from upgrades. These construction summaries were added after examining the retained delivery results and remain exploratory. Pooled delivered units improve by 1.14%; the equal-weight paired-seed change above is the primary summary. Deaths use absolute paired changes because percentage changes over small baseline counts are unstable.

Across seeds, delivery changes range from −4.75% to +6.17%, with a standard deviation of 3.28 percentage points. 7 of 32 pairs lose more than 2%, even though the average clears that margin. The average result does not imply every match improves.

## Movement, hiring and construction: audited four-seed cohort

Seeds 19, 23, 47 and 83 share the building-heavy Oazis/eleven-Maxima setup. Each checkpoint was audited for 10,000 ticks with off/2/4/8 delays. Mixed and smaller checkpoints were also audited, but stay separate from the comparable seed cohort. The smaller checkpoint also uses seed 19 and is not counted as another independent seed.

Four ticks versus off, equal weight per seed:

| Measurement | Average paired change | Exploratory 95% interval |
|---|---:|---:|
| Delivered resource units | −0.06% | −7.91% to +7.79% |
| Construction completions | −1.68% | −8.22% to +4.86% |
| Completed-trip mean duration | +1.20% / +4.44 ticks | +0.08% to +2.31% |
| Completed-trip mean distance | +1.40% / +0.22 tiles | +0.37% to +2.44% |
| Completed-trip p95 duration | +0.44% | −3.98% to +4.86% |
| Unfilled staffing slot-ticks | +1.29% | −3.10% to +5.69% |
| Hungry unit-ticks | +0.82% | −3.73% to +5.38% |

Mean trip duration and distance increase modestly across these four seeds. The construction interval in this smaller cohort does not clear the −2% screening margin; the separate 32-seed cohort above does. This cohort and the 32-seed validation remain separate; their samples are not pooled after observing results.

Published-versus-current-oracle decision discrepancies exist with the feature off too. Average fractions across the four seeds:

| Decision discrepancy | Off | Four ticks |
|---|---:|---:|
| Changed movement direction | 5.95% | 6.20% |
| Worse weighted movement step | 2.57% | 2.59% |
| No hire where fresh evaluation would hire | 0.091% | 0.073% |
| Changed requested resource type | 0.628% | 0.403% |
| Changed predicted resource destination | 13.58% | 9.64% |

Changed directions include equal-cost alternatives. The mean increase in worse-step fraction is 0.021 percentage points, with an interval from −0.128 to +0.169 points. Counts, denominators, reachability failures, scores, rejection reasons, market identity, actual map harvests and market pickups remain in the machine-readable report.

Completed-trip distributions exclude trips whose starts predate the checkpoint. Abandoned, removed and unfinished trips are retained separately, as are missed hiring opportunities that end because a candidate becomes unavailable or demand disappears. Their elapsed observations are lower bounds, not exact additional hiring delays. Completed-only p95 values do not prove that censored trips would have acceptable tails.

Hiring tails remain unresolved in the four-seed audit. Off and four-tick runs each complete only 23 tracked missed-hire episodes; 78 and 77 additional episodes are censored. The mean of each seed's observed p95 falls from 36 to 23.5 ticks, but the paired change's 95% interval spans −44.6 to +19.6 ticks. Those small, censored samples cannot clear the eight-tick hiring screen or establish a population improvement.

All intervals use independent seeded matches or paired timing rounds. Decision events and trips within a match do not inflate the sample count. These are exploratory intervals across multiple endpoints, not a multiplicity-adjusted confirmation of every individual effect. Whole-match outcomes after divergence measure screening differences, not the cause of a particular trip or hire. The measured candidate also includes the gated portable pathfinder heap repair described below, so whole-match comparisons measure that combined candidate. Paired decision interventions keep the repair active in both continuations and isolate the fresh-field substitution.

## Paired diagnostic continuations

The first up to twenty discrepancies per category and delay retain 1,630 case records across six workloads. Each forks the same initial checkpoint and inputs, checks identical simulation state before the identified decision, substitutes current fresh building fields in one continuation, and observes up to 512 subsequent ticks. Cases join unit/building lifetimes and retain original opportunity censor boundaries.

All 1,630 records are complete, covering 1,393 distinct interventions. There are 464 verified late-checkpoint replays; seven unsuitable older checkpoints were rejected and replayed from the common workload checkpoint. Previously complete cases retain their original proofs. Remaining cases use the corrected private format-137 checkpoint writer, with every resumed simulation state and pre-decision observation checked against the frozen reference. Independent decision groups run in separate output directories and merge only after checking the exact original inventory.

These first-discrepancy cases are a convenience diagnostic sample. Their averages describe selected discrepancies, not population effects or the incremental penalty of enabling the pipeline. Normal behavior is compared with fresh fields substituted at that decision; the feature-off baseline also has stale decisions.

Selected no-hire discrepancies across all six workloads:

| Delay | Both branches hired the candidate | Censored pairs | Mean additional normal wait | Median | Observed p95 |
|---|---:|---:|---:|---:|---:|
| 2 | 45 / 120 | 75 / 120 | 71.73 ticks | 43 ticks | 266 ticks |
| 4 | 50 / 120 | 70 / 120 | 73.00 ticks | 23 ticks | 254 ticks |
| 8 | 29 / 120 | 91 / 120 | 81.69 ticks | 31 ticks | 312 ticks |

Exact-delay distributions exclude pairs where either original opportunity ended without the corresponding hire. Candidate unavailability, demand disappearance, removal and the finite observation horizon remain censored, with reasons retained. Selected hiring tails exceed the eight-tick review threshold. These conditional results warrant review but do not estimate typical hiring delay or establish that scheduling worsens hiring relative to the feature-off baseline. The independent four-seed hiring-tail comparison remains uncertain.

The complete machine-readable appendix also retains acquisition/delivery differences, map harvesting versus markets, trip distance/duration/reversal changes, both-side completion denominators and unfinished outcomes by category and workload. No confidence interval treats these selected events as independent seeded matches.

## Implementation and verification

The Map-owned runtime separates immutable capture, pure construction, deterministic fixed deadlines and simulation-thread publication. It uses the shared asynchronous executor; first construction stays synchronous. Building lifetimes, dirty generations, bundle coalescing, bounded buffers and private overflow spools are explicit. Saving can finish private jobs without early publication. The saved experiment and delay remain off/four by default; delays two and eight are accepted alternatives.

Save version 138 preserves the minimum floor of 58 and gates the extensions. Formats 137 and 138 also repair two independently reproduced pre-existing Maxima save omissions: frozen wood reservations and resolved director budgets. Replay acceptance begins at 138, network protocol is 56, and SIM revision 15 plus the golden record are updated. The replay gate accounts for the saved delay rule also appearing in script observations when the feature is off. The obsolete frontier prototype is removed.

Verification evidence:

- Seventy-two matching worker/audit configurations on each of native macOS and Linux: per-tick state traces, replays and saved bytes match across workers 0/1/2/4/8 and across platforms.
- Twenty-eight save continuations per platform cover every phase of the two/four/eight-tick delay with zero/four workers. Checkpoint hashes also match between platforms.
- All twenty-four 10,000-tick audits have the same final checksum, team state and player state as matching nonaudit runs on the other platform.
- A complete 10,000-tick small/delay-two Mac/Linux trace matches after repairing an existing decreased-key pathfinder heap defect exposed by the experiment. A shortest-path oracle test covers 128 obstructed maps. That repair was gated in the original measured prototype; the newer terrain base already includes the immutable-priority-queue repair in its reference behavior.
- Slow workers, thread-creation failure, simultaneous deadlines, invalidation, identity reuse, deletion/conversion, eviction, cold fields during pending work, overflow, legacy metadata and controlled movement/resource/market/hunger fixtures pass focused native tests.
- Terrain-integrated focused tests cover typed terrain, every swim class, slow workers, creation failure, overflow, dirty state, destination invalidation and Maxima save continuity. All focused feature cases pass on macOS and Linux.
- Initial full integrated suites: macOS 1,452 passed, 118 skipped and three failures; two stale replay/network assertions were corrected and their retests pass. Linux 1,453 passed, 116 skipped and two failures. Remaining failures concern external SDK coverage: both SDKs lack the current 16-bit PNG decoder patch, and Linux has the existing bundled SDL3_ttf kerning failure. No gradient feature test fails.
- Final replay/save/golden boundary tests: macOS 24 jobs passed, zero failures, one display skip (32 reported cases); the Linux run is retained separately. All final 72 worker/audit configurations and all 28 publication-phase continuations per platform exactly match the previously validated native builds, including per-tick state traces, replay bytes, save bytes and checkpoint hashes. The validated native source tree is `cc4ec2d5b8f6b246dcfa8c91a67ce8ed7af04cbc3d5565436f876b27f84b9d69`; subsequent documentation and offline event-filter changes yield final tree `84e1ab28c75de5b833e0f541311a534efd89720e1a4ada79bee9217f6b4aece4` on both platforms, without changing simulation or main executable code. Final native boundary harnesses were rebuilt: 21 passed, zero failed, one display skip on each platform.
- Python interpretation/statistics tests: 27 passed. Protocol generation/type checks, focused protocol tests and simulation-version checks pass. The full platform JavaScript suite needs its unavailable Postgres service and includes Linux sandbox tests not applicable to macOS; it is not reported as green.
- Browser replay and Studio fixtures were regenerated for 138. Native Linux Studio matches the macOS reference byte for byte (1,849,612 bytes, SHA-256 `f01db26d35949e2685ce3a1544848342a4ba9ac5a2106e5f8ab330ba1852f422`). Actual browser, Windows and Android execution were not performed.

The final working tree is based on `79c8d65f52cfea0a91efa3c104f97d79fd5de54d`. Fetched master `ee67fd5f3` advances unrelated components and refreshes the old browser fixtures; the new 138 fixtures supersede that refresh. Localization overlaps were checked without conflicts. The original pre-terrain study is based on `8bc1b897ff7b30b094d35dd86c3230828988c457`. Frozen source archives, patches, binary hashes and commands distinguish these versions. No commit or PR has been created.

## Evidence locations

Under `artifacts/building-gradient-experiment/`:

- `scheduled-v135-final-source.json`, source patch/archive and original timing-driver copy identify source, binary hashes and commands.
- `scheduled-v135-final-timing-summary.json` and `scheduled-v135-final-timing-linux/` contain all timing variants and raw runs.
- `scheduled-v135-final-statistical-report.json` contains the original cohort's decision/outcome distributions and seed statistics.
- `scheduled-v135-final-construction-validation-statistics.json` summarizes all 32 seeds’ completed new buildings, upgrades and repairs separately.
- `scheduled-v135-final-economy-validation-spec.json`, `scheduled-v135-final-economy-validation/` and `scheduled-v135-final-economy-validation-statistics.json` retain the 32-seed cohort declaration, fixtures, counter baselines, all paired results and statistics.
- `scheduled-v135-final-audit-mac/` contains six workload audits and paired-case metadata. Large heavy-workload continuation files remain at `devlaptop.local:/home/bradley/glob2-building-gradient-20261004-9520/evidence/scheduled-v135-final-counterfactual-linux/`.
- Cross-platform, continuation, long audit/nonaudit, focused/full test and controlled-fixture evidence use the same final prefix.

The original timing pause was restored. The current timing coordinator finished all 360 measurements plus 36 warmups and restored all four identified background jobs in its finalizer. Gameplay runs under load are not used for speed claims.
