# Unified hospital capacity ablation

Maxima uses one hospital capacity target: `ceil(live warriors × beds per warrior)`.
The configurable integer `military.hospital_beds_per_warrior_percent` represents
that ratio (30/40/50/60 mean 0.3/0.4/0.5/0.6). The selected default is **60 (0.6 beds per warrior)**.
For 20 warriors, those settings request 6/8/10/12 beds.

Completed hospitals provide 2/5/7 beds. New construction and upgrades both work
toward this target. The planner credits the finished capacity of existing sites,
unobserved construction reservations, and reserved upgrades once, so an upgrade
does not trigger duplicate hospitals during its downtime. Existing hospitals are
not demolished when the army shrinks. Whole buildings/upgrades can overshoot the
target. Injury pressure still raises construction priority; it no longer creates
a competing capacity target. Other units can use the hospitals, but do not add to
the requested capacity under this warrior-based policy.

This target counts physical beds. Hospital tiers also heal at different speeds
(`timeToHealUnit` is 30/18/6), so equal bed counts need not provide equal healing
throughput. Reaching the target suppresses further hospital upgrades as well as
new buildings. The comparison therefore tests this complete unified policy,
including its changed mix of hospital tiers; it does not isolate physical bed
count from healing speed. Existing upgrade priorities select upgrades while the
target is unmet.

This replaces the configurable warrior threshold, units-per-hospital ratio and
eight-hospital cap, plus the separate one-per-eight and twice-wounded rules.
Ordinary builder availability, technology, placement and construction quotas
still determine whether and when the target is reached. Surplus-worker towers
remain exactly the adopted strategy from `b806198f5`.

## Results and decision

All **240 games completed: 48 paired cases × five policies**. The selected default
is **0.6**, following the full ablation and the maintainer's preference for stronger
defensive capacity. This is a practical policy choice, not a statistically proven
win-rate optimum.

| Policy | Wins / losses / unresolved | Pressure samples short of beds | Missing beds / 10 warriors | Mean peak deficit / game | Completed hospital resource units / game |
|---|---:|---:|---:|---:|---:|
| Former policy | 10 / 18 / 20 | 32.3% | 1.14 | 6.96 | 57.69 |
| 0.3 | 13 / 16 / 19 | 32.0% | 1.01 | 6.73 | 32.98 |
| 0.4 | 11 / 15 / 22 | 22.6% | 0.84 | 4.56 | 38.02 |
| 0.5 | 12 / 14 / 22 | 22.9% | 0.73 | 4.79 | 51.35 |
| **0.6** | **15 / 13 / 20** | **18.5%** | **0.57** | **3.90** | **62.06** |

Pressure columns average within games, then give games equal weight. Peak deficit
is each game's largest sampled deficit, averaged over all 48 games; unexposed
games contribute zero. These are capacity proxies, not healing queues.

At 0.6, the paired missing-bed difference against control is **−0.48 beds per ten
warriors**, unadjusted 95% CI **[−0.81, −0.16]**, across 46 jointly exposed cases.
The row-mean difference is larger because exposed cohorts differ. Mean peak
shortage falls from 6.96 to 3.90 beds. Completed hospital resource requirements
rise by 4.38 units/game, about **7.6%** relative to control; the paired 95% CI is
[−7.56, 16.31]. This proxy excludes unfinished construction and labour cost.

The 0.6 policy has five more wins than control, but its exact paired win test is
**p=0.0625 before correction, p=0.25 after Holm correction**. None of the four
primary win comparisons is significant. The pointwise t interval for its win
change excludes zero, but the prespecified exact, corrected test governs the
win-rate conclusion. Many large FFA games remain unresolved at the fixed horizon.

The cheaper alternative is 0.4: it improves the observed capacity measures while
using fewer construction resources. Moving from 0.5 to 0.6 costs 10.71 additional
resource units/game (unadjusted CI [1.80, 19.62]); the adjacent-ratio bed-deficit
and win intervals span zero. Thus the experiment supports the tradeoff but does
not establish that 0.6 beats 0.4 or 0.5 in win rate. 0.3 does not materially reduce
the observed shortage frequency. No additional adaptive rules are introduced.

