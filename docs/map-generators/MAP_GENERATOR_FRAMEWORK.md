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
| `contested-commons` | 9 | Contested commons | Point dispersion (`shared/Regions`) |
| `maze` | 11 | Maze | Its own — see below |
| `fjord-continent` | 12 | Fjord continent | Its own — see below |
| `shattered-coast` | 7 | Old random | Iterative water/sand/grass balancer, own resource search |
| `isles` | 6 | Isles | Point dispersion; islands linked by land bridges |
| `watershed` | 13 | Watershed | Its own — see below |
| `stone-highlands` | 14 | Stone highlands | Its own — see below |
| `symmetric-arena` | 15 | Symmetric arena | Its own — see below |
| `ring-world` | 16 | Ring world | Its own — see below |
| `city-states` | 17 | City states | Its own — see below |
| `tidal-flats` | 18 | Tidal flats | Its own — see below |
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

## Resource amounts and switches

Every playable generator has amount controls for the resources it places (wheat, wood and stone
everywhere; algae and fruit where it places them) and one to three on/off switches for its own
sub-behaviours. An amount is a percentage of the generator's default, 100, from 0 to 300 in steps
of 25 unless noted. It scales the numbers that already decided that amount, and at 100 every map is
exactly what it was. Fairness placements (starter kits, 1:1 guaranteed wheat and wood, the
reachability backstop) stay unscaled wherever a generator has them, so an amount of 0 empties the
ambient layer but still leaves every colony a start. Maze keeps its explicit densities.

| Generator | What the amounts scale | Switches (default) |
|---|---|---|
| Swamp, River, Islands, Crater lakes | Each resource's band of the height field: algae the lowest sixth of the water, stone a third of the farmland share just above the beach, wheat and wood the rest as two separate bands, none past the top of the grass. Fruit still counts groves | Stone on hilltops (off): stone on the highest grass instead of by the shore. River also: Winding river (on) |
| Concrete islands | Each colony's wheat and wood fields (how far in from the coast they reach) and its six stone deposits; the channels' algae (how deep it grows); the neutral islands' wheat half and fruit count | Sandy beaches (on) |
| Isles | Each colony's fields and stone deposits, and its algae patch (5×5 at 100) | Land bridges (on); Sandy beaches (on) |
| Old random | The area of each colony's wheat, wood, stone and algae squares | Colony meadows (on): the cleared grass square round each colony |
| Old islands | The area of each island's deposits | Extra starting deposit (on): the fourth deposit, of whichever of wheat or wood came out smaller |
| Contested commons | How many of the commons' zones are wheat, wood or fruit, its quarry's size and the moat's algae; home islands' fields are unscaled | Moat bridges (on); Jagged coastlines (on) |
| Maze | No new amounts: its existing Wheat, Wood, Stone, Algae and Fruit controls already set them | Sand roads (on); Treasure in dead ends (on): off, the same fruit is scattered along the passages' shores |
| Fjord continent | The ambient scatter, the core's stone and fruit clumps, the lake's and open sea's algae, and the banks' extra clumps; starter kits and bank guarantees are unscaled | Lake connects to fjords (off); Sandy lake shore (on); Fjord bank deposits (on) |
| Watershed | Separate wheat and wood farmland budgets, stone outcrops, confluence fruit groves, and algae at the mouths and in the shallows; starter kits are unscaled | River delta (on); Meandering rivers (on) |
| Stone highlands | The ponds' farmland (wheat and wood) and algae, and how much of the ridgeline is two tiles thick (stone, 0 to 200); kits are unscaled and Fruit still counts groves | Fruit in home valleys (off) |
| Symmetric arena | The farmland's wheat and wood, stone outcrops and algae, on top of Resource richness; fruit (0 to 100) keeps that share of the orchard's groves nearest the centre, never fewer than three | Moat (on); Stone in the orchard (on) |
| Ring world | The ambient scatter's wheat, wood, stone and fruit, and the shallows' algae; starter patches and island prizes are unscaled | Winding belt (on); Colonies on both coasts (on) |
| City states | Every home's ambient fields, outcrops and grove, everything on the commons (farmland, outcrops, groves, the orchard) and the sea's algae and island prizes; every home's kit and the walls' stone are unscaled | Stone walls (on): off, the causeways are plain roads and the homes' coasts are open |
| Tidal flats | Every island's ambient fields and outcrops and every island's prize; each home's kit is unscaled | Central island (on): off, the middle of the map is flats and there is no orchard |

