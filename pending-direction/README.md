# Direction fallback accuracy while building gradients are missing

Accuracy-only experiment; production PR 1197 remains at d1c0d4d50109b6f7d30f843368d30406f515b84f. No heuristic movement was added to production or to the simulated matches.

## Workload and validation

64 completed games, 2,069,746 actual ticks, 304,744 sampled pending route queries. Fixed Holiday Island 2 (128×128, four teams) and Oazis (256×256, eleven teams), 32 game seeds 42–73 per map. 32,768-tick cap; games may finish earlier. AI order Nicowar, Econo, Castor, Warrush, repeated/truncated to eleven teams. Four compute participants; two matches at a time. Current PR uses its own global four map admissions per tick; the separate per-team proposal is not folded into this workload.

All 64 initial/final checksums and actual tick counts match the previously recorded baseline games. Both 2,048-tick seed-42 pilots match per-tick world and entity checksum traces against the frozen unprobed binary and against the current PR's native development executable. Long-run comparison is at endpoints, not every tick. Runtime attribution mode `baseline` suppresses the old speculative creation requests and retains primary hiring preparation. Its source ancestry is documented in experiment.json; pilot checks establish equivalence to the current demand-driven PR on those inputs.

Apple M3, macOS arm64. Release build with existing dependencies, `PYTHONPATH=. scons release=1 CXXFLAGS=-g0 client_profile=full server=0 -j4`. Private instrumentation patch and its three untracked headers are attached; applying the patch to its recorded benchmark-checkout head and restoring the headers reconstructs the diagnostic source. Scripts and frozen binary SHA are attached. Analysis uses repository-pinned NumPy 2.4.6 on Python 3.13. This run has no CPU/tick, jitter or device-performance interpretation: oracle work is substantial and matches run concurrently.

## What is measured

At a pending query, identify the ground unit actually occupying the query cell and matching its swim class. Compute a private complete building gradient from the current map and the destination's current footprint/range, using the normal seeder and Dijkstra implementation. The private field is never published, never touches a live field or assignment, and never consumes game RNG. Fields can be reused only within a tick with matching tracked map generations, identity and slot epoch. These are fresh current-world route costs, not a prediction of the later queue capture or publication after the world changes.

The direction rule copies the ordinary unconstrained flyer heading exactly: wrap displacement around the map, point toward the building centre (flag position for flags), then round to eight directions with the existing 2.41 threshold. Every proposed step must pass ground terrain and collision rules, including forbidden areas, buildings, resources and other units.

- Direct: try the heading, wait when blocked.
- Flyer turns: if that heading is blocked, try offsets +1, -1, +2, -2, +3, -3, +4 in the existing flyer order, using ground collisions. Wait if no step is legal.

No policy sees the oracle when choosing its direction. A shorter remaining route is useful even if another direction is better. Best-available agreement accepts every tie under the normal gradient neighbour score, rather than requiring one arbitrarily tie-broken direction.

Some current cells are excluded from the gradient (e.g. forbidden cells or standing on resource/other-building cells). For these, compare against the shortest legal reachable exit, counting the step to reach it. They remain in the overall figures and have a separate table. Origins already at a route goal are excluded from movement percentages. Potential choices without a reachable origin/exit remain in the denominator. All rates use actual pending query opportunities; they do not claim a percentage of all movement ticks.

## Assigned-unit movement requests

| Map | Policy | Requests | Legal step | Shorter remaining route | Longer remaining route | Best available route step |
|---|---|---:|---:|---:|---:|---:|
| holiday | Direct / wait if blocked | 13,283 | 82.21% | 80.23% | 1.26% | 71.72% |
| holiday | Flyer turns / ground collisions | 13,283 | 99.95% | 94.96% | 3.70% | 84.24% |
| large-eleven | Direct / wait if blocked | 121,333 | 84.17% | 83.01% | 0.81% | 75.10% |
| large-eleven | Flyer turns / ground collisions | 121,333 | 99.96% | 96.34% | 2.91% | 86.70% |

Rates are request-weighted. summary.json also contains means of the 32 per-game rates and 95% intervals from 10,000 whole-game bootstrap resamples. Calls from one game are not treated as independent statistical trials. Stopped, blocked, unchanged-distance and off-route categories are included in that file, so legal-step rates are not confused with useful movement rates.

## Repeated locations counted once

For each game, retain only the first request for each unit identity, building identity, route, swim class and coordinate. This reduces repeated waiting at one spot.

