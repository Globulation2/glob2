# Pathfinding: current design, measured cost, and options

Status: evaluation for the `feat/pathfinding-wip` branch (2026-09-06). Numbers
come from `-test-games-nox` runs of the release build with
`GLOB2_PATHFIND_STATS=1` (see "Measuring" below).

## 1. How pathfinding works today

Ground units never plan a path. Every tile step they ask the map for one
direction, read off a precomputed **distance field** ("gradient"):

| Field | Owner | Size | Built by | Rebuilt |
|---|---|---|---|---|
| resource gradient `[team][resource][canSwim]` | Map | 1 byte/cell, full map | chamfer sweeps | round-robin, one field per tick (`Map::syncStep`) |
| forbidden / guard-area / clear-area `[team][canSwim]` | Map | 1 byte/cell, full map | chamfer sweeps | on area edits + same round-robin |
| building local gradient `[canSwim]` | Building | 32×32 bytes | spiral chamfer, 2 depth passes | when dirty |
| building global gradient `[canSwim]` | Building | 1 byte/cell, lazily allocated | chamfer sweeps | throttled to once per 128 ticks |

Field values: `0` obstacle, `1` unreachable/unknown, `255` goal, otherwise
`255 - distance`. Distance is **Chebyshev** (orthogonal and diagonal steps
both cost 1) computed by a forward+backward raster sweep repeated until
stable (`Map::updateGlobalGradient`, `src/map/gradient/MapGradientGlobal.cpp`).
The Uint8 range caps usable distance at ~250 tiles.

Steering (`src/map/gradient/MapMinigrad.cpp`): the unit samples the 5×5
window of the field around itself, marks cells currently occupied by other
units as obstacles, and picks the direction whose inner cell (plus outer arc)
has the highest value. Ties favour cardinal directions; diagonal directions
are scored first. In `strict` mode a direction must be strictly uphill; if
none is, the caller falls back to `MOV_RANDOM_GROUND` (a random free step).

Point-to-point A* (`Map::pathfindPointToPoint`) exists but is only used for
warrior target acquisition, with a path-length cap. It returns only the
first step.

Job allocation (`Building::subscribeToBringResourcesStep`,
`Building::considerUnitForResources`): a building hires the free worker with
the lowest `(dist(unit,building) + dist(unit,nearest resource)) / need`.
Both distances are read from the fields above. The worker then walks the
resource field to the resource nearest **to itself**, then the building
field back. Nothing scores the resource by its distance to the building.

Speed (`Unit::syncStep`, `Race.cpp` defaults): a tile move takes
`256 / speed` ticks regardless of direction. Worker walk speed is
16/21/26/30 per level, swim 0/10/20/30. So a diagonal step covers √2 the
ground in the same time (units are ~41 % faster diagonally), and swimming
is 2.1× slower than walking at level 1, 1.3× at level 2, equal at level 3.
Neither fact is visible to the field, which is why a unit will swim a
long way rather than walk around, and why paths hug diagonals.

### How each reported symptom maps to the code

| Symptom | Cause |
|---|---|
| swims slowly where walking one tile north would be faster | Chebyshev field: water and land cost the same; `canSwim` only toggles water between passable and obstacle |
| erratic in crowds | occupied cells become obstacles in the 5×5 window; if no strictly-uphill free neighbour exists the unit takes a **random** step (`pathfindResource` → `MOV_RANDOM_GROUND`). Measured: 2 % of resource steps on a 2-team map, 11 % on an 11-team map |
| long walks when short on workers, resources carried in opposite directions | hiring uses unit→building + unit→nearest-resource; the resource actually walked to is the one nearest the unit, not the one nearest the building; a carrying unit can be hired away by another building (`selectUnitCarryingNeededResource`) |
| no concept of intermediary stops | one field per goal type; the unit descends resource field then building field; the composite cost `d(unit,r) + d(r,building)` is never minimised |
| faster diagonally | `delta += speed` per tick, same threshold for every direction |

## 2. Measured cost of the current system

10 game minutes (15 000 ticks), Nicowar on every team, seed 1, release build,
single core:

| Map | Teams | Size | CPU total | of which full-map chamfer | chamfer calls | avg per call | wall |
|---|---|---|---|---|---|---|---|
| Mazury | 2 | 128×128 | 5.65 s | 4.16 s (74 %) | 17 483 | 238 µs | 5.7 s |
| G2 | 4 | 128×128 | 7.30 s | 5.03 s (69 %) | 21 088 | 239 µs | 7.3 s |
| Oazis | 11 | 256×256 | 45.5 s | 33.1 s (73 %) | 27 959 | 1 183 µs | 45.5 s |

