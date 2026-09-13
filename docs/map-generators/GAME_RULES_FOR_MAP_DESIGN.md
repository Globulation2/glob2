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
  same rule to every corner at once.
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

- **Farmland follows water.** Wheat and wood planted far from water never regrow; planted near
  water they spread until cleared, harvested or blocked. Land far from any water is a desert for
  the economy even when it is grass.
- **Sand beside farmland slows it.** The growth test refuses when its probe lands on sand, so a map
  with lots of sand grows its fields back slowly — a lever, not just decoration.
- **Growth can shut a map.** Wheat and wood that spread unchecked cover grass, and covered grass is
  unwalkable and unbuildable. Lanes, sand roads and levees exist to stop a map growing shut.
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
- **Fair starts.** `scoreStarts` rates each colony on wheat and wood distance, fertility, deposit
  depth, room and isolation from rivals; fairness is the weakest start divided by the strongest,
  and the lobby keeps the best-scoring of several seeds. Designed generators get fairness by
  construction instead: they design one colony's share and turn or mirror it onto every other
  colony (`WedgeFrame`), so every colony's ground is the same.

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
