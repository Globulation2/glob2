# Map generator framework

A generator is a pure function, `bool generate(Game &, GenerationContext &)`, registered
alongside its metadata in a `GeneratorDefinition` (id, stable legacy numeric id, display name,
revision, its `GeneratorControl`s, and optional `validateRequest`/`validateWorld` callbacks).
`GeneratorRegistry::builtins()` holds the fixed list; `GenerationService` drives the actual
lifecycle (validate the request, sample candidate seeds, run `generate`, score the result,
validate the finished world). There is no generator superclass, pipeline dispatcher or terrain
repair policy — a generator is free to build its terrain and resources however it likes, and
leans on the shared modules below only where doing so is actually less work than not.

## Shared building blocks

| Concern | Lives in | What it gives a generator |
|---|---|---|
| Discrete/stepped/power-of-two option domains, one shared UI and catalog export | `GeneratorControls` | Declares options once; normalization, display and validation are centralized |
| Cross-control and dimension constraints | `GeneratorDefinition::validateRequest` | A pure check run before generation; no silent settings rewrites |
| Jagged islands, stretched/rotated continents, coastline shape | `shared/Geometry` (`ShapeTransform`, `RadialShape`, `stampShape`) | An invertible shape transform and a seeded radial shape with a per-angle `radiusAt()` query — checking a specific candidate against its own angle is exact, where a single global "furthest possible" bound is only ever a conservative overestimate |
| Distances through corridors, connected components, resource-role adjacency | `shared/Topology`, `shared/Distances` | Graph distances and grid-component labelling with explicit wrapping/neighbor policy |
| Whole-region or local point dispersion (Contested Commons, Concrete Islands, Isles) | `shared/Regions` | Paired weight/point shuffling, a bounded local search and an opt-in whole-region search with an explicit pass/evaluation budget rather than silently treating an unfinished search as converged |
| Settlements restricted to a home island or room | `shared/Settlements` | Whole-footprint mask, nearest legal anchor, exact worker count, per-colony diagnostics |
| Generator-specific connectivity guarantees | `GeneratorDefinition::validateWorld` | Runs after structural checks, against the actual finished terrain/movement mask |

Geometry, Topology, Distances and Regions operate on plain dimensions and grids, independent of
`Game`. Settlements needs `Game` because placing buildings and units requires its mutation APIs.

## The generator catalog

`GeneratorRegistry::builtins()` orders these for the product-facing catalog; the numeric legacy
id is a separate, stable compatibility identifier that never changes once assigned and is never
reused after a generator is retired.

| id | legacy id | Display name | Family |
|---|---|---|---|
| `fjord-continent` | 12 | Fjord continent | Its own — see below |
| `maze` | 11 | Maze | Its own — see below |
| `contested-commons` | 9 | Contested commons | Point dispersion (`shared/Regions`) |
| `shattered-coast` | 7 | Old random | Iterative water/sand/grass balancer, own resource search |
| `isles` | 6 | Isles | Point dispersion; islands linked by land bridges |
| `rugged-archipelago` | 8 | Old islands | Island growth + beach passes, own resource search |
| `concrete-islands` | 5 | Concrete islands | Point dispersion; islands linked by channels |
| `crater-lakes` | 4 | Crater lakes | Height-field noise; round lakes in otherwise connected land |
| `islands` | 3 | Islands | Height-field noise; organic islands with no inter-island passage |
| `swamp` | 1 | Swamp | Height-field noise; water interleaved with land |
| `river` | 2 | River | Height-field noise; a winding river through connected land |
| `uniform` | 0 | uniform terrain | Editor-only; one terrain type, unstructured |

Swamp, River, Islands and Crater Lakes ("the height-field generators") shape their terrain and
paint their resource bands from the same Perlin noise field via `generateHeightField`, and use
`chooseBalancedStarts` (below) for colony placement. Contested Commons, Concrete Islands and
Isles build on the older Voronoi-style point-dispersion machinery in `shared/Regions` and
`shared/Distances`; Contested Commons additionally paints solid, sharply-bordered resource zones
as a deliberate design choice rather than a noise scatter. Shattered Coast and Rugged
Archipelago predate the shared resource/placement machinery and still place resources relative
to each boot tile with their own compass-direction search, rather than through
`scatterResources`/`chooseBalancedStarts`.

