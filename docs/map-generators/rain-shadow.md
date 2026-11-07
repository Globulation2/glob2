# Rain shadow

**Rain shadow** (`rain-shadow`, numeric ID 27) is a map with a grain. Long rock ridges run across the
torus, parallel and a valley apart, and the wind blows across them from one side. The windward foot of
every ridge catches the rain: a chain of streams, spring seeps between them and the rock, and the
farmland. The lee side is dry: a skirt of scree at the foot of the rock, then a band of sand that
drifts into dunes downwind, where nothing grows or builds. Passes cut every ridge at intervals
staggered from ridge to ridge, so moving along a valley is easy and crossing is slow. Every colony
sits in the middle of a valley with the same ridge behind it, the same streams before it and its own
quarry face cut into the rock straight upwind of its town.

Revision 7 (Rain shadow 2.0) rebuilt the map for the engine's full terrain palette; revision 6 drew
the ridges as stone deposits on grass and everything else from grass, sand and water.

## How it plays

- **The ridges are rock; stone is a place.** Ridge rock is impassable and permanent, and towers
  shoot over it (the Ridges rule group), so the passes are doors worth holding. Stone stands only on
  quarry faces: one in the ridge behind every home, the same for every colony (at least 8 stone),
  and more along both faces of every ridge (`stone-amount`), plus the odd outcrop in the valleys.
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
| Rock | The band round every crest, drawn a tile thicker than `ridge-thickness` (1.5 for 2), less the passes. A pass that would still cut a home's quarry is closed whole. Colony 0's stretch of ridge round its quarry is stamped onto every home's and the quarry face (7 wide, 2 deep) copied with it. Rock vertices only where all four tiles are rock (`rockVertices`). A flood across every ridge with the passes shut must find no way over, or the band is thickened half a tile and rebuilt. |
| Water | Streams along the windward foot (20 long every 24, 7 deep, four tiles out from the ridge's nominal edge), none within 4 tiles of a pass; inland lakes joined by a wandering river to the nearest stream of their own valley (one that crosses no rock). No water within 3 tiles of a quarry. |
| Lee | Sand from the rock as far as `lee-width` downwind (`upwindSteps`), so it stops where a pass lets the rain through; scree skirt in front of it (`talusApron`, one tile, frayed to three). |
| Ambient quarries | Faces of 3 to 10 tiles on both faces of the ridges, one per 48 tiles of face at the default `stone-amount`, clear of passes, roads, homes and water; a face that would open a way across the ridge is given back to the rock. |
| Terrain | Mechanics, sand patches (`sprinkleSand`), beaches (which never eat the rock), then the looks. Quarry tiles and their approach stay plain grass. |
| Resources | Home kits (14 wheat, 12 wood by the pond), farmland on the fertile ground (`furnishGround`: a third under wheat, a fifth under wood), outcrops, fruit, algae, quarry stone; the starting-crop guarantee, cramped-start relief and route opening never touch the rock or a quarry. |

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Ridges | 2–8 (4) | Crossings of the map; fewer on a small map so the valleys fit. |
| Slant | 0–3 (1) | Ridges climbed following one across the width; with a slant every ridge is one spiral, which staggers its own passes (straight ridges stagger by parity). |
| Ridge thickness | 2–6 (3) | Nominal rock thickness; the rock blocks about a third of a tile more (more where the sealing check thickens it). |
| Pass spacing | 24–96 (48) | Tiles between passes along a ridge. |
| Pass width | 3–9 (5) | Width of every pass. |
| Lee width | 2–12 (6) | Depth of the lee sand band. |
| Home size | 10–20 (12) | Home radius; shrinks to fit the valley. |
| Inland lakes | on | Lakes with rivers in the middle of the valleys. |
| Sand patches | 0–20 (6) | Percent of the valleys' grass in sand patches. |
| Climate | Random, Temperate, Steppe, Desert | Looks only: how far from water the grass stays lush and green and how much of it dries, and the sand's colours. Plays identically. |
| Rock | Random, Granite, Sandstone, Volcanic | Looks only: ridge rock (ridge rock, hoodoos, outcrop) and its talus (scree, scree, pumice). Plays identically. |
| Wheat, wood, algae, fruit amount | 0–300% (100) | Ambient layers; the kits are guaranteed. |
| Stone amount | 0–300% (100) | Ambient quarry faces and outcrops; every home's quarry is guaranteed. |

## Food

FOOD_TABLE

## Limits

LIMITS

## Implementation source

[RainShadowGenerator.cpp](../../src/map/generator/generators/RainShadowGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
