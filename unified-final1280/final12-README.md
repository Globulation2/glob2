# Final1280 twelve-feature prospective research check

Final7 was rejected for a late guard-field starvation episode. This already-fitted12-feature candidate resolves that failure on the same saved states. No refit or scheduling override was used.

| Arm | Simulation CPU ms/tick | Process CPU s | Wall s |
|---|---:|---:|---:|
| final12 | 0.125927 | 10.706 | 11.185 |
| gen005_reference | 0.117819 | 10.190 | 10.784 |
| master_reference | 0.150035 | 12.321 | 12.859 |

Across24OFFruns, final12 saves16.07% simulation CPU versus master, while costing6.88% more than accepted gen005. Per-repeat gen005 differences span+1.32% to+11.66%; this small two-state panel has noticeable variation.

| State | Arm | Queries | Direction extra/query | Stall rate | Correctable stalls | False unreachable | Missed moves |
|---|---|---:|---:|---:|---:|---:|---:|
| 4ffa | final12 | 1219 | 0.3782 | 0.492% | 1 | 0.082% | 0.574% |
| 4ffa | gen005_reference | 1118 | 0.7862 | 0.626% | 5 | 0.089% | 0.805% |
| 8ffa | final12 | 1783 | 0.8817 | 4.150% | 20 | 0.337% | 1.851% |
| 8ffa | gen005_reference | 1151 | 1.2181 | 2.606% | 10 | 0.261% | 1.651% |

Direction cost falls on both states, but8FFA stall rate increases. Of74new stalls,20would move with the fresh selector;54also fail under the fresh selector. The accepted run has30stalls,10correctable. The query/game populations differ, so these counts are descriptive rather than paired causal effects.

Final12 is a measured tradeoff, not evidence of adaptive dominance or proof that any particular extra input caused the difference.

Two previouslyexamined175states;notindependentgeneralizationsample
4repeatscharacterizelocalspreadnotpopulationCI
CPUnoobservers/tracing, commonassets/dependencies;noisolatedphysicalcoreclaim
Gen005comparisonusesdifferentmodelcoefficientsandcleanup,nocountercausalityclaim
Qualityquerypopulationsdiverge;directionextraobjectiveomitscachedunreachablemissedmoveevents
Final7rejectedproofpreservedseparately; final12correctslateguardstarvation but8FFAstallratestillhigher