`scaledCount` and `scaledShare` (`shared/Resources.h`) apply a percentage to a count or a share and
return it unchanged at 100. `setScaledResource` scales one `Map::setResource` square to a share of
its tiles, nearest the centre first, placed in `setResource`'s own order. Where an amount has to
add random draws (Contested commons' moat algae, Fjord's bank clumps), they come from streams of
their own, so the rest of the layout doesn't reshuffle.

A high amount can also bury a colony: resources block ground units and buildings alike, so a
widened farmland band or ambient scatter can wall a swarm into a pocket with nowhere to put a
building. `guaranteeStartingResources` does not cover that case — a colony buried in wheat has
wheat at its feet, so it counts as served — so `openCrampedStarts` (`shared/Resources.h`) clears
the resource tiles nearest such a colony, one ring at a time outwards, until it can walk to 16
tiles where a 4x4 building fits within 24 steps, and the caller then re-runs the guarantee in case
the clearing took the nearest crop too. A colony that already has the room is untouched. The
height-field generators (through `openStartsBuriedByAmounts`), Fjord and Ring world run it at any
non-default amount, and never at the defaults. Concrete islands and Isles need it differently:
their colonies' fields can cover every building site the start search looks at, so at a non-default
amount that search widens its window out from the default field rather than failing.

`placeSettlement` guards the same class of problem for the colonies it places: it measures the
workers' room (free tiles inside the home mask touching the swarm) before the swarm goes down, and
if the nearest-tie site the random pick landed on is short of room it takes the nearest site that
has it instead of failing the map. The pick itself is unchanged wherever the room was already
there, so this only rescues maps that used to fail outright.

## Resource placement

`shared/Resources.cpp` gives generators a small set of composable primitives rather than one
fixed resource pass:

- `placeResourceClump` / `placeResourceClumpInArea` grow a resource outward from a point to a
  requested tile count — the primitive every guaranteed placement (starter kits, bank deposits)
  is built from.
- `computeComponents` flood-fills the map into connected components of land (or of water). A
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
  have no growth rule to prefer and so use a plain noise field rather than fertility. Stone is
  shared out between landmasses and algae between water bodies (a lake apart from the sea), since
  algae only places on water.
- `scatterResources(game, context, densities)` is the shared ambient layer — ordinary, unclaimed
  deposits filling the interior between a generator's deliberate placements — used by Fjord and
  Ring world.
  It runs after every colony's swarm, workers and guaranteed clumps already exist:
  `isResourceAllowed` refuses any tile already holding a building or unit, so scattering earlier
  can claim a tile a swarm footprint needed; scattering last only ever loses a clump's own tile
  to whatever was already there, the same low-stakes trade every generator's own resource
  layering already makes.
- `guaranteeStartingResources(game, context, wheatRange, woodRange, clearRadius=0,
  protectedWalls=nullptr)` is a
  reachability backstop, not a placement pass: it compares a resource-respecting flood from each
  boot tile against the same flood with resources ignored, and where the two disagree — a
  colony's own tile is on well-connected land, just walled off from wheat or wood by a resource
  tile a ground unit can't stand on — it clears exactly the wall tiles responsible before topping
  up whichever resource is still out of range. A colony genuinely isolated on its own small spot
  (nothing reachable even through terrain alone) is left untouched, since there's nothing on the
  other side of a wall that isn't there. `protectedWalls` is an optional width×height mask of
  resource tiles that belong to the map's design, such as Stone highlands' ridgelines or City
  states' walls; they are
  treated like terrain, never cleared and never looked past. It wraps each boot tile onto the map
  first, since the height-field generators' fallback site search can hand over one past the edge.
  RuggedArchipelago, ShatteredCoast, Fjord, Watershed, Stone highlands, Ring world, City states and
  Tidal flats call this, as do the height-field generators, and Concrete islands and Isles at any wheat or wood amount
  other than 100.
  Maze doesn't need it: its deposits are placed only along passage shores, leaving a clear lane
  down every passage, and its `validateWorld` confirms every colony can still walk to every
  other.

