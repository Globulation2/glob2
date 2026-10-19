# Latest family model: threshold16/24/32 comparison

Only the threshold changes. All arms use the exact final1280 family experts and combined fallback, H128 targets, min8, publicationdelay8 and demanded-field agecap1024. The qualified16 executable is unchanged;24/32are private one-TU literal overrides, not deployable models.

| Threshold | CPU ms/measuredtick | Sim CPU s | Process CPU s | Wall s | Measuredticks | GrossTPS | MeasuredTPS | Periodicjobs | Lazyjobs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| threshold16 | 0.123805 | 8.113670 | 10.591171 | 11.083335 | 65536 | 6097.8 | 5913.0 | 19032 | 18044 |
| threshold24 | 0.115545 | 7.572336 | 10.046119 | 11.266401 | 65536 | 5998.7 | 5816.9 | 14044 | 15416 |
| threshold32 | 0.115885 | 7.594661 | 10.093859 | 11.224774 | 65536 | 6021.0 | 5838.5 | 12452 | 16932 |

Eacharm has8processes,65,536measuredticks and67,584totaladvancedticks. Jobs and processCPU/wall include loading and256settling ticks perprocess; simulation CPU excludes settling.

24saves6.67%simulationCPU/tick versus16 (pairedfour-repeat range6.05–7.24%);32saves6.40% (5.75–7.34%). Wall throughput did not improve.24and32are approximately tied forCPU in this small panel.

| State | Threshold | Movementqueries | Extra cost/query | Falseunreachable | Missedmove | Stall | Fresh-correctablestalls | Planning falseUNR/queries |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 4ffa | threshold16 | 1596 | 0.724 | 0.188% | 0.564% | 0.251% | 0 | 50/56251 |
| 4ffa | threshold24 | 1129 | 1.096 | 0.531% | 1.860% | 0.620% | 4 | 253/37676 |
| 4ffa | threshold32 | 1328 | 1.151 | 0.075% | 0.979% | 0.527% | 5 | 201/46576 |
| 8ffa | threshold16 | 1213 | 0.712 | 3.298% | 2.885% | 3.792% | 7 | 3/2129 |
| 8ffa | threshold24 | 1763 | 1.258 | 0.737% | 2.496% | 3.800% | 19 | 18/3205 |
| 8ffa | threshold32 | 1450 | 0.903 | 0.345% | 1.241% | 2.483% | 9 | 8/1565 |

The CPU reduction has a quality tradeoff. Both higher thresholds increase direction
extra cost in both states;32 improves the8FFA stall and false-unreachable rates,
while4FFA stalls increase. Trajectories and query denominators differ, so these
rates are not outcomes for a fixed set of movement queries. The diagnostic panels
contain all family-specific and planning outcomes separately; no stall penalty is
folded into the direction-cost target. A fresh-correctable stall means the live
selector stalled while the matched fresh selector would move. The remaining stalls
are not automatically proven congestion.

Four interleaved repeats on two previously examined175 saved states characterize
local spread, not generalization or a population confidence interval. Load,
thermal and power are retained percell; no isolated physical-core claim is made.
Markets/new-family queries are absent, so this cannot attribute cost to the shared
fallback. Model/header, canonical sources, existing cacheobjects and qualified16
executable hashes were unchanged before/after the private research builds.

No productionthreshold, coefficient, schema, source or version changed. Raw commands,
resultJSON, processCPU/wall, load, quality/workCSV and patch/linkinput proofs are
retained. No timing overlapped compilation. All observerON runs matched the timed
arm/state initial/final checksums; each timedrepeat ended identically.
