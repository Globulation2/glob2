# Resources and colony starts

This reference explains generator resource budgets, renewable supplies and colony placement.
Read [game rules](game-rules-for-map-design.md) before setting an economy budget.

## Resource amounts and switches

Every playable generator has amount controls for the resources it places (wheat, wood and stone
everywhere; algae and fruit where it places them) and one to three on/off switches for its own
sub-behaviours. An amount is a percentage of the generator's default, 100, from 0 to 300 in steps
of 25 unless noted. It scales the numbers that already decided that amount, and at 100 every map is
exactly what it was. Fairness placements (starter kits, 1:1 guaranteed wheat and wood, the
reachability backstop) stay unscaled wherever a generator has them, so an amount of 0 empties the
ambient layer but still leaves every colony a start.

Every landscape carries `wheat-amount`, `wood-amount`,
`stone-amount`, `algae-amount` and `fruit-amount` as percentages, with these exceptions, each
because the landscape has no such layer or names it differently: Anthill has no `stone-amount`
(its only stone is its walls); Old growth has no `wood-amount` (its `forest-density` is the wood
control); Isles, Old random and Old islands have no `fruit-amount` (they place no fruit); the
height-field generators (Swamp, River, Islands, Crater lakes) share `heightFieldResourceControls`
and keep their `fruit` control as a count of groves, 0 to 64. Maze's and Stone highlands' amounts
were counts of their own until that audit and are percentages since.

