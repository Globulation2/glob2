# Rain shadow

**Rain shadow** (`rain-shadow`, numeric ID 27) is a map with a grain. Long rock ridges run across the
torus, parallel and a valley apart, and the wind blows across them from one side. The windward foot of
every ridge catches the rain: a chain of streams, spring seeps between them and the rock, and the
farmland. The lee side is dry: a skirt of scree at the foot of the rock, then a band of sand
downwind, where nothing grows or builds. Passes cut every ridge at intervals
staggered from ridge to ridge, so moving along a valley is easy and crossing is slow. Every colony
sits in the middle of a valley with the same ridge behind it, the same streams before it and its own
quarry face at the foot of the rock straight upwind of its town.

Revision 7 (Rain shadow 2.0) rebuilt the map for the engine's full terrain palette; revision 6 drew
the ridges as stone deposits on grass and everything else from grass, sand and water. Revision 8 gives
each role one look (lush grass by the water and plain grass beyond, one sand per climate, spring
meadow seeps). Revision 9 moves every quarry face out of the rock onto open ground at its foot, so the
ridges stay whole (before, the faces were cut into the rock and read as holes in the mountain).

## How it plays

- **The ridges are rock; stone is a place.** Ridge rock is impassable and permanent, and towers
  shoot over it (the Ridges rule group), so the passes are doors worth holding. Stone stands only on
  quarry faces, open ground at the rock's foot: one under the ridge behind every home, the same for
  every colony (at least 8 stone), and more along both feet of every ridge (`stone-amount`), plus
  the odd outcrop in the valleys.
- **The windward foot is the farm.** Crops regrow only near water, so the streams decide where the
  fields are. Between the streams and the rock lie spring seeps: crop land that waters its
  surroundings at 48/256 of water.
- **The lee is a desert you can walk.** Sand inhibits growth and takes no building, so an army on it
  is far from any inn and a forward base must wait for the next valley. A scree skirt at the foot
  of the rock is slow ground (walk 160/256), unbuildable and barren, but it does not inhibit growth.
