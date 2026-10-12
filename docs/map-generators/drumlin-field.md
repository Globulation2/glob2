# Drumlin field

A lake land: a swarm of long oval grass hills, all pointing the way the ice went, with water in every
hollow between them, and between the hills wind eskers, sinuous sand causeways that are the only dry
way from one drumlin to the next until a colony can swim. Every colony starts on one of the biggest
drumlins: its blunt head is the town, a collar of sand across the waist parts the town from the tail,
and the tail is the colony's farm. Everything is fertile, so farm drumlins grow shut over the game;
the beach round every drumlin stays walkable, so the shoreline is always a road.

- **Grain and swarm.** A `Grain` (`grain`: random, horizontal, vertical or diagonal; a random grain is
  one of eight whole-step headings, refitted to the widest if it leaves the homes too close) with a
  stretch of `drumlin-length` percent (150-400, 250). Homes on a lattice (`latticeSites`, dealt), then
  darts under the grain `drumlin-spacing` apart (16-32, 20, widened by whatever `water-gap` exceeds
  its default 6 by, so a wider gap widens the lakes rather than thinning the drumlins;
  `spreadPoints` with the homes fixed and kept clear by two home radii and the gap), three rounds
  of relaxation under the grain with the
  homes held still (`relaxPoints`; without it the drumlins covered a quarter of the map, with it two
  fifths of the sketch), packed radii (`packLandforms`) with `water-gap` map tiles of open water
  (4-12, 6) between any two drumlins whichever way they lie, and a `Teardrop` fitting each radius,
  its widest point 35-45% back from the head.
- **Homes.** `TeardropHome` (Homes.h): the first 45% of the length is the town (swarm 25% back from
  the head, never more than 12 tiles short of the collar), a two-tile sand collar across the waist, the tail the farm: wheat 14 and wood 12 planted
  just past the collar (`teardropHomeKit`, `plantSplitKit`), a quarry near the head's tip, and the
  rest of the tail furnished as farmland like any farm drumlin. No home pond: the lakes are the
  water. Building room is scarce by design; the lobby's seed ranking needs no allowance for it,
  since the fitted fairness model compares towns with each other rather than with an open plain.
- **Sizing.** Homes are `home-size` (8-16, 11) half-width, shrunk to fit between their nearest
  neighbours under the grain, and shrunk again (to the floor of 8) so a farm drumlin fits at the
  lattice cells' centres between them; below the floor the request is refused
  (`planHomes`, the request check, plans only this much).
- **Eskers.** The drumlins' cells under the grain (`nearestSiteLabels` with a `Grain`) give the
  neighbour graph; a near tree (`carveNearTree`, 30% length jitter) joins every drumlin and
  `esker-loops` percent (0-100, 25) of the drumlins' count opens second ways; each link is a
  wandering corridor three corners of sand wide (`carveOpenEdges`, wander 3), laid over water only.
- **Prizes.** Farmland (farm drumlins and home tails) gets wheat 30% and wood 12% of its fertile
  grass in patches, the farm drumlins an outcrop per 2000 tiles and a grove per 1500
  (`furnishGround`), and half a colony's worth of orchards of all three fruits on the drumlins
  farthest from every home and each other (`farthestSites`, `plantOrchard`). Algae on the
  best-growing half of the water.
- **Checks.** With every tile touching an esker corner shut no colony walks to another
  (`colonyLeak`: the drumlins do not touch); no town grass touches tail grass (`firstRegionLeak`:
  the collar holds); with them open every colony walks to colony 0. `openColonyRoutes` clears crops
  only, never fords: an esker that fails is a failed map.
- **Cost.** A 512 map takes about 1.4 s against 0.4-0.5 s for Rain shadow or Stone highlands:
  four whole-map labellings under the grain (three relaxation rounds and the cell graph), each
  searching the bounding box of the grain's reach ellipse round every tile.

- **Played.** A rotation tournament (six 256×256 maps, four colonies, every cyclic team
  rotation, four Nicowars, 45,000 ticks) found the lake land healthy and unhurried: pooled
  per-start peaks of 110 to 190 units, 30 to 65 warriors, one colony in 96 eliminated (the
  drumlins are islands until the pools are built), prestige on every map. One start in six
  maps stalled at 35 units. The generator's colony index 2 won 14 of 23 adjudicated games and
  had the largest population on four of six maps although sites are dealt at random and the
  start metrics show no index pattern; treat that as an open question for a larger sample, not
  a measured defect.

## Implementation source

[DrumlinFieldGenerator.cpp](../../src/map/generator/generators/DrumlinFieldGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
