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
  (`deep-channel`): swimmable at three quarters of the speed, no algae. The core is measured in a
  straight line from land, so its outline is round. At least three tiles of shallows line every bank,
  more on the wide river (the core holds at most half the water), so beaches, algae and swimming access
  work as before; the algae the core displaces are seated again on the nearest free shallows. Crossing by swimming is slower than in revision 5.
- **The banks are the richest ground.** A band of loam (Fertile: buildable crop land that waters the
  ground round it at 48/256 of water) runs two to seven vertices back from every shore outside the
  homes' own gardens. The banks are richer than revision 5's (see the food table); the row where loam
  meets grass grows crops but takes no building.
- **Point bars and bluffs.** On the inside of the river's bends the beach and the sand behind it are
  mudflat (slow, unbuildable, holding nothing, and unlike sand it does not slow the crops behind it),
  never where a deposit stands, so the bars take no stone or crops.
  On the outside of the bends low rock bluffs stand four to nine tiles back from the water, behind the
  stone that lines the bank (`bluffs`):
  impassable, towers shoot over them, scree round them. Most carry a quarry face of stone on a flank
  (how many follows `stone-amount`). A strip of walkable bank always runs between a bluff and the
  water, no bluff stands within 22 tiles of a home, and none parts any ground.
- **Far ground: a meadow upland.** Grass is lush beside the water and green to 34 tiles out; beyond
  that about half of it, in patches, is dry. Beyond 28 steps from any water, where crops never regrow,
  heath patches (pavement: walkable and buildable, holding nothing, slowing crops near it as sand does)
  break the grass. Every home's garden stays green.
