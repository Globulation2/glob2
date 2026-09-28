# Permanent wheat versus master

128 paired starts: 16 map families × four map seeds × two orientations. Runs capped at 32,768 ticks. Food and population measurements use each pair’s common observed window. CPU uses whole-run process CPU per tick. Confidence intervals bootstrap whole map families (5,000 draws); rotations are not treated as independent samples. These retained maps are exploratory, not a new holdout.

| Metric | Change | 95% interval |
|---|---:|---:|
| harvest | -14.9% | -21.4% to -8.6% |
| meals | -10.4% | -17.6% to -3.8% |
| population_time | -12.1% | -18.9% to -6.5% |
| cpu_per_tick | -9.8% | -15.6% to -4.3% |
| starvation_rate | +2.2% | -23.3% to +21.1% |

Crowded save: 46.200s master → 43.740s permanent (-5.3%), mean of four matched physical-core repetitions of 8,192 ticks.

All machine-level commands, map seeds and binary hashes are retained alongside this report. CPU differences include altered game trajectories/population, not only planner overhead. An unrelated build overlapped devlaptop tournament runs, so CPU is also reported separately by host in summary.json. The initial devlaptop isolated CPU attempt is excluded; the reported crowded-save test ran on therig after its tournament. This measures removing maturity from current master; it does not isolate the benefits of expansion support or wood reserves, which both versions contain.

Windows parity and human gameplay review are not verified.