See [the full tables and paired intervals](results/RESULTS.md), including map-size
strata, deaths, building losses and exploratory worker metrics. The broad result
supports more reliable bed availability; it does not prove fewer combat deaths
or better coverage/reachability of hospitals.

## Fixed comparison

`run.py` specifies 48 new independent map/seed cases, each played by the adopted
tower control and the four bed ratios: **240 games**. There are 24 small 128×128
cases (eight each against Nicowar, Cabino and four-player FFA) and 24 large
256×256 four-player FFA cases. Each size uses eight cases apiece from generators
15, 2 and 48. Starting seats rotate; every policy uses the same map and game seed
within a case. The final six large-map case groups run on Mac arm64, all five
policies together, from tick-zero saves generated by the frozen Linux binaries;
the other cases run on Linux x86_64. This scheduling choice was made to reduce wall time, without changing cases or
policies. Any redundant Linux attempts on those assigned cases are excluded; a
complete paired case uses one platform. A fresh Linux start and the Mac
loaded start for seed 4019, ratio 0.5 match every checksum through 4,096 ticks.
The tick cap is 60,000; no predicted winners or outcome-dependent
stopping are used.

The primary comparison is paired actual win fraction against control across all
48 cases. Unresolved games remain unresolved. Exact discordant-win tests use Holm
correction across four control comparisons. Paired Student-t intervals use games,
not individual ticks, as observations. Size strata and secondary metrics are
reported descriptively; their intervals are unadjusted. The experiment does not
establish small differences between adjacent ratios precisely.

At sampled visible colony pressure, the bed deficit is
`max(0, hurt warriors − operating hospital beds)`. Army size includes warriors
at home, away, in the field and inside buildings. Pressure is sampled every 512
ticks, using the same contact definition as the prior defense study. The wounded
count is team-wide; this is a capacity deficit proxy, not a waiting queue or a
measurement of hospital reachability. Workers and explorers also need care but
are excluded from this demand measure. Reported pressure averages weight games
equally; paired pressure comparisons include only cases exposed under both
policies, with the sample count shown. Policies can change which ticks qualify
as pressure and the armies present then; these are resulting-burden measures,
not a controlled comparison with identical incoming attacks.

The construction-cost proxy totals resource requirements of completed hospital
projects: 3 for a new basic hospital, 8 for either upgrade. It excludes unfinished
or destroyed-before-completion projects, repairs, travel and worker opportunity
cost. It is not a complete resource expenditure measurement. Worker/building
losses and completed construction are cumulative per game; survival time can
affect these totals. After
inspecting the small-map totals, two exploratory worker metrics were added:
combat deaths per million worker-ticks, and worker-time without a hospital target while
needing healing. These use the last 512-tick labour sample and deaths at that same
tick (so they omit at most 511 final ticks). They were not part of the original
analysis plan and are not confirmatory tests. Adjacent-ratio paired intervals
(0.4−0.3, 0.5−0.4, 0.6−0.5) are also supplementary, unadjusted comparisons;
the prespecified primary family remains the four win comparisons against control.

## Compatibility and validation

The saved state layout is unchanged. Its historical `budget.desired_hospitals`
slot remains readable, but live construction demand is recomputed from army size
and the ratio. A complete legacy saved hospital schema is migrated explicitly to
0.6 beds per warrior. Unknown, incomplete, duplicate and mixed schemas remain
errors. Old custom live strategy overlays must replace the three retired keys
with the new ratio. Pre-version-98 permissive save handling is retained.

With the provisional 0.5 default, Mac arm64 and Linux x86_64 produce byte-identical
detailed simulation checksums for the same old initial save through 24,576 ticks. The compact per-tick records,
original checksum-stream hashes, initial save and new final save are retained in
`compatibility/`. A repeat on the selected 0.6 default also matches all 24,576
ticks across Mac arm64 and Linux x86_64; `selected-60-*.tsv.gz` retains both
per-tick records. The old hospital schema loads with the selected 0.6 target. Windows was not tested. The isolated lifecycle regression
checks 300 ticks of early-game save/load continuation. A separate late-game
check saves at tick 24,576 and compares continuation through 32,768. It fails
for both the unchanged tower policy (first mismatch 24,591; 8,177 differing ticks)
and the unified 0.5 policy (first mismatch 24,578; 8,190 differing ticks).
`compatibility/` retains both checkpoints and per-tick comparisons. The control
failure establishes an existing limitation; the differing trajectories do not
rule out an additional issue in the candidate. The first candidate difference is Maxima worker 1162
(position/direction/movement); the control first differs in Cabino worker 69 and
building 18. These are diagnostic locations, not established root causes.
Full late-game continuation is **not verified**. All ablation games run continuously and are unaffected by this
save/resume check.

