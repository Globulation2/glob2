# Honeycomb isle

**Honeycomb isle** (`honeycomb-isle`, numeric ID 53) is a board of terrain hexes on an island in
the sea, after the board game: every hex is one landscape (a wheat field, a forest, a pasture,
clay hills, a mountain, a lake), cobbled roads run along every hex edge, a river cuts the island
in two under a few boardwalk bridges, and a desert oasis with an orchard sits at the middle.
Every colony's home is two hexes with a cistern garden, bordered by two food hexes and a forest of
its own.

## How it plays

- **Crowded by design.** The island is only as big as the colonies need (`blocks-per-colony`), and
  the sea and river can't be crossed until units swim, so colonies start a hex or two apart.
- **One hex, one resource.** Food is in the fields and the water meadows along the river, wood in
  the forests, stone at the foot of the mountains and on the hills, building room on the pastures
  and hills. A colony grows by taking the hexes round it.
- **Contested ground between the homes.** Mountains (one per colony by default) and lakes are
  spread as far from the homes as the island allows, and the oasis orchard is the prize in the
  middle. The bridges are where the two halves of the island meet.
- **Fast roads.** Roads are cobbled road with gravel shoulders: fast, buildable, and nothing grows
  on them, so every hex is a sealed plot of its own.
- **The deep sea is a wall.** The sea deepens eight tiles from the shore, and a tile mixing
  shallow and deep water can be neither walked nor swum, so swimming opens the river and the
  coast, never a way across the sea.

## The hexes

| Hex | Terrain | Use |
| --- | --- | --- |
| Home (two per colony) | Lush grass, a cistern pond and garden | Swarm, starter crops, the first buildings |
| Field | Loam furrows (three vertex rows in six) under wheat | Food. The loam irrigates the field as the pond of earlier revisions did |
| Water meadow (the river takes a fifth of it) | Spring-meadow furrows under wheat | Food, watered by the river too |
| Forest | Trees on lush grass round a heart of moss and a thicket that never clears | Wood; the moss keeps it growing |
| Pasture | Grass with drifts of flower meadow, and a hedgerow across it with a gap | Building room; the hedge stops arrows |
| Hills | Clay and dirt with gravel crests, two or three outcrop knolls, a grass pocket | Building room and two stone |
| Mountain | A ridge-rock and outcrop massif with a deep-snow cap, a scree and boulder skirt | Eight stone at its foot; rock towers shoot over, boulders stop arrows |
| Lake | A pond with a dark deep middle, a garden round it, a mud rim | Food and wood between the homes |
| Oasis (the middle; two on maps of more than four colonies) | Dune and pale sand round a pond, lush grass and the orchard | The fruit prize |

The sea's beaches are pale sand with stretches of dunes; stretches of the river's banks are marsh;
bridges are boardwalk with dirt-track causeways on to the nearest road.

## How it is built

`src/map/generator/generators/HoneycombIsleGenerator.cpp`; the header comment records the vision,
how it evolved and the tradeoffs.

| Stage | What happens |
| --- | --- |
| Hexes | A warped hexagon tiling (`hexTessellation`, pitch 150% of the block size) or squares at 140% (the same area). Hexes shrink towards 16 on crowded maps. |
| River | A wandering band along one axis, all the way round the torus in three legs, a little off centre. Hexes it takes a fifth of are water meadows. |
| Island | The hexes nearest the middle, at most three fifths of the map's hexes, so the sea always surrounds it. A crowded map squeezes colonies down to three hexes each, and refuses them below that. |
| Homes | Two adjoining hexes per colony, spread farthest apart and kept off the river's banks, then dealt at random. Each home's free neighbours nearest its swarm (preferring hexes no other home borders) make up two food hexes, counting water meadows already there, and one forest. One of two garden designs per map: **well yard** or **garden court**. |
| Contested hexes | The oasis at the middle, then lakes and mountains, each by farthest-point spreading from everything placed before. |
| Dealt hexes | Half the riverside hexes not beside a home are fields (the Wheat fields control). The rest are dealt from a shuffled deck of fields, forests, pastures and hills whose counts bring the island's totals to the proportions of the weights, as the board game deals its tiles; a field or forest card that falls beside a home changes places with the next pasture or hills card. |
| Roads | Road down the middle, gravel shoulders against every hex, grass inside a home. |
| Bridges | Boardwalk decks across the river where it runs through the island, landings kept clear and a dirt-track causeway to the nearest road. |
| Looks | The layout is drawn with one terrain per mechanic; a last pass paints the looks that share each mechanic, and grass by its growth chance (lush where watered, dry where nothing grows). |

On a small map the river can leave too few hexes away from its banks for the homes; the layout
then runs again without the river.

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Block shape | Squares, Hexagons, Random (Hexagons) | The tiling; Random selects a fitting concrete layout from the map seed. |
| Block size | 16–22 (18) | Target hex size; crowded maps shrink it. |
| Street width | 2–5 (3) | Road bands; wider roads mean less room inside the hexes. |
| Warp | 0–100 (50) | How irregular the hexes are. |
| Blocks per colony | 8–16 (11) | Island size. |
| Forest density | 20–90 (70) | How full the forests are; scaled again by the wood amount. |
| Lakes | 0–3 (1) | Lakes per colony, between the homes. |
| Mountain ranges | Few, Normal, Many (Normal) | Half, one or one and a half mountains per colony (at least one). |
| Wheat fields | Few, Normal, Many (Normal) | A quarter, half or all of the riverside hexes, and the weight of fields among the dealt hexes. |
| River width | 0–20 (12) | 0 removes the river. |
| Bridges | 1–6 (3) | Plus one per four colonies. |
| Wheat amount | 0–100% | Garden extras and how full the fields are (full at 100). |
| Wood amount | 0–200% | Garden wood and forest density. |
| Stone amount | 0–300% | Quarry stone at the mountains (8 each at 100) and hills (2 each). |
| Algae, fruit amount | 0–300% | Algae in the shallows; the orchard grows from 0 to 6 groves. |

Revision 5 replaced the Rubble and Craters per colony controls with Forest density and Lakes, and
added Mountain ranges.

## Limits

- On 512-sided maps with few colonies the island is small in a large sea.
- Fairness is statistical, not exact: every home has the same garden, two food hexes and one
  forest beside it, and its other neighbours are pastures, hills, lakes or mountains. The river
  can give a home a third food hex (a water meadow it borders), which the
  `honeycomb-isle.home.extra-food` fallback records; a home with too few free neighbours gets
  fewer (`honeycomb-isle.home.short-of-neighbours`). Hexes further out are dealt at random.
- Roads are buildable; a building on a road closes it until it is gone. The hex network gives
  every hex several ways in.
- Numbi and Castor are not tuned for; by the maintainer's decision maps are not tuned for the older
  AIs.
- No human playtest of revision 5 has been recorded.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
platform golden rows, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[HoneycombIsleGenerator.cpp](../../src/map/generator/generators/HoneycombIsleGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
