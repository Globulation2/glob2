from pathlib import Path
import json
b=Path(__file__).parent;d=json.loads((b/'summary.json').read_text())
def selected(subset,dedup=False):return [r for r in d['results'] if r['subset']==subset and r['dedup']==dedup]
def table(subset,dedup=False):
 lines=['| Map | Policy | Requests | Legal step | Shorter remaining route | Longer remaining route | Best available route step |','|---|---|---:|---:|---:|---:|---:|']
 for r in selected(subset,dedup):
  m=r['metrics'];label='Direct / wait if blocked' if r['variant']==0 else 'Flyer turns / ground collisions'
  lines.append(f"| {r['workload']} | {label} | {r['n']:,} | {m['legal']['pct']:.2f}% | {m['progress']['pct']:.2f}% | {m['worse']['pct']:.2f}% | {m['best_available']['pct']:.2f}% |")
 return '\n'.join(lines)
text=f'''# Direction fallback accuracy while building gradients are missing

Accuracy-only experiment; production PR 1197 remains at d1c0d4d50109b6f7d30f843368d30406f515b84f. No heuristic movement was added to production or to the simulated matches.

## Workload and validation

{d['runs']} completed games, {d['ticks']:,} actual ticks, {d['pending_queries']:,} sampled pending route queries. Fixed Holiday Island 2 (128×128, four teams) and Oazis (256×256, eleven teams), 32 game seeds 42–73 per map. 32,768-tick cap; games may finish earlier. AI order Nicowar, Econo, Castor, Warrush, repeated/truncated to eleven teams. Four compute participants; two matches at a time. Current PR uses its own global four map admissions per tick; the separate per-team proposal is not folded into this workload.

All {d['runs']} initial/final checksums and actual tick counts match the previously recorded baseline games. Both 2,048-tick seed-42 pilots match per-tick world and entity checksum traces against the frozen unprobed binary and against the current PR's native development executable. Long-run comparison is at endpoints, not every tick. Runtime attribution mode `baseline` suppresses the old speculative creation requests and retains primary hiring preparation. Its source ancestry is documented in experiment.json; pilot checks establish equivalence to the current demand-driven PR on those inputs.

Apple M3, macOS arm64. Release build with existing dependencies, `PYTHONPATH=. scons release=1 CXXFLAGS=-g0 client_profile=full server=0 -j4`. Private instrumentation patch and its three untracked headers are attached; applying the patch to its recorded benchmark-checkout head and restoring the headers reconstructs the diagnostic source. Scripts and frozen binary SHA are attached. Analysis uses repository-pinned NumPy 2.4.6 on Python 3.13. This run has no CPU/tick, jitter or device-performance interpretation: oracle work is substantial and matches run concurrently.

## What is measured

At a pending query, identify the ground unit actually occupying the query cell and matching its swim class. Compute a private complete building gradient from the current map and the destination's current footprint/range, using the normal seeder and Dijkstra implementation. The private field is never published, never touches a live field or assignment, and never consumes game RNG. Fields can be reused only within a tick with matching tracked map generations, identity and slot epoch. These are fresh current-world route costs, not a prediction of the later queue capture or publication after the world changes.

The direction rule copies the ordinary unconstrained flyer heading exactly: wrap displacement around the map, point toward the building centre (flag position for flags), then round to eight directions with the existing 2.41 threshold. Every proposed step must pass ground terrain and collision rules, including forbidden areas, buildings, resources and other units.

- Direct: try the heading, wait when blocked.
- Flyer turns: if that heading is blocked, try offsets +1, -1, +2, -2, +3, -3, +4 in the existing flyer order, using ground collisions. Wait if no step is legal.

No policy sees the oracle when choosing its direction. A shorter remaining route is useful even if another direction is better. Best-available agreement accepts every tie under the normal gradient neighbour score, rather than requiring one arbitrarily tie-broken direction.

Some current cells are excluded from the gradient (e.g. forbidden cells or standing on resource/other-building cells). For these, compare against the shortest legal reachable exit, counting the step to reach it. They remain in the overall figures and have a separate table. Origins already at a route goal are excluded from movement percentages. Potential choices without a reachable origin/exit remain in the denominator. All rates use actual pending query opportunities; they do not claim a percentage of all movement ticks.

## Assigned-unit movement requests

{table('movement')}

Rates are request-weighted. summary.json also contains means of the 32 per-game rates and 95% intervals from 10,000 whole-game bootstrap resamples. Calls from one game are not treated as independent statistical trials. Stopped, blocked, unchanged-distance and off-route categories are included in that file, so legal-step rates are not confused with useful movement rates.

## Repeated locations counted once

For each game, retain only the first request for each unit identity, building identity, route, swim class and coordinate. This reduces repeated waiting at one spot.

{table('movement',True)}

## Ordinary footprint routes

{table('movement-footprint')}

## Combat flag routes

{table('movement-combat')}

## Open and excluded origin cells

Open origin:

{table('movement-origin-open')}

Excluded origin / exit assessment:

{table('movement-origin-blocked')}

## Food/healing selection queries

{table('service-selection')}

These assess hypothetical directions toward each service candidate. They do not test a replacement distance estimate for choosing the best hospital/inn, reservation semantics, or starting travel before a destination is selected.

## Limits

This is a one-step counterfactual on real request states. It does not move the unit, simulate repeated heuristic steps, test loops, or measure arrival time, starvation, hiring outcomes, match outcomes or performance. Changing movement can change later unit positions, collisions and requests. A fallback may be useful for a short pending interval without being a complete pathfinder. Ground-legal alternatives can make remaining path distance longer, especially when selecting the wrong exit from an excluded origin; wrong-step-distribution.json and worst-examples.json preserve their frequency and magnitude in weighted land-step equivalents. A high useful-step rate alone does not establish the net benefit of enabling the fallback.

The main PR and its pending semantics remain unchanged. Raw requests and per-run commands/results are in request-data.tar.gz; pilot checksum traces, source patch, analysis and per-game aggregates are attached separately. No rendered play, cross-platform or other-device validation is claimed.
'''
(b/'README.md').write_text(text)
