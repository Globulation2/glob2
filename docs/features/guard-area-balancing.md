# Guard-area balancing

An [experiment](experimental-features.md) (`guard-area-balancing`, **Settings →
Experiments → Guard-area balancing**). Off, the game plays exactly as before.

## Why

Guard areas were only half usable. Painting one told the warriors where to go,
but not how many would go: every free warrior walked to the nearest painted area
whatever was already there, so a batch trained in one place all took the same
area and any other area stayed empty. How many warriors a defence ended up with
depended on where the barracks stood, not on what the player painted, and the
only dependable tool was the war flag.

With the experiment, painted tiles are seeded with a crowding cost, so warriors
spread between areas in proportion to painted size, an area that is over-full
drains into the others, and a bigger painted area gets more warriors. Painting a
guard area then means what a player expects it to mean.

## Mechanism

The rules live in `src/map/gradient/MapGradientArea.cpp`
(`Map::seedGuardAreaCrowding`), `src/map/pathfind/MapPathfindArea.cpp`,
`src/map/pathfind/MapPathfindRessource.cpp` and `src/unit/UnitMovement.cpp`; the
constants in `src/map/MapInternal.h`. Every one of them reads
`game->gameHeader.hasExperiment(ExperimentId::GuardAreaBalancing)` first.

- **Crowding.** At each guard-gradient rebuild the map counts, for every tile,
  the team's warriors within `GUARD_CROWD_RADIUS` tiles (Chebyshev, units inside
  buildings excluded), and separately the painted tiles within the same radius.
  Each count is a box sum: positions are splatted onto a map-sized scratch grid
  and summed with two sliding-window passes, rows then columns, both row-major.
  The torus wrap is the same index masking the gradients use. Each pass is
  linear in map size, independent of warrior count and radius, and both only run
  for a team that has painted tiles and warriors. Guard fields of different teams
  and swim classes rebuild in parallel, so the scratch grids are per thread.
- **Seeding.** A painted tile is seeded at `GRADIENT_AT_GOAL` minus
  `GUARD_CROWD_COST_PER_WARRIOR` per counted warrior, scaled by
  `GUARD_CROWD_REFERENCE_AREA` over the painted count and capped at
  `GUARD_CROWD_COST_MAX`: warriors nearby per painted tile nearby. A 5x5 area
  costs the full per-warrior amount, a four-times larger one a quarter, so shares
  follow painted size. Dijkstra then gives every tile the best of "distance to an
  area plus that area's crowding". A warrior outside any area steps uphill as
  before. Two areas settle where their crowding difference matches the walking
  distance between them, so balance is traded against distance rather than
  forced: a far area is allowed a few fewer warriors.
- **Leaving.** Inside an over-full area another area's field runs over the
  painted tiles, so an uphill step out exists. Every warrior there sees the same
  field, so following it is gated to one action in `2^GUARD_LEAVE_CHANCE_SHIFT`;
  the other actions move to a higher painted neighbour or keep the old in-area
  wander. Leavers still count toward the area they left until they are
  `GUARD_CROWD_RADIUS` tiles out, which costs more than the one warrior's worth of
  crowding they freed, so the trickle stops rather than running on, and a leaver
  does not turn back (`static_assert` in `MapInternal.h`).
- **Staying.** A warrior standing on a painted tile that has to take a random
  step prefers a free painted neighbour. Off the paint it looks like a new
  arrival to the field, so stepping off whenever it could is how a full area
  would drain all at once. When no painted neighbour is free (a packed area, or
  sparse, checkerboard or 1x1 paint), it takes the ordinary random step onto any
  free tile, as without the experiment. Holding it still instead would leave it
  with no direction, which both renderers draw as a unit spinning in place; and a
  player's guard area is rarely painted exactly, so warriors standing beside the
  paint still defend it. Only a warrior boxed in on all eight sides stays put, as
  any unit always has. The cost is a small overshoot while an area drains: a
  warrior that steps off a packed area is a free warrior to the field and may walk
  to the other area, so the first area dips a few warriors below where it settles
  and they come back.
- **Inside.** Without the experiment "inside a guard area" is the field's goal
  value, as it always was. With it a crowded area's tiles sit below that value, so
  the warrior movement code reads the painted bit (`Map::isGuardArea`) instead.

The guard gradient is rebuilt where it always was: on every area order, on
building death, and on the gradient pipeline's schedule. Nothing new is saved:
the crowding grids are scratch, the field itself was already saved with its
refresh flag, and a loaded game refreshes on the same schedule as an uninterrupted
one. The experiment itself travels in the game header, so a loaded game keeps
balancing.

## Tuning

| Constant | Value | Meaning |
| --- | --- | --- |
| `GUARD_CROWD_RADIUS` | 8 tiles | Warriors and painted tiles this close to a tile count toward it. Must exceed the per-warrior cost in tiles, and be at least the size of a typical area: with radius 3 a 5x5 area has cheaper edge tiles and warriors spread to them instead of leaving. |
| `GUARD_CROWD_COST_PER_WARRIOR` | 4 tiles | Extra walking one nearby warrior is worth in a 25-tile area. 8 tightens the split (11/13 instead of 13/11 in the test) at the price of a stronger pull on a moving crowd. |
| `GUARD_CROWD_REFERENCE_AREA` | 25 painted tiles | Painted count at which the per-warrior cost applies in full; larger paint counts divide it. |
| `GUARD_CROWD_COST_MAX` | 400 tiles | Keeps seeds above the unreachable sentinel. Also where balancing stops: an area's cost reaches the cap at four nearby warriors per painted tile nearby, which is 100 warriors for a 5x5, 36 for a 3x3 and 4 for a single tile. Past it, every capped area reads the same and the field is a plain nearest-area partition again, so small areas painted with brush 0 balance only up to a handful of warriors. |
| `GUARD_LEAVE_CHANCE_SHIFT` | 6 (1 in 64 actions) | Drain rate. At 64 a full area of 24 loses 10 to 13 to a new area over about 2,000 ticks. |