Breakdown on Mazury: resource gradients 3.23 s (12 032 rebuilds — one per
tick, blind round-robin whether or not anything changed), area gradients
0.90 s, building global gradients 0.56 s (1 178 rebuilds), building local
gradients 0.03 s, A* 2.3 ms (709 calls, 53 expansions each), 36 634 minigrad
steering calls (cost not separately measurable, well under 50 ms).

Per tick that is 0.38 ms (Mazury) to 3.0 ms (Oazis) against a 40 ms tick
budget. The chamfer averages 3.9 passes (7.8 raster sweeps) per rebuild,
about 15 ns per cell-sweep.

Behaviour counters (Mazury, team 0 / team 1): 28 346 / 21 255 tile moves,
of which 44 % / 39 % diagonal; 257 / 22 swim moves; 162 / 23 random steps
while attached to a job; 851 / 688 deliveries.

Conclusions: (a) per-unit steering is essentially free, the whole cost is
field construction; (b) the current scheduler burns most of that cost
rebuilding fields that did not change; (c) there is 10–100× CPU headroom
before pathfinding threatens the tick budget, so a more expensive but
correct field builder is affordable.

## 3. Options, structured by layer

The problems live in four separable layers. Each can be changed
independently and A/B-tested with the harness in §5.

### Layer A — cost model (what "distance" means)

| Option | Fixes | Notes |
|---|---|---|
| A0 Chebyshev, uniform (today) | — | wrong for time |
| A1 octile: cardinal 10, diagonal 14 | diagonal bias, once diagonal moves also cost √2 time (Layer D) | exact for the unit's own step timing to 1 % |
| A2 A1 + terrain weight per unit class: water costs `walk/swim` × land | swim-vs-walk | speed ratio depends on level, so either 3 swimmer classes (L1 ≈2.1×, L2 ≈1.3×, L3 1×) instead of today's 2 (`canSwim`), or one compromise ratio |
| A3 A2 + congestion penalty on cells recently occupied | crowds (partly) | field ages; cheap to add once fields are Dijkstra-built |

Weighted costs need more than 8 bits: a Uint16 field (max 65 535 = 6 500
tiles of cost 10) covers any map size the engine supports.

### Layer B — field construction

| Option | Cost per full-map rebuild (128×128 est.) | Exact for weighted costs? | Deterministic? |
|---|---|---|---|
| B0 chamfer raster sweeps, Uint8 (today) | 0.24 ms measured | no (only Chebyshev/uniform) | yes |
| B1 BFS (queue), uniform cost | ~0.2 ms | no | yes |
| B2 Dijkstra with bucket queue (Dial), Uint16 integer costs | ~0.6–1.2 ms | **yes** | yes with FIFO buckets (no heap) |
| B3 B2 + dirty-region incremental repair | ~0.05–0.3 ms typical | yes | yes |
| B4 fast marching / eikonal | ~2 ms | continuous only; float arithmetic | risky across platforms |

B2 is the natural replacement: it is a multi-source shortest-path on a
weighted 8-connected torus, which is exactly what every existing consumer
(`resourceAvailable`, `buildingAvailable`, steering) already assumes the
field to be. B4 is ruled out: floats are a multiplayer desync risk and the
grid is coarse anyway.

Nicowar's own planning (`src/ai/echo/Gradients.*`) already builds
Sint16 BFS fields; it is separate from unit movement and unaffected.

### Layer C — path consumption

| Option | Notes |
|---|---|
| C0 5×5 minigrad, strict uphill, random fallback (today) | fallback is the erratic behaviour |
| C1 argmin over the 8 neighbours of `field[n] + edgeCost(n)` (true descent on a weighted field) | needed for A1/A2 anyway; cardinals no longer win ties for free |
| C2 C1 + "wait one tick" and "sidestep to equal-cost neighbour" before random | crowds: units queue instead of scattering |
| C3 per-unit A* on the weighted grid (path cached, re-planned on blockage) | exact paths, but multi-target queries ("nearest wood") still need a field or a Dijkstra from the unit; ~50–200 µs per plan, 100+ units → affordable but no better than C1 on a correct field |
| C4 hierarchical A* (HPA*) / jump-point search | only pays off on maps far larger than 256×256; JPS needs uniform cost, incompatible with A2 |

