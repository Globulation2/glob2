# Ring world

**Ring world** (`ring-world`, numeric ID 16) is one continental belt that wraps the map along its
longer axis (horizontally on square and wide maps), with ocean either side that meets itself across
the other wrap: there are no corners, and every colony has exactly two land neighbours along the belt.

Revision 4 (Ring World 2.0) redrew the map with the engine's full terrain palette; revision 3 drew it
from grass, sand and water. The belt, its lakes and islands and every colony's site are laid exactly as
revision 3 laid them for the same seed. Revision 6 draws each rule group in one look per climate;
every rule, deposit and start is revision 5's.

## How it plays

- **Two fronts, sea behind.** Colonies sit in evenly spaced, jittered slots round the ring, 6 tiles
  from water, alternating between the belt's two coasts (`both-coasts`). Each has one neighbour each
  way along the belt and the ocean at its back.
- **A road right round the world.** A trail road (`ring-road`) runs from each colony's doorstep (five
  tiles outside its ground, so the building room round the swarm stays whole) to the next colony's, all the way round. Road walks at twice grass speed
  and takes buildings, but holds no deposit and nothing grows on it or across it, so the ring can
  never be overgrown shut and the road is the natural line for forward inns and towers. It keeps off
  the wet farmland near water where it can.
- **A broken spine down the middle.** Rock (`rock-spine`; impassable on foot, towers shoot over it)
  runs along the belt's dry centre line in segments of 16 to 30 tiles with passes of 5 to 8 between
  them, cut again wherever the road crosses and within 16 tiles of every home. It divides the two
  coasts without ever parting them: it stands at least 8 steps from any water, so it never meets a
  lake or a coast, and a flood checks that no rock cuts any ground off.
- **A quarry tor for every colony.** Each colony has a small outcrop of the spine's rock with a
  five-tile quarry face of stone turned towards it, on its own side of the spine, the same walk from
  every swarm (16 steps to the face's front where every colony's ground allows, otherwise the nearest
  walk all of them can have). Every colony's tor is the same stencil turned by quarter turns, so every
  face holds the same five stone. Nothing is planted within 3 tiles of a tor, and the walk to every
  face is opened after the resources are laid. With the spine off there is no rock and no quarry.
- **Deep ocean swims slowly.** Ocean six tiles or more from any land in a straight line is deep water (`deep-water`):
  swimmable but slower, no algae. The coastal shallows keep their beaches and the algae band, and an
  island keeps a ring of shallows. Raiding across the ocean or reaching the islands takes longer than
  in revision 3; every route on foot is unchanged.
- **Coasts with character.** In the bays the beach is mudflat (slow, unbuildable, holds nothing, but
  unlike sand it does not hold back the crops behind it); at the tips of headlands it is boulders
  (impassable, blocks arrows). Neither comes within 18 tiles of a home, so every home's shore is as
  in revision 3.
