# Primary building-gradient sweep analysis

This analysis measures the CPU/speed tradeoff as prebuild budget, publication delay and worker count change. The original synchronous lazy implementation is a reference point on that curve. All 81 scheduled configurations have lower equal-family mean ticks per second and higher engine CPU per tick than that reference in this sweep. The extra late-game cohort was skipped at the user’s request; its failed checkpoint loads are retained and excluded.

## Method

24 independent evaluation matches, four per family, split evenly between macOS M3 and Linux Ryzen 7 6800H. Six families cover large economy, large generated economy, mixed islands, mixed town, small economy, and contested town. Each match ran all 81 configurations twice, with original and experiment-off controls bracketing each block: 5,616 process runs. Models were trained on 18 separate matches, then frozen.

Policies: scheduled zero expansion, p25/p50/p65/p80/p90/p95/p99 predicted extension cost, and full field; publication delay 2/4/8 ticks; workers 1/2/4. A percentile is a predicted request-cost budget, not a percentage of map tiles or guaranteed query coverage. First construction remains synchronous and demand driven.

Comparisons pair within match, host and worker count; repeated timings are averaged before each family receives equal weight. Intervals are pointwise 95% bootstrap intervals from 10,000 whole-match draws, stratified by family and host. Only two independent matches per family per host limit interval precision. They are not simultaneous confidence intervals and do not correct for selecting the fastest of 81 points.

## Results

| Policy | Best measured delay | Workers | TPS change | Engine CPU change |
|---|---:|---:|---:|---:|
| Scheduled zero | 8 | 2 | -5.15% | +13.63% |
| p25 | 4 | 4 | -5.23% | +15.09% |
| p50 | 8 | 2 | -5.02% | +14.14% |
| p80 | 4 | 4 | -4.69% | +16.47% |
| p95 | 4 | 4 | -4.51% | +19.13% |
| Full | 8 | 4 | -2.94% | +22.41% |

Rows select the fastest setting for each policy after measurement; these are exploratory, not independently confirmed optima.

Fastest overall: full field, delay 8, workers 4. TPS -2.94% (pointwise 95% interval -4.52% to -1.35%); engine CPU +22.41% (interval +20.18% to +24.60%). Total process CPU +18.53%. Relative to the candidate with scheduling disabled: TPS -1.02% and engine CPU +21.97%.

| Scenario family, fastest overall setting | TPS change | CPU change |
|---|---:|---:|
| contested-town | -2.69% | +5.70% |
| large-economy | -8.49% | +44.63% |
| large-generated-economy | -2.20% | +20.63% |
| mixed-islands | +1.93% | +30.00% |
| mixed-town | -6.59% | +27.51% |
| small-economy | +0.43% | +5.99% |

For that setting, macOS averages +3.13% TPS and Linux -9.00%. Hosts ran different seeds, so this is not a controlled hardware comparison.

## Gameplay and measurement limits

At the fastest overall setting, pooled baseline counters over the 24 windows were 6,838 wheat deliveries, 281 construction completions and 13 starvation deaths. Differences were -43 deliveries (-0.63%), +7 completions (+2.49%) and zero additional starvation deaths. These pooled counts weight active economies more heavily than the performance macro-average; whole-match divergence does not establish causal hiring or delivery delays. They do not establish gameplay equivalence.

All timing runs had gradient instrumentation and impact auditing disabled. Audit-derived trip duration, missed hiring delay, per-decision staleness, deadline waits and causal counterfactual results are not part of this completed timing sweep. Disabled counters cannot establish zero waits. Tick p95 values are coarse doubling-histogram bounds, not exact percentiles. RSS and process CPU include more than the scheduler itself.

Each window covers 2,048 ticks from a loaded save with no excluded warmup, so cache cold start is included. The original and candidate receive the same checkpoint within each match; warm continuous-game behavior remains unmeasured by these timing windows. Existing published caches and first constructions interact with this load boundary. No extremely late-game inference is made from the skipped cohort.

## Interpretation

Increasing prebuild coverage often recovers some speed relative to low-budget scheduling, while consuming more CPU. No tested partial policy improves the overall CPU/speed tradeoff over synchronous lazy computation. This result supports keeping the feature off by default and the PR in draft. It does not prove that every possible adaptive policy or scheduler design must lose. The comparison does not identify how much overhead belongs to snapshots, propagation, invalidation or executor scheduling; that requires separate profiling.

## Evidence

Frozen candidate source: 3fb7141d5; original greedy source: ab143f583b16c4cfdab32c9fc4abaa6200867a07. Commands, binary/checkpoint/model hashes, process statistics and results remain in mac/ and linux/ measurement.json.gz files. Protocol and analysis scripts are retained beside this report. primary-summary.json contains all 81 points, per-match, family and host results. Native focused correctness and checksum tests were completed earlier; current master integration and other platform checks remain separate pending work.

Plots: primary-summary.png / .pdf and primary-summary-tradeoffs.png / .pdf.
