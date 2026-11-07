# Crater lakes

**Crater lakes** (`crater-lakes`, numeric ID 4) is Leo Wandersleb's crater map from 2006: round
bowls pressed at random into high ground and filled to the requested water share, on the shared
height-field pipeline. The lakes are local obstacles, not barriers between colonies, so it plays as
an open land map where water shapes the paths and farmland rings every lake.

Revision 6 (Crater Lakes 2.0) draws the same bowls with the engine's full terrain palette: dark
middles in the bigger lakes, broken rims of rock round the bowls with quarry faces, a talus at the
rock's foot, patches of dark soil on the outer flanks, heath or tundra on the dry uplands between
the craters, and a choice of volcanic maars or glacial kettles. Revision 5 drew everything from
grass, sand and water.

## How it plays

- **The same map underneath.** For a given seed, revision 6 builds the height field, fills the
  lakes, lays the beaches and resource bands, chooses the starts and repairs openings exactly as
  revision 5 did. The 2.0 pass then repaints ground nobody stands on; the only deposits it touches
  are under the rim rock and the algae of the deep cores.
- **Lakes have deep middles.** Water at least four and a half tiles (frayed up to six) from any
  shore is deep: swimmable but slower, holds no algae, and waters the land like water. Every shore
  keeps two tiles of plain shallows. Revision 5 put the algae in the lake middles; they now move to
  the nearest free shallows of the same lake, as many as were taken, so the algae sit within reach
  of a shore. Crossing a big lake takes longer than in revision 5: a change of feel.
- **Broken rims.** Round every lake of 80 water vertices or more, arcs of rock stand six to ten
  tiles from the water in a straight line (six to nine for kettles), just outside the farmland at
  the shore, following the lake round: runs of rock parted by open
  gaps, so a rim is never a closed ring. Rock is impassable and permanent; towers shoot over it.
  It never stands on the neck between two lakes, never within 24 tiles of a home (closer in it took
  farmland from a colony's first 24 steps), and never cuts walkable ground off: an arc that would is
  not drawn. Wheat, wood and stone under the rock are cleared, never within 34 tiles of a home.
- **Quarries.** Some arcs carry a **quarry face** on their outer flank: permanent stone on grass,
  at least 24 tiles from every home. The face nearest each colony is stocked first, then others at
  random; how many follows `stone-amount` (one per colony plus one per 32,768 tiles at 100%, none at
  0%). The revision-5 stone band by the beaches stays where it was.
- **Talus.** Rough pumice or scree lies at the rock's inner foot: slow, unbuildable, and unlike
  sand it does not hold crops back.
- **Dark soil.** Grass on the outer flank, from the rim band's outer row to three tiles
  (straight-line, frayed) beyond it, is loam in patches: buildable crop land that waters the land round it at 48/256 of water. The home
  garden, 12 tiles round a swarm (frayed outward), keeps plain grass, as do quarry faces and the
  ring before them.
- **Uplands.** Grass 22 steps or more and about 24 tiles (frayed) from any water is heath, tundra or ash where a noise
  field passes: buildable, but it inhibits crop growth like sand, so it lies only beyond the water's
  reach.
- **Looks only.** Lush grass on the inner slopes, dry grass on the high ground, beach and dune
  artwork change nothing in play.

## Crater types

| Type | Rims | Looks |
| --- | --- | --- |
| Volcanic maars | Dense and thick: rock runs of one to three sectors, gaps of one or two, a band six to ten tiles from the water | Ridge rock, pumice talus, loam, heath and ash uplands, plain beaches and dunes, deep blue water |
| Glacial kettles | Sparse and thin: runs of one or two sectors, gaps of two to four, a band six to nine tiles from the water | Outcrops, scree talus, moss, tundra and heath uplands, pale beaches, dark water, more lush grass |

Random (the default) picks one per map. The two play alike apart from how much rock the rims hold.

## How it is built

`src/map/generator/generators/CraterLakesGenerator.cpp`; the header comment and the constants'
comments record why. Revision 5's stages run unchanged, then the crater pass:

| Stage | What happens |
| --- | --- |
| Height field | `makeCraters`: `width * height * lake-density / 30000` bowls of radius `lake-size`, roughened by noise; `classifyHeightField` turns the weights into thresholds. |
| Terrain, resources, starts | `paintHeightFieldTerrain` and beaches, resource bands, balanced starts, the starting-crop guarantee, fruit groves; `placeStarts` and `openStartsBuriedByResources`. |
| Rims | Rim band tiles (every corner six to ten tiles from water in a straight line, six to nine for kettles; nearest one lake, not on a neck, 24 tiles from homes) cut into sectors of about seven tiles by angle round the lake's middle; runs of rock sectors and open ones per crater type; `blocksOfTwo` keeps the rock two tiles thick; quarry faces on the outer flank (`quarryNear`'s selection, worked out from one flood of the open ground); a parting check drops any arc that cuts ground off; `rockVertices`. |
| Deep cores | Straight-line distance from land at least 4.5 tiles plus a frayed 1.5, specks under 12 vertices dropped; displaced algae to the nearest free pure shallows of the same lake. |
| Talus, loam, uplands | Rough at the rock's inner foot (two steps to six tiles from water), loam patches on the outer flank (straight-line), barren uplands beyond the water's reach (22 steps and 24 tiles, frayed), none in the home garden. |
| Looks | A mechanic sketch (canonical types) with looks painted by `composeLooks`, by crater type; quarry stone stocked, and generation refuses the map if any face tile is left without its stone. |