Every guaranteed placement (starter kits, bank guarantees, per-team clumps) keeps wheat and wood
at a 1:1 ratio, since those exist for reachability fairness between colonies. Ambient/bonus
layers (`scatterResources`, Fjord's bank-scatter roll, Maze's scattered deposits) instead use 2:1 corn:wood, which is purely
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

The lobby's Landscape field opens `LandscapePickerScreen`, a full-window modal that shows every
playable landscape as a freshly generated map at the draft's current size and colony count, with
a Regenerate all button. `LandscapePreviewer` rolls those previews on background threads (one
roll per landscape, up to three seeds before a tile reports no preview) and hands back the seed
each shown map came from; the lobby then rolls that one seed instead of sampling five, so the map
a player picked by sight is the map the preview shows and the match starts on. Any later edit to
the draft drops the remembered seed and returns to candidate sampling. The picker takes only a
list of localized names and requests, so the editor or a multiplayer lobby can run it too.
Background rolls are safe because `syncRand()`'s state is per thread: a worker seeds its own
stream inside `GenerationService::generate` and never touches the menu's live colony on the UI
thread.

## Fjord continent

The richest generator, and the one most of this framework's resource work was proven against:

- **Shape.** A `ShapeTransform`-warped `RadialShape` coastline; an untouched core disc
  (`coreR`, currently 0.23x the continent radius) that every fjord stops short of, so it always
  stays connected land regardless of coastline roughness. A fjord is carved between every pair of
  angularly-neighboring teams as a smooth S-curve centerline from just outside the coast in to
  `coreR` (or, in lake-connected mode, well inside the lake — see below).
- **Central lake.** `lake-size` (0–90%, of `coreR`; 0 disables it) carves a lake at the exact
  center, ringed by a sandy no-man's-land wider than `Map::controlSand()`'s own coastal fringe
  (`sandy-lake-shore`, on by default; off, the lake has an ordinary beach).
  `lake-connected` (a switch, default off) decides whether the fjords actually cut through into the lake —
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
  corn/wood/stone along the same bank (`bank-deposits`, on by default). Each amount places its
  rolled clumps that many hundredths of a time, drawing any chance from a stream of its own.
- **Ambient layer and backstop.** `scatterResources` fills the continent interior at the end
  (corn:wood 2:1, fruit, stone; algae left to the dedicated shoreline/lake passes above), and
  `guaranteeStartingResources` runs last as the reachability backstop described above.

## Maze

A maze in the pen-and-paper sense, built to be lived in: grass passages that colonies farm and
build out into, separated by thin stone-and-water walls, with every colony in its own
cul-de-sac.

- **Grid.** The map is tiled into cells of about `cell-size` tiles on the map's own torus.
  Boundaries are chosen so the cells tile the map exactly (neighbouring cells differ in pitch by
  at most one tile), so the maze wraps across the seam like any other boundary. At least two
  cells are needed in each direction.
- **Maze and homes.** Homes are chosen first, by farthest-point spreading on the torus that only
  accepts a cell if every home still has a non-home neighbour and the non-home cells stay
  connected. The pattern depends only on the grid, so `validateRequest` checks exactly how many
  colonies fit; each map then places it at a random offset and mirror image. A recursive
  backtracker carves a spanning tree of passages over the non-home cells, and each home is
  attached by exactly one passage, so every colony starts in a genuine dead end. `loopiness` knocks through extra walls between non-home cells only, so homes
  stay cul-de-sacs.
- **Passages.** Every cell is a grass chamber and every open boundary a band of the same width
  joining two chambers, so a run of passage reads as one continuous strip of buildable,
  farmable grass. Passage width isn't a control of its own: passages fill whatever the narrowest
  cell leaves once its walls and channels are taken out, and stay odd so they centre on a tile.
- **Roads.** A three-tile sand road runs down the middle of every open passage, centre to centre,
  so every cell is linked to the rest of the maze by ground that can never be closed:
  `Map::incResource` only seeds a resource on its own terrain and buildings need pure grass, so
  nothing grows over a road or is built on one. At a home the road stops against the swarm's
  footprint. Shore distances for the resource scatter ignore the road. With `sand-roads` off
  (on by default), passages are grass from shore to shore, open to farmland and buildings.