- **Desert banks.** The `banks` variant Desert is a Nile: one green ribbon along the water, dry grass
  everywhere beyond nine tiles but the homes' gardens, baked hardpan pans (slow, unbuildable, holding nothing) beyond 16 steps where no
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
| Bluffs | Planned from the height field rebuilt from the request (`heightFieldSketch`: its terrain and stone band, before the starting-crop top-up, whose wall clearing can remove stone) and the starts alone, exactly as `validateWorld` replans them: cutbank beach vertices of the river (`majorWater`) whose water share over a window a quarter of the bed's diameter (`wetShare`) is in the banks' bottom 20% and below 470 thousandths, strongest first; a crescent lens of walkable tiles 4 to 9 steps from water within about 10 tiles, never on the field's stone, kept in 2x2 blocks; centres 30 apart, 22 from every swarm, one per 80 tiles of width and height; dropped on islands under 1,500 tiles or where the walkable ring round it falls apart. A face of tiles left out of the rock on a flank (60% of bluffs at 100% stone, two flanks from 150%). |
| Mechanic layers | Deep core (`deepReachForShare`, `deepenWater`): at least 5 tiles from land in a straight line, more until it holds half the water, bodies of 16 vertices or more. Plain grass round every quarry face, rock (`rockVertices`), scree one tile out where a noise peaks, never touching a deposit. Mudflat on beach and sand within three steps of the river at point bars (the banks' top 20% of share, above 530), and on grass one row up at the sharpest (top 8%), never on a vertex touching a deposit. Loam two to seven vertices from water (`layLevee`), never in a garden or before a quarry. Pavement (`paveBeyondWater`) on old desert sand and bare grass from 28 tiles in a straight line and 22 steps (`distanceFromWet`, frayed), never touching a deposit: 40% of it in fractal patches on the temperate banks, all of it in the desert, plus hardpan pans on 55% of the desert's bare grass from 16 tiles (15 steps) and 34 tiles from homes. |
| Looks | Loam and spring meadow, pale and dune sand, heath and shale (weathered sandstone and dirt), outcrop (hoodoo) rock; `writeLayeredTerrain` writes them and clears every deposit the new ground refuses. The grass looks, lush within 5 tiles of water (3 in the desert) and dry beyond 34 (9) (`gradeGrassLooks`), never dry in a home's garden, 45% of the temperate dry ground in fractal patches (all of the desert's), go on last, after the starts, so placeStarts and the starting-crop guarantee meet plain grass everywhere. |
| Algae and quarries | The algae the deep core displaced are seated again on the nearest free shallows (straight-line distance plus up to four tiles of noise, so the beds' edges are ragged); quarry faces get stone and the ground before them is cleared. |
| Starts | `placeStarts` and `openStartsBuriedByResources` as in revision 5, with the quarry faces protected from its clearing; then the grass looks. |

`validateWorld` keeps revision 5's floor (every colony harvests wheat within 24 and wood within 32
steps, with 16 building sites within 24), replans the bluffs from the same rebuilt height field and the
colonies' start positions (the contracts check the replanned bluff and face counts equal generation's
telemetry) and requires every quarry face to hold its stone (`quarryFault`), and refuses any blocking terrain that parts the walkable ground
round it (`partingBarrier`).

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Water, sand, grass, desert weight | 0–100 (45, 3, 75, 0) | The shares of the field; water sets how much floods, so the river and the ponds trade off. |
| Smoothing | 1–8 (4) | The field's feature size. |
| River width | 20–65 (35) | The bed's diameter as a percentage of the mean side. |
| Winding river | on/off (on) | Off, the river runs straight. Straight rivers have no point bars and few bluffs (bends are read from the banks' own spread). |
| Banks | Random, Temperate, Desert | Temperate: loam and spring meadow, a green upland with patches of dry grass and heath, outcrop bluffs. Desert: a green ribbon, then dry grass, hardpan pans and weathered sandstone, hoodoo bluffs. |
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
| Revision 6, Temperate | 657 | 28.8 (0) | 88.5 (6) |
| Revision 6, Desert | 689 | 30.2 (0) | 91.9 (6) |
| Temperate, deep channels off | 657 | 28.8 (0) | 88.5 (6) |
| Temperate, bluffs off | 722 | 29.3 (0) | 94.4 (6) |
| Desert, bluffs off | 760 | 30.7 (0) | 98.0 (6) |
| Revision 5, water 30 | 97 | 3.6 (0) | 8.1 (0) |
| Revision 6, water 30 (Temperate / Desert) | 92 / 91 | 3.7 / 3.5 (0) | 8.1 / 8.0 (0) |
| Revision 5, water 55 | 985 | 46.2 (12) | 145.3 (57) |
| Revision 6, water 55 (Temperate / Desert) | 1,098 / 1,151 | 52.2 / 54.2 (15 / 16) | 168.3 / 176.6 (52 / 53) |
| Revision 5, river width 20 | 1,738 | 70.5 (35) | 223.8 (139) |
| Revision 6, river width 20 (Temperate / Desert) | 2,174 / 2,313 | 81.1 / 86.2 (46 / 49) | 281.8 / 300.3 (150 / 165) |
| Revision 5, river width 65 | 26 | 1.3 (0) | 2.8 (0) |
| Revision 6, river width 65 (Temperate / Desert) | 26 / 25 | 1.3 / 1.2 (0) | 2.8 (0) |

The levees are the gain: seven vertices deep they water the land round them, right up to the homes'
gardens, and they pay for the bank farmland the bluffs take (about 9% of the total and 6% within 48
steps; the deposits under a bluff's rock are cleared, `river.displaced.wheat` and `.wood`: 182 wheat and
50 wood on seed 3; one bluff per 56 tiles of side cost 11%, so there is one per 80). At the defaults
revision 6 gains 7–13% within 24 and 48 steps. Two or three deep (behind the sand band) the levees lost
4%. Deep water waters the land as plain water does. Point-bar mud never lies on a vertex touching a
deposit, so it takes none (before that guard it cleared 12 stone and 13 wheat on seed 3 with the bluffs
off). Pavement from 22 steps cost a low-water map 8%; from 28 it costs about 5% of the total at water
30 and nothing near the homes. Hardpan lies only where no crop grows. The one shortfall: at water 55 the
worst colony's ground within 48 steps holds 52 against 57, a bluff standing in it; the means are higher.
The grass looks change no food.

A four-AI calibration game (`map study river --seed 101`, 256x256, Nicowar, Cortex, Cabino and Maxima,
game seed 1, 20,000 ticks, every map played by the same release game binary; seed 101 draws the Desert
banks), with levees up to the homes' gardens: 169 worker births, 2,158 wheat and 379 wood harvested and
66 buildings on revision 6, against 165, 2,007, 306 and 64 on revision 5. The same seed with Temperate
banks pinned: 195 births, 2,341 wheat, 350 wood, 62 buildings. Per seat (Nicowar, Cortex, Cabino,
Maxima), births were 30, 31, 49, 59 (Desert) and 35, 33, 44, 83 (Temperate) against 36, 36, 31, 62;
Nicowar, beside the river, built 2 and 4 buildings against 8: Nicowar and Maxima treat loam like water
when placing buildings, an AI defect being fixed in the AI rather than by keeping loam off the homes.
One game varies widely: this shows no stalled opening, not a measured improvement.

## Limits

- Deep water swims at three quarters of the speed, so crossing the river by swimming is slower than in
  revision 5: a change in feel.
- Most of the map lies far from water at the defaults; on the temperate banks the far ground is a
  mosaic of meadow, dry grass and heath (the dry grass is a look only). The Desert banks change play
  beyond the green ribbon: hardpan is slow and unbuildable.
- Straight rivers (winding off, or a seed whose bed runs straight) have no point bars and only a few
  bluffs where the bank wiggles.
- Bluffs take bank farmland and can stand within 48 steps of a home (never within 22 tiles); at water 55
  one colony's 48-step food is 9% below revision 5.
- The post-pass runs over the whole map, so a repeated landscape (`repeat`) is no longer identical patch
  for patch in its bluffs and looks.
- Generating a 512x512 map took about twice as long as revision 5 in the same build (6.1 s against 3.1 s
  in an unoptimised development build; revision 5's release build takes 0.35 s), and generate now
  rebuilds the height field once more to plan the bluffs, as the validator does. On a heavily loaded
  machine (load average about 70 on 8 cores) that build took 52–57 s against 31–48 s before the change:
  too noisy for a clean figure, but no more than one extra height-field pass.
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