Every layer and choice is in telemetry (`crater-lakes.*`): the type, deep vertices, algae moved and
lost, rim lakes, arcs and dropped arcs, rock vertices, cleared stone and crops, quarry faces wanted,
placed and stocked, each colony's walk to its nearest face, talus, loam and upland vertices, and the
look counts, with the swarm corners.

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Water, sand, grass, desert weight | 0–100 (25, 3, 75, 0) | The height field's shares. |
| Smoothing | 1–8 (6) | The noise's feature size. |
| Lake density | 10–50 (25) | Bowls per area: few big lakes to many ponds. |
| Lake size | 10–40 (25) | Bowl radius: round clean lakes at 40, ragged ones at 10. |
| Crater type | Random, Volcanic maars, Glacial kettles | Rim density and looks (see above). |
| Deep water | on/off (on) | Deep middles in the bigger lakes. Routes on foot are the same either way. |
| Crater rims | on/off (on) | Broken rock rims with quarry faces and talus. |
| Fruit | 0–64 (4) | Fruit groves. |
| Wheat, wood, algae amount | 0–300% (100) | Each resource band's size. |
| Stone amount | 0–300% (100) | The stone band and how many rim arcs carry a quarry face. |
| Stone on hilltops | on/off (off) | Stone on the highest grass instead of by the shore. |
| Repeat landscape | 0–5 (0) | Tiles the height field; the 2.0 pass runs once over the whole map. |

## Food

Growth potential (the engine's crop growth chance summed over farmland, by rule group: sand and
barren ground inhibit, rough ground does not, deep water waters like water, loam is farmland that
waters at 48/256), 256×256, 4 colonies, seeds 1–6, revision 6 against revision 5 on the same seeds
(the starts are identical; the swarm corners come from revision 6's telemetry):

| Variant | Total | Within 24 of swarm (worst) | Within 48 (worst) |
| --- | --- | --- | --- |
| Revision 5 | 2195 | 87.0 (41) | 308.1 (150) |
| Revision 6, defaults | 2395 | 95.9 (50) | 331.2 (178) |
| Volcanic maars | 2329 | 96.8 (51) | 326.4 (181) |
| Glacial kettles | 2430 | 95.6 (50) | 338.2 (178) |
| No rims | 2714 | 97.4 (50) | 367.3 (193) |
| No deep water | 2395 | 95.9 (50) | 331.2 (178) |

The loam carries the gain: total food +9%, +10% within 24 steps and +8% within 48. The rims cost
food (no rims: +24% total), maars more than kettles, since rock replaces grass in the crop probe's
reach; with the rock 24 tiles off the homes, near-home food is barely affected by it. Deep water
changes no food at all. Measuring the rim band and the loam ring in straight lines (round, where step
contours drew squares round round lakes) needed the band a tile deeper to keep the maars' rock (1,688
rock vertices against 1,812 on seed 2); the kettles now carry more rock than before (753 and 843
against 473 and 479 on seeds 1 and 3), and the total is 6% below the step-contour revision 6 (2,538).

## Limits

- The pass doubles generation time in the same build (512×512, 4 colonies: 17.9 s against 8.9 s
  with rims and deep water off, unoptimised development build); revision 5 takes 0.5 s in a release
  build.
- Every map shape and colony count revision 5 accepted (7 sizes and rectangles × 1–12 colonies × 2
  seeds) still generates at the defaults, with each crater type and with each layer off.
- Calibration (seed 101, Nicowar, Cortex, Cabino and Maxima, game seeds 1–3, 20,000 ticks, kettle
  type drawn): total worker births 622 against revision 5's 644 (−3%), wheat harvested −8%, wood
  +4%, buildings 216 against 210. Per seat over the three games: Nicowar 80 against 93, Cortex 121
  against 132 (one game 11 against 49), Cabino 110 against 91, Maxima 311 against 328. Single games
  vary by up to 40%. Nicowar and Maxima treat Fertile ground as water when placing buildings, a
  known AI bug being fixed separately; part of any drop near loam is that. No rotation tournament or
  human playtest has been recorded for revision 6.
- Rims need grass six to ten tiles from the water outside the home ring: maps crowded with
  colonies, or small lakes, carry fewer arcs, and arcs blocked by fruit groves, buildings or the
  starting crops leave gaps. Quarry faces are a best effort: when fewer arcs have a grass flank 24
  tiles from every home than wanted, telemetry reports `crater-lakes.quarry.shortfall`, and the walk
  to the nearest face varies by tens of tiles between colonies. Generation refuses a map with a face
  tile left without its stone; `validateWorld` does not recheck the faces, since the pass works from
  revision 5's finished map and its plan cannot be rebuilt from the request alone.
- When a lake's shallows are already full, a few displaced algae find no free tile and are lost
  (`crater-lakes.algae.lost`, about 4% of the moved algae on one of six seeds).
- With repeat landscape the 2.0 pass is drawn once over the whole map, so the repeated patches no
  longer match exactly in rim gaps, faces and looks.

## Verification

Run the focused contracts (`GLOB2_CONTRACTS=crater-lakes`, `MapGeneratorContracts.h`) and the
generator's default case, sweep every shape and colony count against revision 5, and compare growth
potential. See [generator verification](verification.md) for commands, revisions, save/replay
checks and evidence requirements.

## Implementation source

[CraterLakesGenerator.cpp](../../src/map/generator/generators/CraterLakesGenerator.cpp) owns this landscape's construction, controls and validation; the pipeline it shares with Swamp, River and Islands is [Terrain.cpp](../../src/map/generator/shared/Terrain.cpp).
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
