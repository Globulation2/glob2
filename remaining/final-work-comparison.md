# Fixed-input final resource outcomes

Each run starts from the same saved fixture and scripted orders, then advances to tick1,024. Current master uses immediate growth; the retained branch uses snapshot growth with delay8/shared4. These different trajectories preclude treating the master timing comparison as identical work.

| Scenario | Master deposits | Retained deposits | Master total material stock | Retained total material stock | Stock difference |
|---|---:|---:|---:|---:|---:|
| dense | 50214 | 50483 | 191549 | 191475 | -74 (-0.04%) |
| multi | 241039 | 236522 | 1894743 | 1743866 | -150877 (-7.96%) |
| ai512 | 24712 | 24691 | 79157 | 79118 | -39 (-0.05%) |
| disabled512 | 23755 | 23755 | 59428 | 59428 | +0 (+0.00%) |
| sparse | 1314 | 1055 | 4132 | 3309 | -823 (-19.92%) |
| saturated | 55775 | 55828 | 228574 | 227510 | -1064 (-0.47%) |
| harvested | 44354 | 44607 | 155178 | 155492 | +314 (+0.20%) |
| fragmented | 117648 | 104116 | 740585 | 590352 | -150233 (-20.29%) |

Material-by-material stocks and team growth-statistic arrays are preserved in [retained-work/results.json](./retained-work/results.json). Playable final saves and their inspection commands/logs are in each scenario subdirectory. These are fixed-seed outcomes, not a claim about long-run ecological equivalence.
