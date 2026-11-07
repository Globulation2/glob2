# River

**River** (`river`, numeric ID 2) is one long river looping across grass country. The bed runs the
whole way round the torus, straight or meandering, so the map is cut into banks that meet only across
water; ponds lie in the hollows on either side. Leo Wandersleb wrote it in January 2006 on the shared
height-field pipeline (`shared/Terrain.h`).

Revision 6 (River 2.0) redrew the finished terrain with the engine's full palette; revision 5 drew it
from grass, sand and water. The height field, the river's course, the resource bands, the starts and
their kits are exactly revision 5's for the same seed.

## How it plays

- **The river is the front line, swum slowly.** Colonies on opposite banks meet only by swimming or by
  walking round where the bed allows. The middle of the wide river and of the big ponds is deep water
  (`deep-channel`): swimmable at three quarters of the speed, no algae. At least three tiles of
  shallows line every bank, more on the wide river (the core holds at most half the water), so beaches,
  algae and swimming access work as before. Crossing by swimming is slower than in revision 5.
- **The banks are the richest ground.** A band of loam (Fertile: buildable crop land that waters the
  ground round it at 48/256 of water) runs two to seven vertices back from every shore, except within
  32 tiles of a swarm, where the AIs would read it as water and refuse to build near it. The open banks
  are richer than revision 5's and the homes' own ground is as it was (see the food table); the row
  where loam meets grass grows crops but takes no building.
- **Point bars and bluffs.** On the inside of the river's bends the beach and the sand behind it are
  mudflat (slow, unbuildable, holding nothing, and unlike sand it does not slow the crops behind it).
  On the outside of the bends low rock bluffs stand four to nine tiles back from the water, behind the
  stone that lines the bank (`bluffs`):
  impassable, towers shoot over them, scree round them. Most carry a quarry face of stone on a flank
  (how many follows `stone-amount`). A strip of walkable bank always runs between a bluff and the
  water, no bluff stands within 22 tiles of a home, and none parts any ground.
- **Far ground is dry.** Grass is lush beside the water and dry beyond 20 steps; beyond 28 steps from
  any water, where crops never regrow, heath patches (pavement: walkable and buildable, holding nothing,
  slowing crops near it as sand does) break the dry grass.
- **Desert banks.** The `banks` variant Desert is a Nile: one green ribbon along the water, dry grass
  beyond nine steps, baked hardpan pans (slow, unbuildable, holding nothing) beyond 16 steps where no
  crop could grow, and weathered sandstone pavement beyond 28 steps, with hoodoo bluffs. Pans keep 34
  tiles clear of every home, so every colony keeps its building room.

## How it is built

`src/map/generator/generators/RiverGenerator.cpp`; the header comment and the comments on the
constants record why each choice was made. The post-pass is built from the shared height-field helpers
(`shared/HeightFieldLooks.h`): it keeps a mechanic sketch of each vertex's canonical rule-group type
that every rule reads, and paints looks over it with `composeLooks` (`shared/Sketch.h`), so looks never
change a rule.