## Resource placement

`shared/Resources.cpp` gives generators a small set of composable primitives rather than one
fixed resource pass:

- `placeResourceClump` / `placeResourceClumpInArea` grow a resource outward from a point to a
  requested tile count — the primitive every guaranteed placement (starter kits, bank deposits)
  is built from.
- `computeLandComponents` flood-fills the map into its connected non-water components. A
  generator whose landmasses are separate (an island generator) needs this so that a
  map-wide "take the best tiles first" pass can't spend an entire resource band on whichever one
  or two islands score highest, starving the rest — every subsequent per-component pass below
  runs independently within each component, sized to that component's own share of the eligible
  candidate pool. On a single connected map this is one component covering everything.
- `scatterFarmland` answers two independent questions separately: *where* farmland goes is a
  `Fertility::Field` threshold (see below) over each land component's own candidate pool, widened
  a little beyond the raw requested tile count but capped at two-thirds of that component's pool
  so every landmass keeps real slack to route around whatever gets placed; *which* of corn or
  wood a given spot becomes is a second, independent noise field, sorted and sliced into a corn
  share and a wood share so the two alternate along the region instead of forming two concentric
  rings sorted by fertility.
- `scatterBand` is the same per-component noise-threshold technique for stone and algae, which
  have no growth rule to prefer and so use a plain noise field rather than fertility.
- `scatterResources(game, context, densities)` is the shared ambient layer — ordinary, unclaimed
  deposits filling the interior between a generator's deliberate placements — used by Fjord.
  It runs after every colony's swarm, workers and guaranteed clumps already exist:
  `isResourceAllowed` refuses any tile already holding a building or unit, so scattering earlier
  can claim a tile a swarm footprint needed; scattering last only ever loses a clump's own tile
  to whatever was already there, the same low-stakes trade every generator's own resource
  layering already makes.
- `guaranteeStartingResources(game, context, wheatRange, woodRange, clearRadius=0)` is a
  reachability backstop, not a placement pass: it compares a resource-respecting flood from each
  boot tile against the same flood with resources ignored, and where the two disagree — a
  colony's own tile is on well-connected land, just walled off from wheat or wood by a resource
  tile a ground unit can't stand on — it clears exactly the wall tiles responsible before topping
  up whichever resource is still out of range. A colony genuinely isolated on its own small spot
  (nothing reachable even through terrain alone) is left untouched, since there's nothing on the
  other side of a wall that isn't there. RuggedArchipelago, ShatteredCoast and Fjord call this.
  Maze doesn't need it: its resources are placed only inside rooms, behind a lane that keeps
  every exit clear, and its `validateWorld` confirms every colony can still walk to every other.

Every guaranteed placement (starter kits, bank guarantees, per-team clumps) keeps wheat and wood
at a 1:1 ratio, since those exist for reachability fairness between colonies. Ambient/bonus
layers (`scatterResources`, Fjord's bank-scatter roll, Maze's bonus-room defaults) instead use 2:1 corn:wood, which is purely
a feel decision independent of the fairness guarantee.

### Fertility