| Generator | What the amounts scale | Switches (default) |
|---|---|---|
| Swamp, River, Islands, Crater lakes | Each resource's band of the height field: algae the lowest sixth of the water, stone a third of the farmland share just above the beach, wheat and wood the rest as two separate bands, none past the top of the grass. Fruit still counts groves | Stone on hilltops (off): stone on the highest grass instead of by the shore. River also: Winding river (on) |
| Concrete islands | Each colony's wheat and wood fields (how far in from the coast they reach) and its six stone deposits; the channels' algae (how deep it grows); the neutral islands' wheat half and fruit count | Sandy beaches (on) |
| Isles | Each colony's fields and stone deposits, and its algae patch (5×5 at 100) | Land bridges (on); Sandy beaches (on) |
| Old random | The area of each colony's wheat, wood, stone and algae squares | Colony meadows (on): the cleared grass square round each colony |
| Old islands | The area of each island's deposits | Extra starting deposit (on): the fourth deposit, of whichever of wheat or wood came out smaller |
| Contested commons | How many of the commons' zones are wheat, wood or fruit, its quarry's size and the moat's algae; home islands' fields are unscaled | Moat bridges (on); Jagged coastlines (on) |
| Maze | The shore scatter's wheat, wood and stone (48, 24 and 16 tiles per 256 shore tiles at 100), every dead end's treasure (9 fruit tiles) and the channels' algae (24 per 400 water tiles); the homes' kits are unscaled | Sand roads (on); Treasure in dead ends (on): off, the same fruit is scattered along the passages' shores |
| Fjord continent | The ambient scatter, the core's stone and fruit clumps, the lake's and open sea's algae, and the banks' extra clumps; starter kits and bank guarantees are unscaled | Lake connects to fjords (off); Sandy lake shore (on); Fjord bank deposits (on) |
| Watershed | Separate wheat and wood farmland budgets, stone outcrops, confluence fruit groves, and algae at the mouths and in the shallows; starter kits are unscaled | River delta (on); Meandering rivers (on) |
| Stone highlands | The ponds' farmland (wheat and wood) and algae, how much of the ridgeline is two tiles thick (stone, 0 to 200), and the valleys' fruit groves (four per 128×128 at 100); kits are unscaled | Fruit in home valleys (off) |
| Symmetric arena | The farmland's wheat and wood, stone outcrops and algae, on top of Resource richness; fruit (0 to 100) keeps that share of the orchard's groves nearest the centre, never fewer than three | Moat (on); Stone in the orchard (on) |
| Ring world | The ambient scatter's wheat, wood, stone and fruit, and the shallows' algae; starter patches and island prizes are unscaled | Winding belt (on); Colonies on both coasts (on) |
| City states | Every home's ambient fields, outcrops and grove, everything on the commons (farmland, outcrops, groves, the orchard), the sea's algae; every home's kit, the islets' full wheat cover and the walls' stone are unscaled | Stone walls (on): off, the causeways are plain roads and the homes' coasts are open |
| Tidal flats | Every island's ambient fields and outcrops and every island's prize; each home's kit is unscaled | Central island (on): off, the middle of the map is flats and there is no orchard |
| Everglades | The swamp's standing wood and wheat, its outcrops and groves, and the pools' algae; every home's kit is unscaled | None |
| Spider web | The threads' standing wheat and wood, the share of knots carrying stone, whether the dew drops and the hub carry fruit and stone, and the shallows' algae; every pad's kit is unscaled | Spiral (on): off, the capture threads are closed rings; Sand roads (on): off, the threads are grass from shore to shore |
| Coral | The branches' standing wheat and wood, the share of forks carrying stone, the tips' fruit groves and the shallows' algae; every pad's kit is unscaled | Sand roads (on): off, the branches are grass from shore to shore |
| Plantations | Every neutral plantation's crop cover (the wheat and wood shares of its band), the orchard and rock islets' counts, and the sea's algae; every home island's cover, every colony's granted pools and inns and the rock islet beside every colony are unscaled | Outpost inns (on): an inn on each granted island; Causeways (off): sand causeways join a colony's own islands, so no unit need swim to work them |
| Carousel | Every home's ambient fields, the farms' wheat and woodlots, the courts' and the plaza orchard's fruit, and the lagoon's algae; the walls' stone and the starting towers are unscaled (homes have no separate kit) | Sand roads (on): off, the corridors and spokes are grass from wall to wall |
| Amphitheatre | Every territory's ambient fields and grove, the arena's groves and terrace outcrops, and the bays' algae; every home's kit, the walls' stone and the starting towers are unscaled | None |
| Switchbacks | Every home's ambient fields and grove, the farms' wheat and woodlots, the plateau's orchard, and the algae; the mountains' stone and the starting towers are unscaled (homes have no separate kit) | Sand roads (on): off, the trails are grass from wall to wall |
| The Glacis | The wadi banks' wheat and wood, the plain's outcrops, the groves beside the fords and the wadis' algae; every compound's well-side kit, quarry, walls, stock and starting towers are unscaled | Garrison (on): off, a compound starts with its colonists only |
| Allotments | The field lots' wheat and wood, the woodlots, the quarry and grove lots and the ditches' algae; every city's sites, wood stacks, stock and home fields are unscaled | Garrison (on) |
| Caravanserai | The outposts' quarry, orchard and algae; every capital's fields, stock and towers, and the oases' and outposts' wheat, are unscaled | Garrison (on) |
| Continents | Every kind of land's kit (`scaledBiome`): its farmland's wheat and wood shares, its dry reserve, its outcrops, its groves, and its cover (wood over forest, stone over the ranges); the shallows' algae; the islets' prizes are drawn unscaled and every colony's kit is unscaled. `oases` (0-200) scales the pond dug for a colony that starts with no water in reach | Mountain ranges (on): off, a range is savanna; Great rivers (on); Islets (on) |
| Braided river | Every bar's farmland and the terraces' thin bank-strip farmland, the dry terraces' woodlots and outcrops, the deep bars' fruit groves and the channels' algae; every home's kit, every promised bar's wheat and wood, the bluffs and the moraine are unscaled | Moraine hummocks (on): off, the terrace edges are open bank. Dry patches (12): the share of the terraces' inland grass turned to sand patches, 0 for the plain sheet of grass |

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
the clearing took the nearest crop too. A colony that already has the room is untouched.
`reopenCrampedStarts` runs that pair at any non-default amount and never at the defaults;
`openStartsBuriedByResources` runs it whatever the amounts are, which is what the landscapes on
the legacy core call because default abundance can also bury a start when no
explicit home-room budget exists. Concrete islands and Isles need it differently:
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
  is built from. Their optional row-major placement mask clips **every** clump tile, including
  wrapped edges; filtering only the centre can still spill a radius-two crop patch into a town.
  Null retains every existing caller's placement and random-draw order.
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
  protectedWalls=nullptr, allowedTopup=nullptr)` is a
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
  `allowedTopup` has a separate role: it confines *new* emergency wheat or wood to a chosen farm
  belt. If that belt is full of the opposite crop, the opt-in backstop trades at most a radius-two
  patch of accessible surplus crop for the missing one. It scores the current walking flood, so
  the new patch has an immediately reachable gathering edge. It does not trade protected walls,
  and maps that omit `allowedTopup` keep their former rescue behavior.
  RuggedArchipelago, ShatteredCoast, Fjord, Watershed, Braided river, Stone highlands, Ring world, City states,
  Tidal flats, Everglades, Spider web and Coral call this, as do the height-field generators, and Concrete islands and Isles at any wheat or wood amount
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

A zero-growth crop is finite stock and needs no sand ring to stop it spreading.
`cropSpreadEnvelope` optionally accepts the finished fertility field to include
those seeds without starting a spread flood from them; renewable seeds still use
the conservative grass-component flood. `containedPlotsMismatch` can additionally
accept an explicit mask for finite wheat, while retaining the existing sealed-plot
checks. Keep these reserves separate from renewable capacity in economy budgets;
see [sustained economies](game-rules-for-map-design.md#from-a-viable-opening-to-a-sustained-economy).

## Colony placement and fairness

Two further shared pieces score and choose where colonies actually start, beyond a generator's
own terrain and resources:

- `shared/BalancedStarts::chooseBalancedStarts` (used by the four height-field generators)
  scores every legal site by its *worse* primary resource — a colony beside wood but far from
  wheat is not a good start — using the finished, as-built state: the swarm's clearing already
  cleared, its footprint impassable, the flood starting from where workers actually stand. It
  takes the narrowest score window that still holds enough mutually distant sites, falling back
  to the older any-legal-site search on maps where no set of sites can reach both resources at
  all.
- `shared/StartQuality::scoreStarts` measures the *finished* map's colonies: the walk to every
  resource, the stock standing within 12, 24 and 48 walking steps and how much of it no rival
  reaches sooner, buildable room, territory held outright and contested, ground fertility, and
  the distance to the nearest and farthest rival. What those measurements are worth is not
  decided here: `FairnessModel.h` turns them into a start's fitness with coefficients fitted to
  thousands of real games, a softmax over the map's colonies turns fitness into a chance of
  winning, and the map's score is the fairness of those chances — 1 when every colony is as
  likely to win as any other. See [FAIRNESS_MODEL.md](fairness-model.md). `scoreStarts` also
  stamps the same `Fertility::Field` it computes for
  scoring into `Map::Tile::fertility`/`Map::fertilityMaximum`, which is otherwise only ever
  populated by the editor's fertility tool or old-save migration — without this, every generated
  map's in-game fertility overlay would show nothing at all.

`GenerationService` rolls `kSampledCandidates` (5) seeds from a root seed and keeps the
best-scoring one that generates successfully; `GenerationService::bestSeed` is the same search
used by the map editor's regeneration action. Generation is deterministic and the score is a
pure function of the finished map (asserted by the `MapGeneratorDefaults` suite), which is what
makes "roll several, keep the best" and "regenerate the winning seed later" both sound.

The lobby's Landscape field opens `LandscapePickerScreen`, a full-window modal that shows every
playable landscape as a freshly generated map at the draft's current size and colony count, with
a Regenerate all button. `LandscapePreviewer` rolls those previews on background threads (one
roll per landscape, up to three seeds before a tile reports no preview) and hands back the seed
each shown map came from; the lobby then rolls that one seed instead of sampling five, so the map
a player picked by sight is the map the preview shows and the match starts on. The lobby's own
five candidate rolls run on the same workers: the best-scoring seed is rolled once more on the
UI thread for the snapshot, so a large map with many colonies no longer freezes the lobby while
its candidates roll. Any later edit to
the draft drops the remembered seed and returns to candidate sampling. The picker takes only a
list of localized names and requests, so the editor or a multiplayer lobby can run it too.
Background rolls own independent target Games. `GenerationService::generate` seeds each
target map's private streams from the request; it never binds a global/thread-local RNG
or touches the menu's live colony. Named generation streams remain request-owned.

### The worker count

The structural check after generation (`validateGeneratedWorld`) requires every colony to hold a
swarm and exactly as many WORKER units as the lobby's shared "Starting workers" control (1 to 8).
Grant additional non-worker units only when the landscape explicitly requires
them; the shared worker count remains authoritative.

Related: [map generators](README.md).
