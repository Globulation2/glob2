# Honeycomb isle

**Honeycomb isle** (`honeycomb-isle`, numeric ID 53) is a city of hexagon blocks on an
island in a lagoon, cut in two by a wide river that a few bridges cross. Paved streets draw the
honeycomb. Pale wheat fields ring the island's edge and line the river. The blocks inside are ruins:
stone outlines broken open onto the street and filled with overgrown rubble, with a few open squares,
crater gardens round flooded craters, and the shell of a landmark at the middle with an orchard in it.
Every colony's home is two blocks with a cistern garden.

## How it plays

- **Crowded by design.** The city is only as big as the colonies need (`blocks-per-colony`), and the
  lagoon and river can't be crossed until units swim, so colonies start a block or two apart and
  nobody expands away from the fight.
- **Rubble is cover and lumber.** Rubble is wood: it blocks walking and building, and it is the
  city's main wood supply, so a colony builds its town by clearing its own cover. Cutting through a
  ruin opens a way into a neighbour's flank. Stone outlines can't be cleared, so ruins keep their
  shape and only their gaps and fill change hands.
- **Food on the edges.** Wheat fields fill the blocks along the lagoon and the river. The bridges are
  where the two halves of the city meet, and the landmark's orchard is the prize in the middle.

## How it is built

`src/map/generator/generators/HoneycombIsleGenerator.cpp`; the header comment records the original
vision, how it evolved and the tradeoffs.

| Stage | What happens |
| --- | --- |
| Blocks | A warped hexagon tiling (`hexTessellation`, pitch 150% of the block size) or squares at 140% (the same block area). Blocks shrink towards 16 on crowded maps. |
| River | A wandering band along one axis, all the way round the torus in three legs, a little off centre. Blocks it takes a fifth of are bank blocks. |
| City | The blocks nearest the middle, at most three fifths of the map's blocks, so a lagoon always surrounds it. A crowded map squeezes colonies down to three blocks each, and refuses them below that. |
| Homes | Two adjoining blocks per colony, spread farthest apart and kept off the river's banks, then dealt at random. One of three designs per map: **well yard**, **walled cellar** (the back of the outline still standing) or **garden court** (the well on the street between the blocks, stone posts round it). |
| Block kinds | The landmark (a **stadium**, **cathedral** or **station** shell round an orchard), craters (spread between the homes), wheat fields (every edge block, half the riverside blocks, and some inner blocks), and ruins, collapsed blocks and squares by weighted draw. |
| Streets | Paved with sand everywhere except inside a home. The sand seals every block, so each block is a plot of its own and no pond or field needs a sand ring. |
| Bridges | Sand decks across the river where it runs through the city, with rubble-free landings. |

On a small map the river can leave too few blocks away from its banks for the homes; the layout
then runs again without the river, and the lagoon still supplies the water.

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Block shape | Squares, Hexagons, Random (Random) | The tiling; Random selects a fitting concrete layout from the map seed. |
| Block size | 16–22 (18) | Target block size; crowded maps shrink it. |
| Street width | 2–5 (3) | Street bands; wider streets mean less building room. |
| Warp | 0–100 (50) | How irregular the blocks are (visual; no measurable effect on the economy). |
| Blocks per colony | 8–16 (11) | City size, and so how much of it is ruins. |
| Rubble | 20–90 (70) | How full the ruins are. |
| Outline gaps | 0–100 (40) | Chance each side of a ruin's outline is broken open; every ruin keeps at least one gap. |
| Craters per colony | 0–3 (1) | Crater gardens between the homes. |
| Wheat fields | Few, Normal, Many (Normal) | Few: half the edge and a quarter of the riverside blocks, none inside. Many: every edge and riverside block and more inner ones. |
| River width | 0–20 (12) | 0 removes the river. |
| Bridges | 1–6 (3) | Plus one per four colonies. |
| Wheat amount | 0–100% | Garden extras and how full the fields are (fields are full at 100). |
| Wood amount | 0–200% | Garden wood and rubble density (rubble is full by 150–200). |
| Stone amount | 0–300% | Masonry chunks in the rubble only; outlines are structural. |
| Algae, fruit amount | 0–300% | Algae in the shallows; the orchard grows from 0 to 6 groves. |

## Limits

- On 512-sided maps with few colonies the city is a small island in a large lagoon (about 85–94%
  water with 2–4 colonies).
- Rubble and fields use the wood and wheat sprites; there is no rubble graphic.
- Fairness is statistical, not exact: the ruins, craters and fields between homes differ, and square
  blocks scored lower on the start-fairness model than hexagons over six seeds (0.76 against 0.85).
  No rotation tournament has been run yet.
- Numbi and Castor stall on this map (they harvest wheat but barely breed); by the maintainer's
  decision maps are not tuned for the older AIs.
- No human playtest of the final version has been recorded.

The block-shape control defaults to Random, choosing a supported square or hexagonal layout
from the map seed. Explicit Squares and Hexagons retain their original stored values (0 and
1). Randomize Parameters chooses a concrete shape from the registered search subset, while
manual controls retain their full experimental ranges.

## Playtest limitations

Streets need to remain usable after growth and construction. Accessible global building room does not establish equally usable local expansion space.

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