A field-based design keeps the per-step cost at O(8) per unit, which matters
because a team routinely has 60–80 workers re-deciding every 8–16 ticks.
Per-unit A* (C3) is the right tool for point-to-point *orders* (attack
target, explicit move) and can stay for those.

### Layer D — movement timing

Charge a diagonal step 362/256 of a cardinal step (√2 in the existing
Q8 delta arithmetic) in `Unit::syncStep`. One-line change, but it must
land together with A1 or units would zigzag through cardinal steps that
the Chebyshev field still rates equal. Cannot be applied per team, so in
the harness it is toggled per run, not per side.

### Layer E — job allocation and intermediate stops

The "walk to the closest resource with no consideration for where it is
needed" problem is not a pathfinding bug: it is the choice of *which*
resource tile. Options:

| Option | Notes |
|---|---|
| E0 nearest resource to the unit (today) | |
| E1 composed field per (building, resource type): Dijkstra seeded with every resource tile at initial cost `d(r, building)` from the building's own field. Descending it from the unit's position minimises `d(unit, r) + d(r, building)` exactly | this *is* the intermediate stop, still expressed as one field, so steering code does not change; cost = one B2 rebuild per active (building, resource) pair, built lazily and throttled like the building global gradient is today; memory 2 bytes/cell per pair (32 KB on 128×128) |
| E2 per-unit two-leg search: Dijkstra from the unit until the best `g(r) + d(r,B)` cannot improve | same answer as E1 without the field, ~0.1–1 ms per decision; better when few units, worse when many |
| E3 hiring by round trip: score candidates by `d(unit,r*) + d(r*,building)` using E1's field, and do not poach a carrying unit unless the delivery point is closer than its current one | removes the "same resource carried in opposite directions" behaviour |

So gradient descent does **not** have to be abandoned for L2-like costs or
for intermediate stops; what has to go is the chamfer builder (B0) and the
integer "uphill" steering (C0). Both A2 and E1 are Dijkstra fields with
non-uniform edge weights and non-uniform seed costs, and descent on such a
field is optimal by construction.

## 4. Expected properties of the recommended stack (A2 + B2/B3 + C1/C2 + D + E1/E3)

**Performance (game outcome).** Straight-line time-optimal routes; no
diagonal bias; swim only when faster; crowd queues instead of random
walks; resource pickup chosen for the round trip. To be quantified by the
harness (deliveries per worker-minute, units and buildings at 10 min).

**CPU.** Field builds become ~3–5× more expensive per rebuild (B2 vs B0),
but the blind round-robin can be replaced by dirty tracking (B3): a
resource field only changes when a resource tile appears, is exhausted,
or a building/forbidden area changes on it. Expected per-tick cost after
both changes: below today's 0.4 ms on 128×128, and 3–6 ms worst case on
256×256 with 11 teams during heavy construction. Headless simulation speed
(currently 10 game minutes in 5–7 s) should stay within 2× of today.

**Memory.** Today: 22 full-map bytes per team (8 resources × 2 + 3 areas ×
2) plus 2 bytes… per building for the local grid and lazily 2 full maps per
building. 128×128: 360 KB per team, 32 KB per building. Uint16 fields
double that: 720 KB per team, 64 KB per building, plus 64 KB per active
(building, resource) composed field. 256×256 with 11 teams: ~32 MB
worst case. Acceptable; the map's `cases` array alone is larger.

**Determinism.** Everything stays integer. Bucket-queue Dijkstra with FIFO
buckets has a fully specified expansion order; neighbour order and
tie-breaks are fixed tables (like `tabClose` today). No `std::set`, no
floats, no `std::priority_queue` (its heap layout is implementation-
defined, so the existing A* is only deterministic between identical STL
builds — worth fixing while touching it). Fields are derived state, not
saved; a save/load round trip regenerates them exactly as today.

## 5. Measuring

Build: `scons release=1`. Run one A/B game:

```bash
GLOB2_PATHFIND_ALT_TEAMS=2 GLOB2_PATHFIND_STATS=1 GLOB2_MAX_TICKS=15000 \
GLOB2_TEST_SEED=1 GLOB2_TEAM_TIMELINE=1 \
./build/src/glob2 -test-games-nox 1 --map Mazury --matchup nicowar,nicowar
```