| Map | Policy | Requests | Legal step | Shorter remaining route | Longer remaining route | Best available route step |
|---|---|---:|---:|---:|---:|---:|
| holiday | Direct / wait if blocked | 11,937 | 82.33% | 80.34% | 1.27% | 71.81% |
| holiday | Flyer turns / ground collisions | 11,937 | 99.94% | 94.92% | 3.69% | 84.22% |
| large-eleven | Direct / wait if blocked | 70,261 | 83.07% | 81.84% | 0.83% | 73.76% |
| large-eleven | Flyer turns / ground collisions | 70,261 | 99.96% | 95.81% | 3.35% | 85.85% |

## Ordinary footprint routes

| Map | Policy | Requests | Legal step | Shorter remaining route | Longer remaining route | Best available route step |
|---|---|---:|---:|---:|---:|---:|
| holiday | Direct / wait if blocked | 9,453 | 78.13% | 75.99% | 1.18% | 67.79% |
| holiday | Flyer turns / ground collisions | 9,453 | 99.94% | 93.86% | 4.32% | 82.90% |
| large-eleven | Direct / wait if blocked | 42,122 | 76.98% | 75.26% | 1.03% | 66.76% |
| large-eleven | Flyer turns / ground collisions | 42,122 | 99.90% | 94.18% | 4.32% | 82.88% |

## Combat flag routes

| Map | Policy | Requests | Legal step | Shorter remaining route | Longer remaining route | Best available route step |
|---|---|---:|---:|---:|---:|---:|
| holiday | Direct / wait if blocked | 3,830 | 92.27% | 90.70% | 1.46% | 81.41% |
| holiday | Flyer turns / ground collisions | 3,830 | 99.97% | 97.68% | 2.17% | 87.52% |
| large-eleven | Direct / wait if blocked | 79,211 | 87.99% | 87.12% | 0.69% | 79.54% |
| large-eleven | Flyer turns / ground collisions | 79,211 | 99.99% | 97.49% | 2.16% | 88.73% |

## Open and excluded origin cells

Open origin:

| Map | Policy | Requests | Legal step | Shorter remaining route | Longer remaining route | Best available route step |
|---|---|---:|---:|---:|---:|---:|
| holiday | Direct / wait if blocked | 11,011 | 82.05% | 80.50% | 1.32% | 71.98% |
| holiday | Flyer turns / ground collisions | 11,011 | 99.97% | 95.52% | 3.81% | 84.77% |
| large-eleven | Direct / wait if blocked | 67,503 | 83.20% | 82.15% | 0.81% | 74.04% |
| large-eleven | Flyer turns / ground collisions | 67,503 | 99.98% | 96.08% | 3.34% | 86.11% |

Excluded origin / exit assessment:

| Map | Policy | Requests | Legal step | Shorter remaining route | Longer remaining route | Best available route step |
|---|---|---:|---:|---:|---:|---:|
| holiday | Direct / wait if blocked | 2,184 | 83.70% | 82.10% | 1.05% | 73.26% |
| holiday | Flyer turns / ground collisions | 2,184 | 100.00% | 95.97% | 3.25% | 84.94% |
| large-eleven | Direct / wait if blocked | 53,671 | 85.46% | 84.33% | 0.81% | 76.67% |
| large-eleven | Flyer turns / ground collisions | 53,671 | 99.96% | 96.96% | 2.37% | 87.69% |

## Food/healing selection queries

| Map | Policy | Requests | Legal step | Shorter remaining route | Longer remaining route | Best available route step |
|---|---|---:|---:|---:|---:|---:|
| holiday | Direct / wait if blocked | 7,661 | 82.27% | 75.45% | 2.62% | 64.95% |
| holiday | Flyer turns / ground collisions | 7,661 | 99.99% | 88.77% | 5.00% | 76.63% |
| large-eleven | Direct / wait if blocked | 44,828 | 78.83% | 76.62% | 1.64% | 68.28% |
| large-eleven | Flyer turns / ground collisions | 44,828 | 99.88% | 93.37% | 4.91% | 82.38% |

These assess hypothetical directions toward each service candidate. They do not test a replacement distance estimate for choosing the best hospital/inn, reservation semantics, or starting travel before a destination is selected.

## Limits

This is a one-step counterfactual on real request states. It does not move the unit, simulate repeated heuristic steps, test loops, or measure arrival time, starvation, hiring outcomes, match outcomes or performance. Changing movement can change later unit positions, collisions and requests. A fallback may be useful for a short pending interval without being a complete pathfinder. Ground-legal alternatives can make remaining path distance longer, especially when selecting the wrong exit from an excluded origin; wrong-step-distribution.json and worst-examples.json preserve their frequency and magnitude in weighted land-step equivalents. A high useful-step rate alone does not establish the net benefit of enabling the fallback.

The main PR and its pending semantics remain unchanged. Raw requests and per-run commands/results are in request-data.tar.gz; pilot checksum traces, source patch, analysis and per-game aggregates are attached separately. No rendered play, cross-platform or other-device validation is claimed.