- **Walls.** Every closed boundary has a stone spine covering the whole boundary line from corner
  to corner, with `channel-width` all-water tiles on either side (default 2). Perpendicular walls
  share their corner tile, so a boundary can only be crossed — on foot or swimming — where it's
  open. STONE only places on a pure-grass tile, which needs grass at all four undermap corners
  (`Map::regenerateMap`), so each spine sits on a two-wide grass core inside a sand ring. Terrain
  is stamped directly rather than through `Map::controlSand()`, whose in-place raster pass shifts
  shorelines unevenly.
- **No walking along a wall.** A wall's sandy flanks are walkable land, so they are always kept at
  least one all-water tile from every passage's shore — a unit can't step across a tile it can't
  stand on. From a cell's centre, a passage's grass therefore reaches
  `floor(pitch / 2) - 5 - channelWidth` tiles; `validateRequest` rejects settings that would
  leave a passage narrower than 9 tiles.
- **Resources.** Every home starts identical: fixed 1:1 wheat and wood banking the dead end's side
  shores, a compact stone deposit at its back wall, and a clear square around the swarm. Outside
  the homes, clumps of wheat, wood and stone (densities per 256 shore tiles) are scattered along
  every passage's shores, never more than three tiles in, so each passage keeps a clear lane down
  its middle however the maze turns. Fruit is treasure: every dead end that isn't a home gets one
  compact patch of `fruit` tiles near its far end, with fruit types dealt round-robin so every
  kind is somewhere in the maze. With `dead-end-treasure` off (on by default) the same fruit is
  scattered along the passages' shores instead. Algae is seeded along the channels.
- **Checked, not assumed.** `validateWorld` floods walkable tiles (water, buildings and every
  resource, including wall spines, block it) from colony 0's workers, and fails the candidate if
  any colony isn't reached.

## Watershed

A branching river system draining into a sea along one side of the map: fertile floodplains,
dry sandy uplands, and sand fords across the channels.

- **Layout.** The coast, springs, flow tree and fords are planned in a canonical frame from named
  streams, then oriented: the sea can face any side of a square map but faces a short end of a
  rectangular one, so the rivers run the long way. Each map has one river network.
- **Rivers.** Springs are sampled by land area (`river-density`) and join the network downstream
  of them nearest-first; a path that runs into another river becomes its tributary. Width grows
  with the square root of the springs upstream (`river-width`), and the trunk splits into one to
  three distributaries on a delta lobe (`river-delta`, on by default; off, one mouth and no lobe,
  with the other distributaries still planned so the rest of the network is unchanged). Every
  path meanders (`meanders`, on by default; off, rivers run without their meanders).
- **Channels that keep their water.** Channels are stamped wide enough to keep a four-connected
  water core under an all-at-once version of `controlSand`'s grass-to-sand rule;
  `Map::controlSand()` is then required to change nothing.
- **Fords and uplands.** `fords` sets the spacing of sand strips laid across straight stretches
  clear of every other channel. Ground farther from water than `dryness` allows turns to sand,
  never within 11 tiles of water. Springs rise inland, so a ford is the shortest crossing between
  banks, not the only route.
- **Resources and colonies.** Colonies take fertile floodplain sites spread across banks, each
  with an identical wheat, wood and stone kit. Farmland ribbons follow the rivers at 2:1 wheat to
  wood, with stone at the upland edge, fruit at confluences and algae at the mouths.
  `guaranteeStartingResources` runs with 16-step ranges. `validateRequest` allows one colony per
  1,024 map tiles.
- **Checked, not assumed.** `validateWorld` plans the layout again from the request and checks
  that every ford interrupts a real channel, is open across its width and has walkable land at
  both ends; that every channel keeps its water core outside fords; and that every colony can walk
  to colony 0 and stand beside wheat and wood (water, buildings and every resource block).

## Stone highlands

Open grass valleys divided by thin stone ridgelines, joined by passes, with a pond in every basin.
Stone is never used up and players can't clear it, so the passes are the only ground routes for
the whole game.