- **Green lakes.** Every lake's beach is loam (buildable crop land that waters its surroundings a
  little), inside the home gardens too, and outside the gardens (12 tiles round a home's middle) a
  frayed ring of loam about five vertices deep surrounds it. The lakes are the belt's richest
  farmland. Beyond a lake's beach the gardens stay plain grass, so a lakeside home and an inland one
  start on comparable ground; a lakeside home's kit takes the wettest ground, which can be beside
  its loam beach rather than where revision 3 put it.
- **Themed islands.** Islands out at sea carry one prize each: stone islands are volcanic (a pumice
  beach round grass carrying the stone); fruit and grain islands carry the belt's grass.

## How it is built

`src/map/generator/generators/RingWorldGenerator.cpp`; the header comment and the comments on the
constants record why each choice was made.

| Stage | What happens |
| --- | --- |
| Belt | Centre line and width are whole-number harmonics of the map length and the coast is lattice noise whose cells tile the torus, so the seam can't be seen. A dry spine beside the centre line and one shared coast budget keep the belt one landmass and the ocean a band whatever `belt-width` and `coast-roughness` ask. |
| Stamp | Revision 3's terrain: grass, lakes (`lake-density`, kept off the spine and 5 tiles from the ocean), islands (`resource-islands`, 7 tiles of water round each) and the order-independent beach pass (`layBeaches`); then the deep ocean core (ocean 6 tiles or more from land in a straight line, opened, bodies under 12 vertices dropped; straight-line distance keeps its edge following the coast, where eight-neighbour steps drew it in stairs). Planned from the request alone. |
| Colonies | Revision 3's slot search, unchanged. |
| Tors | For every colony, every tile within 32 steps' walk on its side of the spine and 10 tiles off the centre line is tried with each of four facings; the stencil (11 by 5 tiles of rock with the face left out of its near rim) and one tile round it must be dry land 3 steps from water off every home's ground, and the face, the rock behind it and the ground before it plain grass. The common walk is the one that keeps the worst colony nearest it and itself nearest 16 (a step of miss costs three of distance); each colony takes its candidate nearest that walk, facing its swarm, furthest inland. All or none. |
| Road | Doorsteps five tiles outside each colony's ground, furthest from water; waypoints on a line between consecutive colonies keep every leg under a quarter of the ring, so each walk goes forwards. Each leg is the cheapest walk (10 a step, 14 diagonally; 3 more per step nearer than 10 to water; up to 15 more from a noise, so it wanders; 40 more within 8 tiles of a colony's ground), never on water corners, within 4 tiles of a colony's ground or within 2 of a tor. Every road tile's corners are trail. |
| Spine | Runs along the ring from a random start; across, the rock is 1.8 to 2.8 tiles each side (tapering at a run's ends) of a centre line wandering 3 tiles off the belt's own; none within 8 steps of water, 16 tiles of a home or 3 of the road or a tor; cores kept only where they are two tiles thick, pieces under 10 tiles dropped. |
| Mechanic sketch | On a copy of the stamp: lake loam rings; mudflat and boulders on ocean beach vertices by the share of ocean in a 6-tile disc round them (bays under 42%, headlands over 56%, each on half of them by a noise), clear of homes, the road and the tors; volcanic islands; a frayed scree apron round the spine 12 or more steps from water; the road; the rock (`rockVertices`). Any group of boulders or spine that parts the walkable ground round it (the quarry faces counted as blocking) is dropped and the sketch painted again, for up to 24 rounds; a sketch still parting after the last is refused. |
| Resources | The quarry faces' stone first; then, as in revision 3, every home's kit (never within 3 tiles of a tor), themed island prizes, the ambient scatter, shallows algae, the ground before every face cleared, the starting-crop promise and cramped-start reopening (quarry stone protected); the walk from every swarm to its face opened (a deposit costs four steps); the belt road backstop (`openBeltRoad`, never over a face). |
| Looks | Painted last with `composeLooks` by climate, so no stage above saw them: one look per rule group (grass, beach, deep water, loam, mud, headland, rock, road), gardens included, plus pumice on volcanic islands' beaches. |

`validateWorld` floods from colony 0's workers (water, buildings and every deposit blocking) and
requires every colony reached and a walkable loop round the map; requires a body of swimmable water
that wraps beside the belt; no deep vertex beside land; no deposit on a tile with a road corner; no
rock or boulders parting the walkable ground round them; then plans the stamp and the 2.0 features
again from the request and the colonies' start positions and requires every quarry face holding its
stone, five for every colony (`quarryFault`), and every road tile road. `validateRequest` needs at
least 24 tiles of belt length per colony.

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Belt width | 30–70% (45) | The share of the map's breadth the belt covers on average. |
| Coast roughness | 0–100 (50) | How far the coast wanders, within the shared budget. |
| Lake density | 0–8 (2) | Lakes per 4,096 tiles of belt. |
| Resource islands | 0–10 (2) | Islands per 128×128 of map; each carries one themed prize. |
| Winding belt | on/off (on) | Off, the centre line runs straight round the map. |
| Colonies on both coasts | on/off (on) | Off, every colony takes the same coast. |
| Deep ocean | on/off (on) | Deep water six steps from land: slower swimming, no algae. Routes on foot are the same either way. |
| Rock spine | on/off (on) | The broken spine and every colony's quarry tor. Off, no rock and no quarry; the sites are the same either way. |
| Ring road | on/off (on) | The trail road round the world. Off, the belt is grass between the colonies, as in revision 3. |
| Climate | Random, Temperate, Steppe, Tropical | Looks only: Temperate (green grass, spring meadow, pale beaches, grey rock, granite headlands, trail), Steppe (dry grass, loam, hoodoos, sandstone road, hardpan bays), Tropical (lush grass, moss, dark deep water, mossy road, mud). All three play identically. |
| Wheat, wood, stone, algae, fruit amount | 0–300% (100) | The ambient scatter and the shallows' algae. Every kit, island prize and quarry face stays. |

## Food

Growth potential (`growth_potential.c`: the engine's crop growth chance summed over farmland, loam
counted as farmland that waters at 48/256, deep water as water, sand and barren ground as
inhibitors), 256x256, 4 colonies, seeds 1-3, revision 4 against revision 3 on the same seeds and
settings. Mean per colony, worst colony in brackets.

| Variant | Total | Within 24 of swarm | Within 48 |
| --- | --- | --- | --- |
| Revision 3 | 1,684 | 98.0 (50) | 301.2 (251) |
| Revision 4, defaults | 1,902 | 112.2 (68) | 340.4 (262) |
| Each climate pinned | 1,902 | 112.2 (68) | 340.4 (262) |
| Deep ocean off | 1,902 | 112.2 (68) | 340.4 (262) |
| Rock spine off | 1,915 | 114.5 (68) | 344.9 (270) |
| Ring road off | 1,937 | 116.7 (68) | 348.2 (276) |
| Revision 3, one coast | 1,684 | 114.7 (50) | 290.9 (239) |
| Revision 4, one coast | 1,869 | 124.6 (67) | 318.8 (266) |
| Revision 3, no lakes | 892 | 60.9 (43) | 120.2 (104) |
| Revision 4, no lakes | 888 | 60.3 (43) | 119.5 (104) |
| Revision 3, lake density 6 | 2,017 | 115.5 (84) | 381.3 (279) |
| Revision 4, lake density 6 | 2,379 | 132.3 (103) | 438.9 (308) |

The lakes are the difference: their sand beaches held back the crops round them, and loam beaches
and rings water the ground instead, about 13% more overall and 14% near the homes. The road and the
spine cost a little (2% and 1%): they keep off the wet ground, and nothing grows on road or rock.
Climates are looks only and deep water waters the land as plain water does. With no lakes the map
is within 1% of revision 3 (888 against 892; the worst colony the same): the road and spine's small
cost is not repaid by the lakes. The tool counts a tile between loam and grass corners as nothing,
though crops grow there, so the gain is understated. An ash ring on the volcanic islands' ground was
tried and cost 12% of the potential with no lakes (barren ground inhibits like sand and an island is
all shore); they keep their grass.

## Limits

- **Calibration.** Nine four-AI games (`map study ring-world --seed 101`, 102 and 103, 256x256,
  Nicowar, Cortex, Cabino and Maxima in that order, game seeds 1 to 3, 20,000 ticks, both revisions
  played by the master release binary) on the current design (12-tile gardens, straight-line deep
  ocean):

  | Map seed | Wheat (3 / 4) | Wood (3 / 4) | Births (3 / 4) | Buildings (3 / 4) | Weakest colony's wheat (3 / 4) |
  | --- | --- | --- | --- | --- | --- |
  | 101 | 9,160 / 7,336 | 1,495 / 1,116 | 690 / 546 | 230 / 216 | 1,052 / 1,157 |
  | 102 | 5,332 / 8,139 | 937 / 1,285 | 421 / 614 | 164 / 237 | 382 / 1,141 |
  | 103 | 8,226 / 9,160 | 1,044 / 1,159 | 697 / 708 | 175 / 224 | 550 / 823 |
  | All | 22,718 / 24,635 | 3,476 / 3,560 | 1,808 / 1,868 | 569 / 677 | 1,984 / 3,121 |

  Revision 4 harvested 8% more wheat and 2% more wood, with 3% more births, 19% more buildings and
  the weakest colony's wheat 57% higher; seed 101 is weaker than on revision 3, seed 102 much
  stronger. Per seat over the three games of each map (births): seed 101 209 / 152 / 115 / 70,
  seed 102 259 / 82 / 145 / 128, seed 103 217 / 246 / 58 / 187. Maxima in seat 3 stays small on seed
  101 (28, 17 and 25 births): its building placement treats loam, like water, as ground to keep
  clear, and the loam rings near its home take its building room. That is an AI defect, being fixed
  in the AI rather than by keeping loam off the homes. Cabino in seat 2 on seed 103 (16, 19 and 23
  births) was not investigated. Single games vary widely; this is a calibration, not a tournament.
- **Quarry walk.** The planned walk to every face is equal (16 steps where all colonies' ground
  allows); the walk measured on the finished map, round deposits, is within two steps on the seeds
  checked (the contract allows six). Where some colony's ground has no room near 16 the common walk
  moves out, up to 32.
- **Deep ocean on small maps.** At 128x128 the ocean between the coasts is often narrower than
  twelve tiles, so there may be no deep water at all.
- **Generation time.** Revision 4 plans the road, tors and spine and then plans them again in
  `validateWorld`: on the same -O0 development build a 512x512 map takes 11.0 s of CPU with 4
  colonies and 14.4 s with 12, against 4.3 s and 5.9 s for revision 3 (the master release build
  takes 0.45 s and 0.65 s for revision 3, so about 1.2 s and 1.6 s are expected for revision 4 in a
  release build).
- **Looks.** The grass grades and lake rings are drawn from distance to water, frayed by noise; the
  road is the cheapest walk on a grid, so it runs in straight and diagonal stretches.

## Implementation source

[RingWorldGenerator.cpp](../../src/map/generator/generators/RingWorldGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