- `GLOB2_MAX_TICKS` caps the run (15 000 ticks = 10 game minutes).
- `GLOB2_PATHFIND_ALT_TEAMS` is a bitmask of teams using the alternative
  pathfinder (`src/map/PathfindPolicy.h`); the others use the baseline.
- `GLOB2_PATHFIND_STATS` prints `GLOB2_PF …` (CPU per field type, steering
  calls, stuck count) and `GLOB2_PF_TEAM …` (per-team moves, diagonal/swim
  split, random steps while working, deliveries).
- `GLOB2_TEAM_TIMELINE` prints units/buildings every 512 ticks per team.

`scripts/pathfind_ab.sh -m Mazury -s "1 2 3 4 5"` runs each seed twice with
the assignment mirrored (team 0 alt / team 1 base, then the reverse) and
prints one table. Mirroring matters: on Mazury seed 1 the two Nicowars end
at 77 vs 33 units with identical code, purely from start position.

Same binary, same seed and mask → identical order stream (verified: replays
differ only in 29 header bytes). Nicowar issues ~1 500 orders per 10 minutes
on 2-team maps, so its decisions do react to unit behaviour; the outcome
metrics are therefore whole-economy metrics, not just path lengths.

2-team maps available: Dejans, SmallForTwo, balanced_for_2 (64×64), Muka
(64×128), Mazury, strange2 (128×128).

## 6. Iteration 1: weighted Dijkstra fields (A1/A2 + B2 + C1, D per run)

Implemented behind `GLOB2_PATHFIND_ALT_TEAMS` (`src/map/pathfind/MapWeightedField.cpp`):
Dial bucket-queue Dijkstra into a Uint16 cost field, octile 10/14, water
×2 for swimmers, argmin steering with a non-strict sidestep when blocked,
one weighted full-map field per building (no 32×32 local field) rebuilt
when the map changes nearby (at most every 25 ticks) or when a unit is
stuck and the field is older than 128 ticks. Resource and building fields
only; forbidden/guard/clear areas stay on the baseline. Unit tests in
`test/WeightedFieldTest.cpp`.

**CPU.** A full-map weighted rebuild costs 150–190 µs on 128×128, *less*
than the 230–240 µs chamfer it replaces (the chamfer needs ~4 passes over
the whole map, Dijkstra touches each cell once). Total simulation CPU with
both teams on the alternative: 4.6–4.8 s vs 5.65 s baseline per 10 game
minutes on Mazury.

**Determinism.** Same seed and mask twice → identical order stream, with
and without `GLOB2_DIAG_SQRT2`.

**Outcome, 10 seeds × mirrored sides = 20 team-games per cell** (Nicowar
vs Nicowar, 10 game minutes; ALT relative to BASE):

| map | diag √2 timing | units | buildings | deliveries | random steps while working | swim moves | tiles per delivery |
|---|---|---|---|---|---|---|---|
| balanced_for_2 (64², no water) | off | +3.0 % | −0.5 % | −2.0 % | −38 % | −85 % | −3.2 % |
| balanced_for_2 | on | −1.9 % | −2.7 % | −3.3 % | −32 % | −74 % | −3.9 % |
| Mazury (128², water) | off | −11.4 % | −7.3 % | −7.1 % | −11 % | −76 % | −4.7 % |
| Mazury | on | −2.2 % | −2.0 % | −2.6 % | +0.3 % | −66 % | −0.3 % |

Reading: the path-level metrics move the way they should (fewer tiles
walked per delivery, far less swimming, a third fewer random steps in
crowds on the small map). The economy-level outcome is within noise
(per-game standard deviation of units is ~20 on 60–90, so the ±2–3 %
cells are not significant) except Mazury without the diagonal timing fix,
where the octile field is a genuine handicap: it avoids diagonals that the
unchanged movement code still makes 41 % faster. With the timing fix the
handicap disappears, which confirms that A and D must ship together.

Why the outcome does not improve yet: the remaining ~3 % fewer deliveries
come from hiring, not walking. `Building::considerUnitForResources`
compares the field distance to the unit's hunger budget in "tiles"; octile
and water-weighted distances are larger than Chebyshev ones for the same
route, so alternative teams reject slightly more far jobs. The traffic and
intermediate-stop problems (Layer E) and crowd queueing (C2) are untouched
in this iteration and are where the visible gains are expected.

Next: (1) express the hunger budget in the same cost units as the field so
hiring is not biased against the alternative; (2) C2 wait/sidestep instead
of the random step; (3) E1 composed (building, resource) fields and E3
round-trip hiring.