The implementation, director, labour, strategy and lifecycle native regressions
pass, including target rounding, duplicate-capacity prevention, pending upgrade
authorization and saved-schema migration. All 44 Python policy regressions pass. The broader Python discovery runs 62
tests: 60 pass and two are skipped for unavailable optional build artifacts.
The separate configuration runner supplies its required executable and passes all
14 configuration tests (with a 512-tick startup-test cap). It
exposed a pre-existing whitespace-sensitive assertion in the runtime contract
test; that assertion now accepts whitespace without changing its meaning.
This changes Maxima's decisions and pacing, not unit/building balance. Existing
recorded orders and simulation rules are unchanged; new games or resumed AI
sessions are expected to make different hospital decisions.

## Reproduction

The production changes belong to `ai/maxima-labour-economy`. The measurement lab
uses the prior frozen diagnostic integration `d1d94affe` (PRs #343/#344/#339),
the adopted tower patch, and this hospital policy patch. The control is the
previously frozen `towers-lazy` Linux binary. It runs with the old strategy data
in an isolated working directory because the headless CLI resolves its base file
from `data/maxima/base.strategy`. Candidate games use their recorded per-player
`--ai-param` override. Configuration values are checked again by `analyze.py`.

Two setup attempts were excluded: the old binary initially encountered the new
strategy schema, and its failed output directory then prevented a retry. Failed
or interrupted directories were archived separately before retry; only complete,
successful runs with verified configurations enter the analysis. No policy was
changed in response to interim outcomes. After freezing the experimental binary,
the production parser additionally rejects empty assignments before legacy-schema
migration. After reviewing the complete ablation, the default and migration of
legacy count schemas changed from the provisional 0.5 to selected 0.6. All ablation
candidates used explicit ratio settings, including the settings stored in Mac
tick-zero saves, so neither change alters their tested configurations. Telemetry adapters rename the hospital target to beds without
changing field positions. The tested source patch and binary hashes are retained
in `provenance/`.

Run `run.py` from the diagnostic lab root with the frozen binaries in
`artifacts/defense-study/binaries/`. Then run `analyze.py BATCH OUTPUT` in a Python
environment with SciPy. All seeds, commands and final configuration checks are
recorded with the evidence.

The retained `game-evidence.tar.xz` contains all 240 result/configuration records,
filtered verbatim telemetry, source-artifact hashes, and the generated maps.
Full raw logs and initial/final saves remain in the diagnostic lab under
`artifacts/defense-study/hospital-ratio/`; their compressed-file SHA-256 hashes
are in each exported `source-hashes.json`. The archive is sufficient to rerun
the reported analysis:

```sh
tar -xJf game-evidence.tar.xz
python3 analyze.py hospital-evidence reproduced-results
python3 report.py reproduced-results
```

The analysis environment used NumPy 2.5.3 and SciPy 1.18.1. `results/games.csv`
retains one row per game; `results/pressure-samples.csv.gz` retains the underlying
pressure observations; `results/summary.json` contains all paired estimates and
intervals. The concise tables are in [results/RESULTS.md](results/RESULTS.md).
`prepare-macos.py` and `run-macos.py` reproduce the six Mac case groups.
`provenance/coordinator.py` records the actual transfer/queue coordination,
including the run-specific paths and process ID; it is an audit record, not a
portable runner. An additional four-case Mac queue was attempted, then cancelled based on
measured runtime before any complete five-policy group finished. Those partial
attempts are excluded; their original Linux assignments were retained. The
cancellation record is in `provenance/`. No outcome was used for this decision. `run.py` can instead run the whole fixed design
on Linux. Per-game commands identify the actual execution path used here.
