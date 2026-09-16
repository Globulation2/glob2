# The game rules a map generator designs for

Every choice a generator makes — how wide a passage is, how far wheat lies from a swarm, why a
beach is always sand — follows from a handful of engine rules and from what makes a Globulation 2
game fun to play. This page collects both, with the code that enforces each rule, so a generator's
comments can say "because grass may not touch water" and a reader can check it here.

## The rules of the terrain

- **Terrain is drawn from corners.** The map stores an *undermap*: one terrain type (water, sand
  or grass) per tile corner. A tile's terrain is derived from its four corners
  (`Map::regenerateMap`), so a tile is pure grass only when all four of its corners are grass.
  A single sand corner spoils the four tiles around it. This is why a one-cell sand road
  (`tracePath`) blocks a strip about two tiles wide, and why generators stamp grass inside a
  one-tile sand ring.
- **Grass may never touch water.** There is no grass-to-water tile graphic, so every grass corner
  beside water must become sand. `Map::controlSand` does it in place in row order, which makes a
  shoreline depend on scan direction; the designed generators use `layBeaches`, which applies the
  same rule to every corner at once. The pass is not a step in a fixed order, it is a postcondition:
  anything that cuts terrain after it — a late pond, a farm plot stamped against a lake — needs
  another pass, or that shoreline ships as a hard grass/water edge that reads immediately as a bug
  in the finished game (the fractal maps' garden beds and bank plots did, 2026-09-16). Size a crop
  bed knowing the beach will take its innermost row: three rows of farmable grass means four rows
  of grass laid down.
- **Buildings need pure grass.** `Map::isFreeForBuilding` requires grass, no resource and no unit
  on every tile of the footprint (`Map::checkTile`). Sand is walkable but unbuildable, so a map's
  building room is its grass, not its land. A swarm is 4×4; the start scorer counts free 4×4
  footprints as a colony's room (`StartQuality`).
- **Water blocks walking until a colony can swim.** Ground units cannot enter water
  (`TileChecks::waterBlocks`) until they have trained at a swimming pool. Every island map, strait
  and moat is therefore a *timing* rule: it separates colonies early and opens once pools are
  built. Sand and grass are both walkable.
- **Resources block movement and building.** A unit cannot stand on a tile with a resource on it
  (`isFreeForGroundUnit` rejects resources), and nothing can be built on one. Wheat, wood and algae
  are *clearable* by clearing flags; stone and fruit are not. A band of deposits across a passage
  closes it until it is cleared, which is why generators keep lanes and roads clear and run
  `openRoad` after placing deposits.

- **Towers shoot over walls.** A defence tower is 2x2 and scans square rings round its footprint out
  to its range, 5, 7 or 9 tiles beyond every side by level, with no line of sight
  (`Building::findBestTarget`, `BuildingUtils::turretScanTile`, `BuildingTypesDefence.cpp`). A wall stops walking but not shooting, so
  a wall's thickness decides whether towers on either side can reach each other (`towerReach`).
- **Towers shoot over water too.** A straight channel w undermap corners wide spoils w + 3 tiles of
  grass and puts the banks' nearest grass w + 4 tiles apart, so a level-1 tower on one bank covers the
  other across a single water corner and a level-3 tower across five (`shared/Channels`). A canal can
  start a tower duel long before either side can swim.
- **Players can plug gaps.** Players build stone walls, so a narrow gate, ramp or trail can be sealed
  by whoever holds it. How wide a map's doors are decides whether a colony can shut itself in.

## The rules of resources

| Resource | Used for | Where it can be | How it grows |
| --- | --- | --- | --- |
| Wheat (corn) | Swarms turn it into units; inns store it to feed units | Grass | Grows and spreads, one visit in three, only where a random probe finds water and the opposite probe finds no sand |
| Wood | Every building site's construction; many upgrades | Grass | Grows and spreads under the same water/no-sand test as wheat |
| Stone | Upgrades of inns, hospitals, towers, barracks, schools, racetracks and pools; stone walls | Grass | Never grows and never runs out (not shrinkable): a quarry is permanent. Cannot be cleared |
| Algae | School and swimming pool upgrades, the top defence tower | Water | Grows and spreads only where a random probe (up to 15 tiles) finds water and the probe turned a quarter and doubled (up to 30 tiles) finds solid sand (`algaeGrowthChance`) |
| Fruit (cherry, orange, prune) | Stocked in inns. Each kind an inn holds raises its happiness level (`Building::availableHappynessLevel`), and a hungry unit that can see a happier enemy inn than any of its own walks there and is converted to that team (`Team::findNearestFood`); traded at markets. A unit that ate fruit also carries less armour for it (`armorReductionPerHappyness`) | Grass | Regrows in place wherever it stands; never spreads, never cleared |

The growth test lives in `Map::growResources` (`src/map/MapStep.cpp`), the resource table in
`src/game/entities/Resources.cpp`, and building needs in `src/game/entities/BuildingTypes*.cpp`.

Consequences a generator has to design around:

- **Farmland follows water, in rows.** A crop regrows when a probe up to 15 tiles away finds pure water
  and the opposite probe finds no pure sand, so rows of crops between rows of water yield most at about
  10 tiles of crops and 8 of water along an axis, 12 and 9 on the diagonal, where stepped edges lose
  more to the beach (`bestFarmRows`, `tools/farm_row_fit.py`).
- **Farmland follows water.** Wheat and wood planted far from water never regrow; planted near
  water they spread until cleared, harvested or blocked. Land far from any water is a desert for
  the economy even when it is grass.
- **Sand beside farmland slows it.** The growth test refuses when its probe lands on sand, so a map
  with lots of sand grows its fields back slowly — a lever, not just decoration.
- **Growth can shut a map.** Wheat and wood that spread unchecked cover grass, and covered grass is
  unwalkable and unbuildable. Lanes, sand roads and levees exist to stop a map growing shut.
- **Wood overgrowth is the usual way it happens, and it is slow enough to miss.** A deposit first
  thickens in place; once its amount passes a random 0-7 it *extends* to one of its eight
  neighbours instead. Timber scattered as decoration therefore creeps outward a tile at a time
  wherever the water probe succeeds, and tens of thousands of ticks later the map is forest. It
  does not show in a preview or in any measurement of the map as generated: it needs a long game,
  or a count of wood tiles on a final save against the same count at tick zero. Treat a sprinkle
  of timber across open land as a promise that the land will eventually be woods, and either keep
  the sprinkle sparse and contained or leave it out.
- **No-growth zones are forbidden on generated maps.** The engine keeps a saved per-tile
  `canResourcesGrow` flag, and it exists for hand-made scenarios such as the tutorial, where a
  designer freezes the ground a lesson needs. A generator may not set it on any tile: the shared
  structural check (`validateGeneratedWorld`) refuses a generated world with even one no-growth
  tile, and the toolkit no longer has a helper that sets it. A frozen tile is invisible to the
  player, stops farmland regrowing where they expect it to, and hides an overgrowth problem the
  design should have solved (the fractal maps used it and were stripped of it on 2026-09-16).
  Contain crops with terrain instead, which players can see and reason about:
  - **Sand.** A deposit cannot occupy sand, so a sand cap or aisle contains a plot permanently.
    One row of sand *corners* is enough: the two tile rows either side of it are no longer pure
    grass, and no crop can extend across them.
  - **Dry ground.** A wheat or wood deposit whose growth probe can never find water never
    thickens or extends. Scenery on dry ground (`Fertility::forMap` reads zero) stays the size it
    was planted.
  - **Leaving it out.** A copse that would have to be held back by anything else does not belong
    on fertile ground.
  Stone and fruit need none of this: neither extends (their resource types are not expendable).
- **Fruit is a weapon.** A colony whose inns hold all three fruits can pull hungry enemy units
  across to its side, so an orchard of all three kinds in contested ground is the strongest prize
  a map can offer, and fruit spread unevenly between colonies is a real unfairness.
- **Stone is strategic, not consumable.** One quarry serves a colony forever, so stone placement
  decides *where* colonies go for upgrades rather than how long they last.
- **Algae needs sand near its water.** Algae in open sea far from any beach never regrows.
- **Every placed deposit draws from the gameplay RNG.** `Map::setResource` draws each tile's amount
  from the synchronised random stream in call order, so the order deposits are placed in is part of
  the map. Reordering placement changes the map even when the positions are the same.

## What makes a good start

The shared tools encode a few measurable promises every generator is expected to keep:

- **Food and wood within reach.** `guaranteeStartingResources` tops up any colony without wheat
  within 24 steps or wood within 32 steps of its swarm. Early growth is walking time; a colony whose
  wheat is 60 steps away starves while its neighbour builds.
- **Room to build.** `openCrampedStarts` makes sure a colony can walk to at least 16 free 4×4
  building sites within 24 steps. A colony walled in by its own deposits cannot grow at all.
- **Workers can leave the swarm.** A clear ring of `kSwarmClearance` tiles round each swarm.
- **Fair starts.** `scoreStarts` measures each colony on wheat and wood distance, fertility, deposit
  depth, room and isolation from rivals; fairness is the weakest start divided by the strongest,
  and the lobby keeps the best-scoring of several seeds. Designed generators get fairness by
  construction instead: they design one colony's share and turn or mirror it onto every other
  colony (`WedgeFrame`), or slide it across a lattice of colonies with no centre at all
  (`translationSymmetry`, `shared/Orbits`), so every colony's ground is the same.

## What makes a good game

These are judgement rules rather than engine rules, learned from playtesting on this branch:

- **Contact should be a choice, not an accident.** Maps that put colonies behind water, walls or
  narrow routes give a player time to build before the first fight, and make the routes worth
  holding. A map where everyone touches from the first minute plays as a rush.
- **Remote ground should pay.** Coral puts fruit on its far tips, Spider web its orchard at the hub,
  City states its orchard in the commons: the riskiest land carries the best prize, so expansion is a
  decision.
- **Chokepoints need alternatives later.** Fords, causeways, passes and bridges decide early fights;
  swimming pools (and Stone highlands' extra passes) keep a map from locking into a stalemate.
- **The map must stay traversable.** Growth, deposits and buildings can all close routes; a map
  that silts shut is no fun. Roads, lanes and `openRoad` guard this.
- **Readable shapes.** A player should see at a glance where home, the front and the prize are.
  Distinct landforms (a web, a fan, a ring, a maze) help; a new generator should also be compared
  side by side with the existing ones, since two different algorithms can draw the same picture.
- **Defaults must work at every size.** 128×128, 256×256 and 512×512 maps, 2 to 12 colonies and
  rectangular maps are all in play; tuning that looks good at one size often breaks another, which
  is why several generators scale widths and angles with map size.

## Premade bases

A landscape may start a colony with a whole base standing (`shared/Bases.h`: The Glacis,
Allotments, Caravanserai). The engine rules it is built on, each verified in the source:

- **A building is raised with `Game::addBuilding`, which checks no room.** `checkRoomForBuilding`
  runs first. A finished type comes out of `Building`'s constructor complete (`hp = hpInit`, times
  the fortress-buildings multiplier), with empty stock, worker ratios of workers only, no bullets,
  and in no call list (`src/building/Lifecycle.cpp`).
- **Only level-0 construction sites can be placed.** A site type (`getTypeNum(name, 0, true)`) is
  valid straight from the constructor (`NEW_BUILDING`, hp 1); a higher-level site needs an
  `UPGRADE` state the constructor does not set, so a plan never asks for one.
- **Stock is written straight into `Building::resources`**, capped by the type's `maxResource`
  (an inn holds 10, 30 or 50 wheat by level; a swarm 20; fruit moves in tens), and a tower's
  bullets into `Building::bullets`. Stock, hp, ratios and bullets survive the lobby's save and
  reload of the generated map; `maxUnitWorking` does not (`Building::load` resets it), so nothing
  may depend on it.
- **`Team::createLists` runs exactly once per colony, after the last building.** It asserts its
  lists are empty and then rebuilds them, running every building's `update()`, which is what
  registers an inn to feed (only while its wheat exceeds the units inside) and a site to be worked.
  `placeTower` pushes into the turret list by hand because it runs after that, so a design raises
  its bases first and chooses wall towers second; never place a flag before the lists exist.
- **Units are placed with `Game::addUnit(x, y, team, type, level, ...)`** on a free tile
  (`isFreeForGroundUnit`, or `isFreeForAirUnit` for an explorer); `level` (0 to 3) applies to
  every ability. A team holds 1024 units and 1024 buildings.
- **The structural check counts only WORKER units**, against `GeneratorDefinition::startingWorkers`
  when set; a premade base's warriors and explorers pass freely.
- **A fed unit walks 264 tiles before it is hungry** (`HUNGRY_MAX` 150000 over 425 per completed
  move at level 0) and 352 before it starves, at 16 ticks a tile. Those are walked tiles, detours
  included: a map that forces long detours round rows, walls or crops can starve an army on its way
  to a base that looks close (Polder, until 2026-09-16, when its rows under crop were crossed only at
  the ditches). Keep colony-to-rival walks well inside that budget, with crossings through every kind
  of linear obstacle and forward inn ground on the way. On a map of short walks, inn spacing is about
  supply throughput (an inn feeds 4, 7 or 17 at once) and forward feeding, not survival.
- **Every AI adopts what it finds** (Echo and Nicowar through `BuildingRegister::initiate`, Numbi,
  Castor and Cortex by reading `myBuildings` live), but their openings drift: Cortex sets the first
  swarm's workers to 4 and tracks at most 24 sites and 16 inns; Nicowar does not count pre-placed
  sites towards its own cap and, in the first headless plays, bred warriors it could not feed. Keep
  a premade base's sites under a dozen and its regrowing food within a short walk of the swarm.