- **Ridges.** Basin sites spaced about `valley-size` apart are scattered on the torus, and every
  tile takes its nearest site after a periodic warp. A tile becomes ridge when any of its eight
  neighbours has a lower label, so no unit can slip through diagonally; noise thickens some
  stretches to two tiles (45% of the ridge at the default stone amount, which scales that
  share), and enclosed pockets under 40 tiles are filled.
- **Passes.** A random spanning tree of `pass-width` openings, cut mid-ridge, joins every valley;
  `loopiness` opens that share of the remaining shared ridges.
- **Homes and ponds.** Colonies take the roomiest valleys by farthest-point spreading, one per
  valley while they last. Ponds (`pond-size`, a percentage of each valley) then grow in basin
  interiors away from ridges, passes and homes, with two-tile beaches so algae can regrow; valleys
  under 120 tiles get none.
- **Resources.** Identical 1:1 wheat and wood kits beside every home, 2:1 farmland ringing the
  ponds, algae in the water, and `fruit` groves (per 128×128) only in valleys no colony starts in
  (unless `home-valley-fruit`, off by default, lets them grow in home valleys too).
  `guaranteeStartingResources` runs with the ridges as protected walls, and any resource left in
  or beside a pass is removed. `validateRequest` rejects maps smaller than four valleys or with
  fewer than 1,024 tiles per colony.
- **Checked, not assumed.** The layout is a pure function of the request, so `validateWorld`
  rebuilds it and checks that every ridge tile holds stone, every pass is open and leads to both
  its valleys, and every colony can walk to colony 0.

## Symmetric arena

An orchard island behind a moat at the exact centre, crossed by sand causeways, giving 2, 4 or 8
colonies identical starting ground.

- **Symmetry.** A half turn for 2 colonies; a quarter turn for 4 on square maps and both mirrors
  for 4 on rectangular ones; all eight square symmetries for 8, on square maps only.
  `validateRequest` rejects other colony counts and layouts that leave too little room.
- **Built, not copied.** Every decision is either a function of a tile's whole orbit (integer
  noise summed over the orbit, the integer squared radius) or is made once for colony 0 (home,
  pond, starting kit, shortest route to its causeways, swarm and workers) and stamped onto every
  image, with the swarm's top-left anchor recomputed per image. Terrain is written with
  `controlSand`'s rule applied to every corner at once, because the engine's row-order pass is
  order dependent, and resource amounts drawn from the engine RNG are equalised across each orbit.
- **Centre.** `centre-size` sets the island's radius and `moat-width` the water around it;
  `causeways` and `causeway-width` choose one gate straight towards each colony or two flanking it.
  With `moat` off (on by default) the island joins the land around it, with no causeways; the
  moat's width still spaces the homes. The orchard deals two-by-two fruit groves out an orbit at
  a time, turns some orbits to stone (`orchard-stone`, on by default) and always keeps all three
  fruits; the fruit amount keeps that share of the fruit orbits nearest the centre, never fewer
  than three. It is the map's only fruit.
- **Outside the ring.** Symmetric lakes (`lakes`), shore farmland, stone outcrops and algae scaled
  by `richness` and each one's own amount. Each home gets a pond and a fixed kit, and each
  colony's shortest route to its causeways is kept as clear land. `scatterResources` is not used.
- **Checked, not assumed.** `validateWorld` requires corner, terrain, deposit, building and unit
  invariance under every symmetry, with a consistent colony permutation that reaches every colony,
  and equal walking distances from every colony to wheat, wood, each fruit and the orchard. Only
  the starting state is symmetric: in-game growth uses the synced RNG. `scoreStarts` counts
  building sites by top-left anchor, so its fairness reads just under 1 on this map.

## Ring world

One continental belt wraps the map along its longer axis (horizontally on square and wide maps),
and the seas either side meet across the other wrap: there are no corners, and every colony has
exactly two land neighbours.