`Fertility::Field` (`src/map/FertilityField.{h,cpp}`) is a closed-form evaluation of
`Map::growResources`'s own regrowth rule — a wheat or wood tile expands when a randomly-offset
mirror point is water and its own mirror isn't sand, which integrates to a triangular kernel
over every nearby water tile with a per-sand-tile correction. `FertilityCalculator` (the map
editor's fertility tool, and old-save-format migration) is a thin wrapper over the same field, so
gameplay fertility and generation-time fertility scoring are the same computation. The field
takes plain terrain/sand masks, not a live `Game`, so a generator or the scoring pass below can
evaluate a candidate's growth potential without needing a finished map.

## Colony placement and fairness

Two further shared pieces score and choose where colonies actually start, beyond a generator's
own terrain and resources:

- `shared/StartingPositions::chooseBalancedStarts` (used by the four height-field generators)
  scores every legal site by its *worse* primary resource — a colony beside wood but far from
  wheat is not a good start — using the finished, as-built state: the swarm's clearing already
  cleared, its footprint impassable, the flood starting from where workers actually stand. It
  takes the narrowest score window that still holds enough mutually distant sites, falling back
  to the older any-legal-site search on maps where no set of sites can reach both resources at
  all.
- `shared/StartQuality::scoreStarts` scores the *finished* map's colonies on six factors —
  fertility (weighted highest, since it decides whether a colony's wheat ever comes back at all),
  wheat/wood distance, buildable room, resource depth, and isolation from rivals — normalized
  against fixed reference values (`StartQualityScale`, e.g. `wheatReference = 24`,
  `fertilityReference = 8000` on `Fertility::kScale`) so that candidate maps are ranked against a
  constant yardstick, not against each other's own best colony. A map's score is
  `worst * (worst/best)^fairnessExponent`: the weakest colony's quality, gated by how evenly the
  map shared quality out. `scoreStarts` also stamps the same `Fertility::Field` it computes for
  scoring into `Map::Tile::fertility`/`Map::fertilityMaximum`, which is otherwise only ever
  populated by the editor's fertility tool or old-save migration — without this, every generated
  map's in-game fertility overlay would show nothing at all.

`GenerationService` rolls `kSampledCandidates` (5) seeds from a root seed and keeps the
best-scoring one that generates successfully; `GenerationService::bestSeed` is the same search
used by the map editor's regeneration action. Generation is deterministic and the score is a
pure function of the finished map (asserted in `map-generator-defaults-test`), which is what
makes "roll several, keep the best" and "regenerate the winning seed later" both sound.

## Fjord continent

The richest generator, and the one most of this framework's resource work was proven against:

- **Shape.** A `ShapeTransform`-warped `RadialShape` coastline; an untouched core disc
  (`coreR`, currently 0.23x the continent radius) that every fjord stops short of, so it always
  stays connected land regardless of coastline roughness. A fjord is carved between every pair of
  angularly-neighboring teams as a smooth S-curve centerline from just outside the coast in to
  `coreR` (or, in lake-connected mode, well inside the lake — see below).
- **Central lake.** `lake-size` (0–90%, of `coreR`; 0 disables it) carves a lake at the exact
  center, ringed by a sandy no-man's-land wider than `Map::controlSand()`'s own coastal fringe.
  `lake-connected` (default off) decides whether the fjords actually cut through into the lake —
  every peninsula then water-isolated from its neighbors, boats required — or stop short behind a
  solid land ring, keeping mutual land connectivity; the latter is verified with an explicit
  flood-fill after construction rather than assumed, and only runs in that mode, since a
  lake-connected map is *supposed* to fail a same-landmass check by design. A fjord's carved tip
  is widened specifically in connected mode, since `controlSand()` erases any water tile with a
  grass neighbor in its own 3x3 neighborhood and a narrow tip is entirely coastal by that rule.
- **Core resources.** The ring around the lake carries several stone clumps and a grove of every
  fruit type — a genuinely rich destination, not a single token deposit. The lake itself gets a
  center-anchored algae clump plus bonus clumps drawn from well inside its shoreline, so a
  deposit never reads as merely stuck to one edge.
- **Outlier islands.** `resource-islands` (0–20) places small, unconnected islands out in the
  open sea, each themed to one resource. Each candidate is checked directly against the
  coastline's `radiusAt()` at its own position rather than against one global worst-case bound,
  so an island can land close to a narrow stretch of coast even while the coastline bulges out
  far away in some other direction.
- **Fjord banks.** Every fjord guarantees one corn and one wood clump per side, placed last so
  nothing else can overwrite the guarantee, plus six lighter best-effort clumps per side mixing
  corn/wood/stone along the same bank.
- **Ambient layer and backstop.** `scatterResources` fills the continent interior at the end
  (corn:wood 2:1, fruit, stone; algae left to the dedicated shoreline/lake passes above), and
  `guaranteeStartingResources` runs last as the reachability backstop described above.

## Maze

A maze in the pen-and-paper sense: narrow corridors between thick walls, with every colony in
its own cul-de-sac.

- **Grid.** The map is tiled into cells of about `cell-size` tiles on the map's own torus.
  Boundaries are chosen so the cells tile the map exactly (neighbouring cells differ in pitch by
  at most one tile), so the maze wraps across the seam like any other boundary. At least three
  cells are needed in each direction.
- **Maze and homes.** Homes are chosen first, on every other column and row and spread by
  farthest-point selection. That spacing keeps all other cells connected and gives each home four
  non-home neighbours. A recursive backtracker then carves a spanning tree of corridors over the
  non-home cells, and each home is attached by exactly one corridor, so every colony starts in a
  genuine dead end. `loopiness` knocks through extra walls between non-home cells only, so homes
  stay cul-de-sacs.
- **Walls.** Every closed boundary is a thick water channel with a stone spine covering the whole
  boundary line from corner to corner. Perpendicular walls share their corner tile, so a boundary
  can only be crossed — on foot or swimming — where it's open. STONE only places on a pure-grass
  tile, which needs grass at all four undermap corners (`Map::regenerateMap`), so each spine sits
  on a two-wide grass core inside a sand ring. Terrain is stamped directly rather than through
  `Map::controlSand()`, whose in-place raster pass shifts shorelines unevenly.
- **No walking along a wall.** A wall's sandy flanks are walkable land. `validateRequest`
  requires `floor(pitch / 2) - roomSize / 2 >= 7`, which leaves at least two all-water tiles
  between any room or corridor shore and any wall flank.
- **Corridors are sand**: walkable, but no crop can grow across one mid-game and nothing can be
  built on one, so buildings stay in rooms.
- **Rooms and resources.** Rooms exist only at dead ends: colony homes, plus bonus rooms at every
  other dead end. Room and corridor widths are odd and centred on a tile, so a room's layout is
  identical whichever way its exit faces. Each room keeps a lane from its back wall to its exit
  free of resources; wheat lines the left wall, wood the right, stone the back. Homes also keep a
  clear square around the swarm and get fixed 1:1 wheat and wood. Bonus rooms follow the
  wheat/wood/stone controls, and `fruit` is a count of fruit patches placed in bonus rooms. Algae
  is seeded in open channel water at least two tiles from land.
- **Checked, not assumed.** `validateWorld` floods walkable tiles (water, buildings and every
  resource, including wall spines, block it) from colony 0's workers, and fails the candidate if
  any colony isn't reached.

## Compatibility notes

- A `GeneratorDefinition::legacyId` is a stable compatibility identifier, not a display or sort
  order — `GeneratorRegistry::builtins()`'s constructor order is purely the product-facing
  catalog order. Once assigned, an id is never reused, including after a generator is retired.
- A generator's `revision` changes only when a change to `generate()` changes what a given seed
  actually produces; a pure refactor, comment or renumbering change leaves it untouched. Bumping
  it is how a reviewer knows a seed's output isn't expected to match a prior build byte-for-byte.
- `GeneratorControl`s are validated by `GeneratorRegistry`'s constructor: every control's default
  must land on a valid step from its minimum, and (`allowedValues` aside) `(maximum - minimum)`
  must be evenly divisible by `step`. Every control label needs matching entries in
  `data/texts.en.txt` (`[Label]` / `Label`) and `data/texts.keys.txt` (`[Label]`), the same as any
  other UI string.

## Verification tools

- `test/MapGeneratorStudy.cpp` builds to `MapGeneratorStudy`, a CLI harness that invokes the real
  production generators directly: `MapGeneratorStudy <method> <seed> <profile-dir> [key=value...]
  [tuning] [headroom] [quality] [dump=path]`. `quality` reports `StartQualityReport`/
  `ColonyQuality` per colony; `dump=` writes a plain-text terrain/resource grid for direct
  inspection or scripted flood-fill checks; `--catalog` dumps every registered generator's
  controls as JSON.
- `test/MapGeneratorDefaultsTest.cpp` builds to `MapGeneratorDefaultsTest`, asserting the
  registry's and every control's contract: discrete domains, shape bounds, topology, home
  footprints, exact worker counts, seed repeatability and RNG stream isolation, and the
  lobby/editor UI's own control-editing behavior.
- The cppunit suite under `test/` (`scons && ./TestsRunner`) covers the rest of the engine and
  must stay green alongside both of the above.

Generators validate their own construction results rather than trusting the geometry to always
succeed: a moat must connect to land at both bridge ends, jagged outlines must leave legal
settlement footprints, and Fjord's core must keep every player peninsula connected (outside
lake-connected mode, where that's expected not to hold). Difficult small or crowded combinations
can still fail outright, but do so with a reproducible stage diagnostic, and are discarded by
`GenerationService`'s candidate sampling rather than surfaced to a player.