| Stage | What happens |
| --- | --- |
| Height field | `generateHeightField` unchanged: `makeRiver` stamps the bed, the lobby's weights become water, beach, grass and desert, resources are bands of the field, `chooseBalancedStarts` picks the starts, the starting-crop guarantee and groves run. |
| Bluffs | Planned from that terrain, its stone band and the starts alone: cutbank beach vertices of the river (`majorWater`) whose water share over a window a quarter of the bed's diameter (`wetShare`) is in the banks' bottom 20% and below 470 thousandths, strongest first; a crescent lens of walkable tiles 4 to 9 steps from water within about 10 tiles, never on the field's stone, kept in 2x2 blocks; centres 30 apart, 22 from every swarm, one per 80 tiles of width and height; dropped on islands under 1,500 tiles or where the walkable ring round it falls apart. A face of tiles left out of the rock on a flank (60% of bluffs at 100% stone, two flanks from 150%). |
| Mechanic layers | Deep core (`deepStepsForShare`, `deepenWater`): at least 5 steps from land, more until it holds half the water, bodies of 16 vertices or more. Plain grass round every quarry face, rock (`rockVertices`), scree one tile out where a noise peaks, never touching a deposit. Mudflat on beach and sand within three steps of the river at point bars (the banks' top 20% of share, above 530), and on grass one row up at the sharpest (top 8%). Loam two to seven vertices from water (`layLevee`), never in a garden, before a quarry or within 32 tiles of a swarm. Pavement (`paveBeyondWater`) on old desert sand and bare grass from 28 tiles in a straight line and 22 steps (`distanceFromWet`, frayed), never touching a deposit: 40% of it in fractal patches on the temperate banks, all of it in the desert, plus hardpan pans on 55% of the desert's bare grass from 16 tiles (15 steps) and 34 tiles from homes. |
| Looks | Loam and spring meadow, pale and dune sand, heath and shale (weathered sandstone and dirt), outcrop (hoodoo) rock; `writeLayeredTerrain` writes them and clears every deposit the new ground refuses. The grass looks, lush within 5 tiles of water (3 in the desert) and dry beyond 20 (9) (`gradeGrassLooks`), go on last, after the starts, so placeStarts and the starting-crop guarantee meet plain grass everywhere. |
| Algae and quarries | The algae the deep core displaced are seated again on the nearest free shallows; quarry faces get stone and the ground before them is cleared. |
| Starts | `placeStarts` and `openStartsBuriedByResources` as in revision 5, with the quarry faces protected from its clearing; then the grass looks. |

`validateWorld` keeps revision 5's floor (every colony harvests wheat within 24 and wood within 32
steps, with 16 building sites within 24), rebuilds the height field and its stone band from the
request (`heightFieldSketch`) to replan the bluffs from the colonies' start positions and requires every quarry
face to hold its stone (`quarryFault`), and refuses any blocking terrain that parts the walkable ground
round it (`partingBarrier`).

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Water, sand, grass, desert weight | 0–100 (45, 3, 75, 0) | The shares of the field; water sets how much floods, so the river and the ponds trade off. |
| Smoothing | 1–8 (4) | The field's feature size. |
| River width | 20–65 (35) | The bed's diameter as a percentage of the mean side. |
| Winding river | on/off (on) | Off, the river runs straight. Straight rivers have no point bars and few bluffs (bends are read from the banks' own spread). |
| Banks | Random, Temperate, Desert | Temperate: loam and spring meadow, heath patches in dry grass, outcrop bluffs. Desert: a green ribbon, hardpan pans and weathered sandstone, hoodoo bluffs. |
| Deep channels | on/off (on) | Deep water in the middle of the wide water: slower swimming, no algae there. Every route on foot is the same either way. |
| River bluffs | on/off (on) | Rock bluffs on the outside of the bends, quarry faces and scree. |
| Fruit | 0–64 (4) | Fruit groves, as before. |
| Wheat, wood, stone, algae amount | 0–300% (100) | The field's bands; stone also sets the share of bluffs with a quarry face (none at 0). |
| Stone on hilltops | on/off (off) | As before. |
| Repeat landscape | 0–5 (0) | As before; the post-pass runs over the whole map, so repeats differ in their bluffs and gardens. |

## Food

Growth potential (`growth_potential.c`: the engine's crop growth chance summed over farmland, loam
counted as farmland that waters at 48/256, pavement as an inhibitor, deep water as water), 256x256,
4 colonies, seeds 1-3, revision 6 against revision 5 on the same seeds and settings. Mean per colony,
worst colony in brackets. River's map report records no start positions, so each swarm's corner was
read from the dump's building tiles.

| Variant | Total | Within 24 of swarm | Within 48 |
| --- | --- | --- | --- |
| Revision 5 | 630 | 26.8 (0) | 81.5 (4) |
| Revision 6, Temperate | 634 | 26.8 (0) | 82.3 (5) |
| Revision 6, Desert | 656 | 26.8 (0) | 82.8 (5) |
| Temperate, deep channels off | 634 | 26.8 (0) | 82.3 (5) |
| Temperate, bluffs off | 698 | 27.2 (0) | 88.0 (5) |
| Desert, bluffs off | 725 | 27.2 (0) | 88.5 (5) |
| Revision 5, water 30 | 97 | 3.6 (0) | 8.1 (0) |
| Revision 6, water 30 (Temperate / Desert) | 92 / 91 | 3.7 / 3.5 (0) | 8.1 / 8.0 (0) |
| Revision 5, water 55 | 985 | 46.2 (12) | 145.3 (57) |
| Revision 6, water 55 (Temperate / Desert) | 1,044 / 1,081 | 45.8 (12) | 150.9 / 153.5 (48) |
| Revision 5, river width 20 | 1,738 | 70.5 (35) | 223.8 (139) |
| Revision 6, river width 20 (Temperate / Desert) | 2,067 / 2,168 | 70.2 / 70.3 (36 / 37) | 250.8 / 259.2 (141 / 147) |
| Revision 5, river width 65 | 26 | 1.3 (0) | 2.8 (0) |
| Revision 6, river width 65 (Temperate / Desert) | 26 / 25 | 1.3 / 1.2 (0) | 2.8 (0) |

The levees are the gain: seven vertices deep they water the land round them, and they pay for the bank
farmland the bluffs take (about 9% of the total; one bluff per 56 tiles of side cost 11%, so there is one
per 80). They keep 32 tiles off every home, so the food near the homes is revision 5's and the gain lies
along the open banks; levees right up to the homes gave about 7% more within 24 steps but left Nicowar no
place for its first inn (below). Two or three deep (behind the sand band) they lost 4%. Deep water
waters the land as plain water does. Pavement from 22 steps cost a low-water map 8%; from 28 it costs
about 5% of the total at water 30 and nothing near the homes. Hardpan lies only where no crop grows.
The one shortfall: at water 55 the worst colony's ground within 48 steps holds 48 against 57, a bluff
standing in it; the means are higher.

A four-AI calibration game (`map study river --seed 101`, 256x256, Nicowar, Cortex, Cabino and Maxima,
game seed 1, 20,000 ticks, both maps played by the same development game binary; seed 101 draws the
Desert banks) harvested 2,162 wheat and 371 wood with 164 worker births and 61 buildings on revision 6,
against 2,007 wheat, 306 wood, 165 births and 64 buildings on revision 5; Maxima had 48 births against
62, Cabino 55 against 31. The same seed with Temperate banks pinned: 2,142 wheat, 360 wood, 176 births,
52 buildings (Maxima 76). With the levees right up to 20 tiles of the homes Nicowar (team 0, beside the
river) built nothing but its swarm in all three games tried (11 to 16 births); with no levee at all it
had 35, and from 32 tiles 36. One game varies widely: this shows no stalled opening, not a measured
improvement.

## Limits

- Deep water swims at three quarters of the speed, so crossing the river by swimming is slower than in
  revision 5: a change in feel.
- Most of the map lies more than 20 steps from water at the defaults, so the far ground reads as dry
  grass and heath rather than green: the dry upland of a big lowland river. The Desert banks change
  play beyond the green ribbon: hardpan is slow and unbuildable.
- Straight rivers (winding off, or a seed whose bed runs straight) have no point bars and only a few
  bluffs where the bank wiggles.
- Bluffs take bank farmland and can stand within 48 steps of a home (never within 22 tiles); at water 55
  one colony's 48-step food is 16% below revision 5.
- The post-pass runs over the whole map, so a repeated landscape (`repeat`) is no longer identical patch
  for patch in its bluffs and looks.
- Generating a 512x512 map takes about twice as long as revision 5 in the same build (6.1 s against 3.1 s
  in an unoptimised development build; revision 5's release build takes 0.35 s); the validator rebuilds
  the height field to replan the bluffs.
- No rotation tournament or human playtest has been recorded for revision 6.

## Verification

Run the River contracts (`GLOB2_CONTRACTS=river`, `MapGeneratorContracts.h`), the toolkit checks for
`HeightFieldLooks.h` (`MapGeneratorToolkitChecks.h`) and the platform golden rows; sweep every size and
colony count against revision 5 per banks value. See [generator verification](verification.md) for
commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[RiverGenerator.cpp](../../src/map/generator/generators/RiverGenerator.cpp) owns this landscape's
construction, controls and validation; the post-pass helpers are in
[HeightFieldLooks.h](../../src/map/generator/shared/HeightFieldLooks.h). See the [catalog](catalog.md)
for its stable command and legacy IDs.

Related: [map generators](README.md).