- **Belt.** The centre line and width are whole-number harmonics of the map length and the coast
  is lattice noise whose cells tile the torus, so the seam can't be seen. A dry spine beside the
  centre line and one shared coast budget keep the belt a single landmass and the ocean a band
  whatever `belt-width` (a percentage of the map's breadth) and `coast-roughness` ask for. With
  `winding-belt` off (on by default) the centre line runs straight round the map. Terrain
  is stamped with an order-independent beach pass rather than `Map::controlSand()`.
- **Lakes and islands.** `lake-density` adds inland lakes that keep land between them and the
  ocean and stay off the spine; `resource-islands` (per 128×128) adds themed islands out at sea.
- **Colonies and resources.** Evenly spaced, jittered slots alternate coasts (`both-coasts`, on
  by default; off, every colony takes the same coast) and sit on the
  spine-connected belt at one fixed distance from water, with identical starter patches; then
  `scatterResources`, a shallows algae pass and `guaranteeStartingResources`. A final road pass
  clears only the deposits on the cheapest walk from each colony to the next, closing the loop.
  `validateRequest` needs at least 24 tiles of belt length per colony.
- **Checked, not assumed.** `validateWorld` floods from colony 0 while counting seam crossings,
  with water, buildings and every resource blocking, and requires every colony reached, a walkable
  loop around the map, and a body of water that wraps beside the belt.

## City states

A large shared commons in the middle of the map, ringed by a strait, and round it one big home for
every colony: a wedge of the outer land with its own lake, fields and quarry, cut off from its
neighbours by water channels and from the commons by the strait. The only way off a home on foot is
its causeway, a road across the strait lined with stone, landing on the commons at the home's own
angle; a wall of stone round every home's coast keeps anything landing from the sea on the beach.
The commons is where the game is fought, richer towards its centre, where an orchard of all
three fruits stands round the central lake; once swimming pools let armies cross water anywhere, the
causeways stop being the only way in.

- **Geometry.** A pure function of the request (`geometryFor`). The commons' radius is
  `commons-size` percent of half the shorter side, the strait `strait-width` percent of the shorter
  side, and the homes reach out to the map's half side less a rim of sea, so opposite homes never
  meet across the wrap. Both coasts are radial shapes (`coast-roughness`); the strait follows the
  commons' coast. Every home is the same wedge turned round the centre, so the layout is fair for
  any colony count. `validateRequest` needs at least 20 tiles of home between the strait and the
  sea, and at each home's inner coast at least the causeway plus ten tiles of arc beyond its
  channel.
- **Causeways and walls.** One causeway per home at the wedge's middle: a `causeway-width` road
  across the strait with shoulders either side. With `stone-walls` (on), stone stands on every
  solid-grass shoulder tile and, round every home, on every solid-grass tile that touches the
  sea's margin (the land whose corners the beach reaches, and any beach joined to it), so every
  step off a beach lands on stone and the road is the only way in. Grass may never touch water, so the beach pass always
  leaves a sand lane outside the stone that a unit can land on and walk along but never leave; the
  causeway as a barrier, and as the ground kept clear of deposits with both its approaches,
  includes its lanes. Home lakes keep seven tiles from the sea so the two beaches never meet.
- **Every roll differs.** Both coasts are random harmonic profiles periodic in the wedge
  (`coast-roughness`): bays and headlands with a finer ripple on the commons' coast, which the
  strait follows, and bays into the flanks of every home's inner and outer coasts, all held flat
  around each causeway; every channel bows sideways by the same random amount. One home layout
  and one heart layout are drawn per map. Homes: Lakeland (one lake), Riverside (a creek from the
  lake towards one flank with a sand ford), Highland (two stone ridges out to the sea with a pass
  each), Marsh (four ponds) and Barrens (a band of sand with a corridor through it). Hearts: a
  lake with the orchard on its shore, a stone crag with a gap towards every landing, an island in
  the lake reached by a ford from every landing, a delta of rivers from the lake to the strait
  between the landings with a ford each, and a belt of forest round the orchard. `sand` adds
  patches of sand (per 64×128 of land) over homes and commons, clear of lakes, landings and
  coasts. Every home feature is designed in the wedge's frame, arc across it and radius out, and
  stamped into every home alike, so any roll stays fair by rotation.
- **Homes.** Each has one lake at six tenths of its depth (held clear of both coasts), the swarm
  between its causeway and the lake, an identical unscaled kit of 40 wheat and 30 wood beside the lake and a stone deposit beyond
  it, then its own scaled ambient farmland on fertile ground, outcrops and a grove.
- **Commons.** A central lake of `kHeartShare` of its radius, `valleys` extra lakes (per 128×128 of
  commons) likelier towards the centre, farmland on fertile ground weighted by depth inward per
  `frontier-richness`, outcrops and groves by the same weight, and the orchard of the three fruits
  on the central lake's shore. `resource-islands` (per 128×128 of sea) raises islets with a prize
  each out in the sea. Algae seeds every shallows. `guaranteeStartingResources` runs with the
  walls' stone protected, causeways and approaches are cleared, and a cheapest-walk pass keeps a
  way open from every swarm to its causeway and from every landing to the heart.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires every causeway road
  and ford walkable and every designed stone tile present (shoulders, walls, ridges and crag), every colony and the heart reachable on foot from
  colony 0, no colony reachable from any beach with the roads shut, no home able to reach the
  commons or another home with the causeways shut, and the colonies' walks to their landings within
  twelve steps of each other. `validateRequest` needs at least 20 tiles of home depth.

## Tidal flats

Grass islands standing on a wide expanse of walkable sand, with tide pools and lagoons over the
flats and the odd grassy sandbar. Sand carries units freely but holds no building and no deposit,
and nothing regrows beside it, so there are open-field battles from the first minute and no forward
bases: an army on the flats fights far from any inn or tower while a defender holds a rim of towers
on its island's edge, and expansion means taking another island whole.

- **Layout.** Every home island sits on a ring at 58% of the half side, one per colony evenly
  spaced from a random start, with a radius of `home-island-size` percent of the half side or as
  much as the ring, the wrap and the central island leave room for. Everything else -
  `extra-islands` neutral islands, `sandbars` and `lagoons` per colony and `tide-pools` per
  128×128 of flats - is placed in one wedge's frame, keeping clear of the home island, the wrap, the
  central island and each other, and stamped into every wedge alike, so the layout is fair for any
  colony count. `coast-roughness` shapes every island. `validateRequest` refuses a map whose home
  islands would shrink below nine tiles of radius.
- **Islands.** Each home has a pond at its middle, an unscaled kit of 40 wheat and 30 wood on the
  pond's two sides and a quarry towards the map's centre, then scaled ambient farmland on its
  fertile ground and an outcrop. Every neutral island is an oasis: a pond, and every other tile
  of it under unscaled wheat, so taking one means clearing it first, with one prize inside, a
  fruit grove or a stone deposit in turn. With `central-island` (on) an island at the centre carries a pond,
  the orchard of all three fruits and a quarry. Algae seeds every pool and lagoon, which is exactly
  where it regrows. Nothing is kept clear because the flats hold nothing.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires every home's pond
  present and every colony and the central island reachable on foot from colony 0, with water,
  buildings and every resource blocking.

## Compatibility notes

- A `GeneratorDefinition::legacyId` is a stable compatibility identifier, not a display or sort
  order — `GeneratorRegistry::builtins()`'s constructor order is purely the product-facing
  catalog order. Once assigned, an id is never reused, including after a generator is retired.
- A generator's `revision` changes only when a change to `generate()` changes what a given seed
  actually produces; a pure refactor, comment or renumbering change leaves it untouched. Bumping
  it is how a reviewer knows a seed's output isn't expected to match a prior build byte-for-byte.
- `GeneratorControl`s are validated by `GeneratorRegistry`'s constructor: every control's default
  must land on a valid step from its minimum, and (`allowedValues` aside) `(maximum - minimum)`
  must be evenly divisible by `step`. A `GeneratorControl::Kind::Toggle` control must be exactly 0 to 1 in
  steps of 1, with no allowed values, power-of-two formatting or terrain weight; the lobby and
  editor show it as a checkbox. Every control label needs matching entries in
  `data/texts.en.txt` (`[Label]` / `Label`) and `data/texts.keys.txt` (`[Label]`), the same as any
  other UI string.

## Verification tools

- `test/MapGeneratorStudy.cpp` builds to `MapGeneratorStudy`, a CLI harness that invokes the real
  production generators directly: `MapGeneratorStudy <method> <seed> <profile-dir> [key=value...]
  [tuning] [headroom] [quality] [dump=path]`. `quality` reports `StartQualityReport`/
  `ColonyQuality` per colony; `dump=` writes a plain-text terrain/resource grid for direct
  inspection or scripted flood-fill checks; `--catalog` dumps every registered generator's
  controls as JSON, with each control's `kind` (`range` or `toggle`).
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
