# Continents

A real continent from [the world atlas](world-atlas.md): North America, South America, Africa,
Europe, Asia or Oceania (`continent`, Random by default), floating in an ocean that wraps round the
torus, with the geography a player knows. The Great Lakes, Victoria and Baikal are lakes; the Sahara,
the Gobi and the Outback are sand; the Rockies, the Andes and the Himalayas are stone; the taiga and
the Amazon are wood; the Mississippi, the Nile and the Yangtze are rivers; and the plains people
farm are wheat country. A toy rather than a tournament map: the geography is fixed, no two colonies
get the same ground, and fairness is measured after the fact (the fitted model, the lobby's best of
several rolls) rather than proved by symmetry. What it promises is that every colony can start and
that the continent looks like itself.

- **The fit.** The region is fitted inside a sea margin (a 32nd of the shorter side, at least 3
  tiles: 4 on a 128 map, 8 on a 256, 16 on a 512), centred, turned a quarter turn when that fits a
  rectangle larger (`orientation`: Turn to fit, or Upright), and resampled by majority to the
  map's vertices (`Raster`). The margin is the torus's seam, so a continent never meets itself
  across the wrap. The rest of a rectangle is sea; round islets in it (`islets`, off by default)
  are invented for whoever wants swimming prizes, not drawn from the atlas.
- **The coast.** Land narrower than three tiles, specks of sea and islets under twelve tiles go
  (`cleanLandmass`); a filled pool takes the land round it. Ocean and lakes are water, deserts and
  ice caps sand, everything else grass with its character planted on it.
- **Rivers.** The great rivers (`rivers`, on) are lines of pure water a tile wide, bridged at every
  diagonal step so nothing slips between two tiles, with a sand ford wherever a noise field of period
  24 peaks along the river within 12 tiles. On maps where a tile is less than 0.3 of an atlas cell
  (128 and below) rivers are omitted: a river's scar is three tiles wide whatever the map's size,
  and there it would take more land than the lakes do.
- **Sites.** Candidates are squares of pure grass on the mainland (the largest walkable piece, with
  rivers counted walkable since they are forded and islets excluded), 4 tiles out on every side (a
  9x9 square, enough for a swarm and its clearing), at least 6 tiles from any river (a colony inside
  a river loop is boxed in whatever the ground count says), and of those the better half by the
  fertile farmable grass within 10 tiles (`windowCount` over the grass where `cropGrowthField` gives
  a crop a chance) when that leaves two per colony and still spreads, so nobody starts in a range
  or on a cramped cape while the plains stand empty; otherwise every square competes. `farthestSites`
  spreads the colonies by walking distance, preferring at each pick a site with 900 walkable tiles
  within 24 steps (its catchment on the bare sketch); the room relaxes to 3 and then 2 while the
  closest pair is under 12 steps, and a spread whose squares would overlap fails the request.
  Territories grow from each colony's square (`growTerritories`, equal area, wandering borders); each
  site walks to the middle of its territory for up to two rounds, a round undone when it brings the
  closest pair under 70% of the spread's spacing; then the sites are dealt at random.
- **Oases.** A site whose ground could not regrow its crops (mean growth chance over the 21x21 square
  round it under 2500 of 65536, measured on the beached sketch with `cropGrowthField`) gets a pond of
  32 water corners (about 20 pure tiles, which waters a 30-tile square) dug 6 to 11 steps away on its
  own ground (`digPond`), and up to two more, each two steps farther out, while it stays under the
  floor. The floor scales with `oases` (0-200, 100); at 0 the geography stands and a dry colony starts
  on its finite kit. The floor comes from the first playtest: colonies starting under about 2000
  stalled once their kit was cut, colonies above 2500 grew.
- **Furnishing.** Each kind of land is a region furnished with its kit (`furnishBiome`): plains
  `farmland`, forest `woodland`, steppe `savanna`, tundra `barrens`, ranges `highland` (`mountains`,
  on; off, a range is savanna). Fields go where the water makes crops regrow, a dry reserve of finite
  crops on the rest, cover in patches from a noise field. Round every home two clearings: no ambient
  deposit at all within 5 tiles (an 11x11 square; on a fertile river bank the fields had filled the
  home square itself) and no cover within 8 (17x17, room to build before cutting). Every colony's kit
  (20 wheat, 16 wood, a quarry) is unscaled and goes in the inner clearing; islets, when asked for,
  carry a prize each (`stockIslands`); algae in the shallows.
- **Routes.** `openColonyRoutes` under a cost model (a clearable deposit 3, stone 6, water 10) with a
  lane three wide cuts a pass through scree or a ford where a colony cannot otherwise walk to the
  first; then the crop guarantee, then cramped starts reopened at non-default amounts, then the
  routes once more, since a top-up can land on the lane just cut.
- **Checked, not assumed.** The design rebuilt from the request, the mainland off the map's seam,
  and every colony's walk from the first.

## Playtest focus

Geography changes when neighboring colonies make contact. Resource and building-room
scores do not capture every vulnerable start: compare complete seat rotations and
inspect first contact alongside local fertility and expansion. Recheck small maps,
rectangles, each continent shape and control extremes; request acceptance is defined
by the current validators, not a historical successful sweep.

## Implementation source

[ContinentsGenerator.cpp](../../src/map/generator/generators/ContinentsGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
