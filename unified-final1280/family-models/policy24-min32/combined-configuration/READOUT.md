# Selected threshold24 and minimum32: measured comparison

All three policies use the same latest final1280 family expert and combined
fallback coefficient tables. H128 labels, publication delay8 and demanded-field
age cap1024 are fixed. The only selected policy changes are threshold and minimum
refresh spacing; the two controls are frozen16/min8 and24/min8 binaries.

| Policy | CPU ms/measured tick | Sim CPU s | Process CPU s | Wall s | Measured ticks | Gross ticks/s | Measured ticks/s | Periodic jobs | Lazy jobs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| threshold16_minimum8 | 0.124739 | 8.174896 | 10.669195 | 11.197933 | 65536 | 6035.4 | 5852.5 | 19032 | 18044 |
| threshold24_minimum8 | 0.117996 | 7.732998 | 10.231164 | 10.980071 | 65536 | 6155.2 | 5968.6 | 14044 | 15416 |
| threshold24_minimum32 | 0.119724 | 7.846263 | 10.372639 | 11.601683 | 65536 | 5825.4 | 5648.8 | 12636 | 17172 |

Each arm advanced67,584 ticks in eight processes, including256settling ticks per
process;65,536ticks contributed to measured simulationCPU. Jobs, processCPU and
wall include loading and settling. SimulationCPU excludes settling.

Selected24/min32 saves4.02% CPU/tick versus16/min8, with paired four-repeat range
2.19–6.20% saved. It costs1.46% more than24/min8, paired range−1.26% to+3.95%.
Periodic jobs decrease, but lazy jobs increase versus24/min8. Whole-engine compute
and trajectories therefore do not change monotonically with refresh spacing.
Wall throughput did not improve in this panel.

| State | Policy | Movement queries | Extra cost/query | False unreachable | Missed move | Stall | Fresh-correctable stalls | Planning falseUNR/queries |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 4ffa | threshold16_minimum8 | 1596 | 0.724 | 0.188% | 0.564% | 0.251% | 0 | 50/56251 |
| 4ffa | threshold24_minimum8 | 1129 | 1.096 | 0.531% | 1.860% | 0.620% | 4 | 253/37676 |
| 4ffa | threshold24_minimum32 | 1407 | 1.169 | 0.498% | 1.493% | 0.498% | 4 | 104/33650 |
| 8ffa | threshold16_minimum8 | 1213 | 0.712 | 3.298% | 2.885% | 3.792% | 7 | 3/2129 |
| 8ffa | threshold24_minimum8 | 1763 | 1.258 | 0.737% | 2.496% | 3.800% | 19 | 18/3205 |
| 8ffa | threshold24_minimum32 | 1461 | 0.801 | 0.616% | 2.327% | 1.985% | 16 | 4/2649 |

The quality tradeoff is mixed. The selected policy has lower8FFA stall and false-
unreachable rates than16/min8, while direction extra cost is higher in both worlds.
Correctable stalls, total stalls and planning errors are reported separately.
A fresh-correctable stall means the live selector stalled while the matched fresh
selector would move. Remaining stalls are not automatically proven congestion.
Query denominators and trajectories differ; these are not fixed-population causal
outcomes. Markets/new-family support is absent, so no shared-fallback cost is
attributed. No individual-counter or model-head causal claim is made.

CPU uses four balanced interleaved repeats on each of two previously examined175
saved worlds. These characterize local spread, not generalization or a population
confidence interval. Process, load, thermal and power observations are retained;
no uncontended physical-core claim is made. No timing overlapped compilation.
All repeats and observerON runs ended with identical per-policy/world checksums.

The earlier direct master comparison measured16/min8. It remains separate and is
not relabeled as a direct master comparison of24/min32. The preceding16/24/32,
minimum8 threshold-only sweep also remains separately identified.

PLAN.json binds exact source, binary and input hashes. STATUS.json retains every
raw process measurement. SUMMARY.json includes per-family quality, planning,
work reasons, field cells and paired timing spreads. No refit or new collection
was performed. The production policy is root-selected24/min32; prototype24/min8
is a research-only literal override whose generated baseline model remained16.