Shares follow painted size roughly, by design: the equilibrium is a band as wide
as the walking distance between the areas divided by the per-warrior cost, and
which point in the band a game lands on depends on how the warriors arrived. An
exact split would have warriors walking between areas to correct a difference of
one, which is both wasteful and not what a player painting two areas expects.

Things that were tried and dropped: a "leave only for a clear gain" margin on the
uphill step never fires, because a smooth field only rises one step per tile;
labelling areas as connected components was replaced by the radius count, which
also handles a warrior standing next to a small area and two patches painted
close together; and radius 3 broke balancing outright.

## Measurements

The `GuardAreaBalance` suite of `glob2-engine-tests`
(`python3 test/run_tests.py --filter 'GuardAreaBalance/*'`) runs the real engine
on a blank 64x64 map with one team and 24 level-0 warriors, seeded so runs repeat
exactly. Two 5x5 areas are painted, "near" 18 tiles from the spawn block and "far"
32 tiles away on the other axis. Counts are warriors within 7 tiles of an area's
centre. The first case runs the spawn and drain scenarios **without** the
experiment and expects the old outcome (24 / 0, nobody leaves), which is what
proves the gate. The balancing cases run at the three refresh cadences a game can
have (every tick, about every 10 ticks with a small game's resource fields, about
every 170 with a large game's).

| Case | What it checks |
| --- | --- |
| spawn | Warriors arriving from the base take both areas. |
| drain | A clump that fills one area thins into a newly painted one without emptying out and refilling. |
| patches | Two unconnected patches 3 tiles apart share as one position. |
| size | A 9x9 area farther away takes more than a 5x5 area nearer. |
| three | Three areas at increasing distance are all guarded. |
| erase | Erasing an area sends its warriors to the remaining one. |
| saveload | A game saved mid-balancing continues identically for 1,000 ticks after loading. |
| spins | Settled guards on 1x1, sparse, checkerboard, packed and roomy areas keep moving: few ticks with no direction, none standing on one painted tile for 500 ticks, and most warriors still within 7 tiles. |
| crowding | The box sum equals a brute-force count across the torus seam. |
| timing (`[benchmark]`) | Rebuild wall time on 64x64 and 256x256 maps. |

Warriors at each area after 4,500 ticks in the original development harness
(macOS arm64, release build, seed 1):

| Scenario | Off (near / far) | On, every-tick refresh | On, ~10-tick refresh | On, ~170-tick refresh |
| --- | --- | --- | --- | --- |
| spawn: both areas painted, warriors arrive | 24 / 0 | 13 / 11 | 13 / 11 | 11 / 13 |
| drain: 24 inside near, then far painted | 24 / 0 | 11 / 13 | 11 / 13 | 11 / 13 |
| patches: near is two 3x3 patches 3 tiles apart (18 painted tiles) | 24 / 0 | 11 / 13 | 12 / 12 | 9 / 15 |
| size: near 5x5, far 9x9 | 24 / 0 | 4 / 20 | 5 / 19 | 10 / 14 |
| three: near / far / third at 18, 32 and 45 tiles | 24 / 0 / 0 | 5 / 10 / 9 | | |
| erase: far erased after settling | | 24 / 0 | | |

Over seeds 1-10 at all three cadences, the first area in `drain` dips 1 to 5
warriors below where it settles before they come back (mean 2.4). Settled guards
(`spins`, 500 ticks sampled after 4,000): the share of on-paint ticks with no
direction fell from 100% (1x1 and sparse paint), 81 to 85% (checkerboard) and 72
to 74% (packed 5x5) to 0 to 24%, 0%, 0% and 0.6 to 2.5%, with no warrior held on
one painted tile for the whole sample on any layout. What remains on a 1x1 is the
one guard boxed in by the crowd pressing round its single tile.

Guard-gradient rebuild wall time, median of 200 rebuilds with two 5x5 areas
painted: 64x64 map 31 to 32 us before, 36 us with 24 warriors after, of which one
box-sum pass is 5 us; 256x256 map 444 to 510 us before, 529 to 582 us after, box
sum 73 to 90 us. A team that has painted nothing pays nothing beyond the old
seeding pass. Full simulation step in the spawn scenario: 16.0 us before and 15.8
us after with 24 warriors; with 96 warriors 181.9 us before and 70.0 us after,
because warriors blocked around a packed area retry the area pathfinder and a
blocked retry rebuilds the whole guard gradient.

## Feel

Arrival at a first area is slower than before when a whole batch walks together:
the crowd counts against the area it approaches from eight tiles out and part of
it diverts, so 90 percent of a 24-warrior block is in some area by tick 1,000
instead of 250. A packed area no longer spills warriors onto the tiles around it.
An over-full area empties as a trickle over a couple of thousand ticks, not at
once. Warrush, which paints guard areas on the enemy buildings it has found,
spreads its attack across them instead of massing on the nearest. These are the
effects a maintainer should look at in play with the experiment on.
