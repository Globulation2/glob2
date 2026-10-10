# Braided river

A glacial outwash plain: a wide belt of interlaced channels and gravel bars runs the long way
round the map, a dry terrace lies along either bank, and a wall of bedrock bluffs stands where the
two terraces meet across the far seam of the torus. Homes are on the terraces; the bars are the
fertile ground; on foot the only way from bank to bank is over the riffles (sand fords) that join
the bars this map, and which bars join which is redrawn every seed.

- **Threads.** `channels` sinusoidal channel threads (3 to 9; 6 by default) wrap the map a whole
  number of times, each in its own lane across the belt (`braid-width`, a percentage of the map's
  breadth, 40 by default), neighbouring threads in antiphase so every adjacent pair crosses twice a
  period and the land between two crossings is a lens-shaped bar `bar-size` tiles long (half the
  period, rounded to a whole number of periods round the lap). Each thread's phase drifts, its
  swing swells and its width (4.4 to 7 corners) swells and narrows over whole cycles of the lap,
  so the weave never repeats and the seam cannot be seen. Lanes are held between 11 and 26 tiles
  by dropping or adding threads, so bars stay bar-sized on every map: 3 threads on a 128 map, 6 on
  256 and 7 on 512 at the default width. Every channel keeps a 4-connected core of open water
  outside its riffles (`channelCoreFault`), so no unit steps over one.
- **Bars and riffles.** The bars are the land components the rasterized water leaves, the two
  terraces the components outside the belt. `channelCrossings` finds every stretch of channel two
  bars face each other across; a riffle (`SandFord`, three rows of sand across the channel) is
  tried at the middle of each chosen stretch on the sketch itself (`stampFord`, then `fordFault`:
  open water either side of it, dry across, land at both ends, and no other thread's centre line
  under its sand), and one that fails is unstamped and its stretch passed over. Chosen: one forced
  crossing per colony, from its terrace to the nearest bar with room for wheat and wood whose
  riffle works; a random spanning tree of the rest over `DisjointSets` (long stretches between
  real bars first, stretches of 4 to 8 tiles and scraps of 12 tiles or more only if the banks are
  still apart); and `extra-riffles` percent (25 by default) of the leftover bar-to-bar stretches
  as loops, never two riffles within 9 tiles of each other on one thread. A seed whose bars do
  not join the two banks is refused for another.
- **Terraces, homes and the wall.** Homes stand 8 to 13 steps up the bank on the larger terrace
  half of a slot, one slot per colony along the lap alternating banks, dealt to the colonies at
  random. The terrace beyond fifteen tiles of the water grows nothing, so a town there is never
  overgrown; the bank strip in front regrows thinly; the bars regrow richly. A sealed line of
  stone bluffs (`traceSealedLap`) waving round the seam keeps the two terraces from being one
  plain, since walking round the back of the torus would otherwise be shorter than crossing the
  braid. With `moraine` (on) a broken line of stone hummocks (`runsAndGaps`) stands four steps up
  each bank with a door at every riffle landing and none against a home.
- **Resources.** Every home gets an unscaled kit (wheat and wood beside the swarm along the bank,
  a stone clump behind) and its promised bar an unscaled wheat and wood clump; every bar's grass
  is furnished a third under crops in patches along its shores (`furnishGround`, 2:1 wheat to
  wood), the bank strips at a quarter of that, the dry terraces get finite woodlots and stone
  outcrops and, so the plain behind the moraine is not one sheet of grass (a maintainer's first look
  found it bland), patches of sand: `dry-patches` (0 to 30, 12) percent of each terrace's inland
  grass where a period-18 noise peaks (`sprinkleSand`, rounded patches rather than speckle), eight
  steps or more from water so the bank strip and the moraine's contour keep their grass, two tiles
  off the bluffs and hummocks (a sand corner spoils the tiles round it and stone stands on pure
  grass) and clear of every town's room, so no colony loses building ground to them. The bars
  nearest the belt's middle get a grove of fruit each (one per 8,000 tiles, at least
  three), and the channels algae where it regrows. `secureStartingCrops` and `connectColonies`
  run with the bluffs and moraine protected; `reopenCrampedStarts` at non-default amounts.
  `validateRequest` needs a terrace at least 22 tiles wide (a 128 map at 60% is the edge; 64 maps
  are refused) and 36 tiles of bank per colony on each bank.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires stone on every bluff
  tile, every channel's core outside its riffles, every riffle open across with walkable land at
  both ends (`fordFault`, `fordLandingWalkable`), every colony walking to colony 0, and every
  colony walking onto its promised bar and standing beside wheat and wood.
- **Played, and bimodal.** A rotation tournament (six 256×256 maps, four colonies, every cyclic
  team rotation, four Nicowars, 45,000 ticks) split the starts: on four maps every colony grew to
  66 to 155 units, while on two maps three of the four starts stalled at 15 to 25 units (peaks of
  26 to 44, nine to eighteen buildings) with almost no starvation, no trapped units, and a fifth
  of the harvest of the start that thrived. The start metrics do not separate the two (wheat two
  to five steps away, comparable fertility and room), so this is an AI stall on the dry terrace
  rather than a geometry defect the generator can measure; the defaults stand, and the retained
  maps are the material for a follow-up with other AIs and human play.
- **Played again with the dry patches.** The same protocol on revision 2 (12% of the terraces'
  inland grass in sand patches): the same economy within noise (pooled per-start units 58 to 96
  against 62 to 103, wheat harvested per colony 218 against 239, births 102 against 109), the same
  bimodal stalls on the same maps, and no significant position bias. The patches cost the terrace
  nothing a colony uses; they are the look of a dry plain.

## Implementation source

[BraidedRiverGenerator.cpp](../../src/map/generator/generators/BraidedRiverGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