- **Passes never grow shut and are slow to rush.** Every pass floor is sand, with a sand road running
  18 tiles into the valley on both sides (on the windward side it ends in a lane joining the
  streams' beaches). The mouths of every pass are frayed scree.
- **Every valley is the same valley.** The ridges' sway fades out across every home's valley, so the
  ridges run straight behind and before every town and every home is the same distance from both;
  what lies along the valley (lakes, rivers, sand patches, which pass is nearest) is where it fell.

## How it is built

`src/map/generator/generators/RainShadowGenerator.cpp`; the header comment and the comments on the
constants record why each choice was made. The design keeps a mechanic sketch (WATER, SAND, GRASS,
LOAM, SCREE, RIDGE_ROCK: the canonical type of each rule group, which every shared helper and check
reads) and paints looks over it with `composeLooks` (`shared/Sketch.h`), so looks never change a rule.

| Stage | What happens |
| --- | --- |
| Ridges | `ridges` crossings tilted by `slant`: stripes that wrap the torus exactly (`stripePhase`); on a small map ridges are taken away until a valley holds a home with its margins, the streams and the sand, then homes shrink, and only then is the map refused (as revision 6). |
| Homes | On a lattice, each slid along the wind to the middle of its valley by the straight phase and snapped to the nearby tile whose phase is nearest the middle. A round clearing (home radius + 4) and a home pond. |
| Quarry and passes | The face of the ridge straight upwind of colony 0's home is found; the pass pattern is shifted along the ridges, and the quarry slid along the valley wall (the same way for every home), to keep every home's quarry clear of the passes. |
| Sway | The ridge warp (12% of a spacing, 40-tile cells) fades to nothing across every home's valley and eases back over 24 tiles. |
| Rock | The band round every crest, drawn a tile thicker than `ridge-thickness` (1.5 for 2), less the passes. A pass that would still cut a home's quarry is closed whole. Colony 0's stretch of ridge round its quarry is stamped onto every home's and the quarry face (7 wide, the open ground within 2 steps of the rock, never a pass) copied with it. Rock vertices only where all four tiles are rock (`rockVertices`). A flood across every ridge with the passes shut must find no way over, or the band is thickened half a tile and rebuilt. |
| Water | Streams along the windward foot (20 long every 24, 7 deep, four tiles out from the ridge's nominal edge), none within 4 tiles of a pass; inland lakes joined by a wandering river to the nearest stream of their own valley (one whose straight line crosses no rock and no pass floor: water is kept off the passes, so a river through one was cut in two). No water within 3 tiles of a quarry. |
| Lee | A scree skirt at the lee foot of the rock (`talusApron`, one tile, frayed to three), then sand as far as `lee-width` downwind of the rock (`upwindSteps`), so it stops where a pass lets the rain through. No scree on the windward foot: the gap there is spring seeps. |
| Ambient quarries | Faces of 3 to 10 tiles of open ground within 2 steps of the rock, on both sides of the ridges, one per 48 tiles of face at the default `stone-amount`, clear of passes, roads, homes and water; the ridge is never cut, and the sealing check still drops any face that would open a way across it. |
| Terrain | Mechanics, sand patches (`sprinkleSand`), beaches (which never eat the rock), then the looks. Quarry tiles and their approach stay plain grass. |
| Resources | Home kits (14 wheat, 12 wood by the pond), farmland on the fertile ground (`furnishGround`: a third under wheat, a fifth under wood), outcrops, fruit, algae, quarry stone; the starting-crop guarantee, cramped-start relief and route opening never touch the rock or a quarry. |

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Ridges | 2–8 (4) | Crossings of the map; fewer on a small map so the valleys fit. |
| Slant | 0–3 (1) | Ridges climbed following one across the width; with a slant every ridge is one spiral, which staggers its own passes (straight ridges stagger by parity). |
| Ridge thickness | 2–6 (3) | Nominal rock thickness; the rock blocks about a third of a tile more (more where the sealing check thickens it). |
| Pass spacing | 16–48 in steps of 16 (48) | Tiles between passes along a ridge, rounded to a whole number of passes per turn along it (the pattern must wrap the torus). A turn is the map's side over the hypotenuse of ridges and slant, about 62 tiles on a 256 map with four ridges and slant 1, so 16, 32 and 48 give four, two and one pass per turn there. Revision 7's first range, 24 to 96 in eights, gave one pass from 48 up: half its steps were dead. At 16 the passes crowd the homes' quarries and several close (six on seed 1 at the defaults otherwise). |
| Pass width | 3–9 (5) | Width of every pass. |
| Lee width | 2–12 (6) | Depth of the lee sand band. |
| Home size | 10–20 (12) | Home radius; shrinks to fit the valley. |
| Inland lakes | on | Lakes with rivers in the middle of the valleys. |
| Sand patches | 0–20 (6) | Percent of the valleys' grass in sand patches. |
| Climate | Random, Temperate, Steppe, Desert | Looks only: how far from water the grass is lush (3, 2, 2 steps; plain grass beyond, which the home gardens match) and the map's one sand (sand, dune sand, pale sand). Seeps are spring meadow in every climate. Plays identically. |
| Rock | Random, Granite, Sandstone, Volcanic | Looks only: ridge rock (ridge rock, hoodoos, outcrop) and its talus (scree, scree, pumice). Plays identically. |
| Wheat, wood, algae, fruit amount | 0–300% (100) | Ambient layers; the kits are guaranteed. |
| Stone amount | 0–300% (100) | Ambient quarry faces and outcrops; every home's quarry is guaranteed. |

## Food

Growth potential (`.agents/skills/glob2-map-design/scripts/growth_potential.c`: the engine's crop
growth chance summed over farmland, by rule group), 256×256, 4 colonies, seeds 1–3, revision 7
against revision 6. Climate and rock are looks only and every one measures the same.

| Revision | Total | Within 24 of swarm (worst) | Within 48 (worst) |
| --- | --- | --- | --- |
| 6 | 930 | 37.2 (22) | 101.4 (73) |
| 7, any climate and rock | 881 | 37.8 (27) | 113.0 (94) |

The revision 7 row was measured again on 2026-11-07: the first figures (942, 38.2, 123.5 within 48,
worst 104) predate later changes on the branch, and the review fixes since (pass spacing range,
rivers kept out of passes) leave the default maps of seeds 1–6 tile for tile as they were.

Spring seeps are counted as farmland only where a tile is wholly seep, and their own watering
(48/256) is counted, so the seep strip is if anything undercounted. The terrain dump of `map study`
counts a tile whose corners are different looks of one rule group as that group (before
2026-11-07 it counted them as mixed ground, which read about a sixth less food on maps with looks).
Scree on the windward foot was tried and dropped: a frayed one-tile skirt there cost 6% of the
map's growth potential; on the lee side, in front of the sand, scree costs nothing.

One calibration game (seed 101, 256×256, Nicowar, Cortex, Cabino and Maxima, 20,000 ticks) gave
165 worker births, 2,245 wheat and 384 wood harvested and 64 buildings against revision 6's 113,
1,457, 276 and 49; on revision 6's map Cortex stalled at 8 units, on revision 7's every colony grew
past 44. One game on different terrain is a calibration, not a measurement.

## Limits

- The ridges are straight across every home's valley (the sway fades out there), so on maps with
  many colonies much of the ridge length runs straight.
- The rock blocks about a third of a tile more than `ridge-thickness` says (more where the sealing
  check thickened it, on steep stretches of thin ridges), so a level-1 tower's reach across a
  ridge is a little shorter than on revision 6.
- Stone is much scarcer than on revision 6, where every ridge was stone: each home has one quarry
  face at the rock's foot (at least 8 stone; 18 at the defaults), and the ambient faces and outcrops scale with
  `stone-amount`.
- A pass that cannot be shifted clear of a home's quarry is closed (telemetry
  `rain-shadow.passes.closed`); with one pass per turn that could cut a ridge off, which
  `validateWorld` refuses, but no seed tried did it.
- Hardpan pans in the far lee were left out: hardpan does not inhibit growth, so it would change
  what the lee band is for.
- Homes are identical in their clearing, their distance to both ridges and their quarry; what lies
  along the valley (lakes, rivers, sand patches, the nearest pass) is where it fell, as on
  revision 6.

## Implementation source

[RainShadowGenerator.cpp](../../src/map/generator/generators/RainShadowGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
