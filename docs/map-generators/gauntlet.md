# The Gauntlet

The Gauntlet (`gauntlet`, numeric ID 59) is a deliberately constructed
battle arena. Every colony has two guarded entrances, each facing a different
neighbouring court. The courts form a circuit around a sealed central lake, so
armies can advance around the arena without walking through homes. Thin rock
partitions let towers support neighbouring courts while armies use offset gates.
Each home has a compact defended frontage: door angles shrink on sparse large
arenas so supplies and reinforcements do not have to travel around a wide arc.
Holding ground uses ordinary inns, towers and armies; there is no capture mechanic.

Home farms provide renewable wheat and wood. Irrigated court gardens, orchards
containing all three fruits, and clear construction aprons reward expansion.
The lake and the outside coast are sealed by cliffs: swimming does not bypass the
fronts. Paved lanes and capped beds keep crops off the fighting circuit.

## Ground

Revision 5 draws every wall, coast and road from the terrain catalogue, where earlier revisions
stood the stone resource on grass and drew roads in sand. The design uses one terrain per rule, and a
last pass paints one of four climates (temperate, arid, highland, volcanic; `gauntlet-climate`), the same
for every colony, with looks that keep those rules (`shared/Climates.h`). Walls are drawn on
vertices and lean away from the open ground beside them (`wallVertices`), so no lane, door or home
loses a tile to a wall.
There are no beaches: grass meets the water, and a coast that must be sealed is cliff. Following the
maintainer's review (2026-11-07), sand appears only where the subject is sand, and a gate that must
stay unbuildable is rough ground.

| Ground | Rules | Where | Why |
| --- | --- | --- | --- |
| Partitions | Ridges | Between neighbouring courts, broken by their offset gates | Towers support the next court over them |
| Arena ring and home borders | Obstacles | The ring between homes and courts, and the borders between homes | No tower shoots through them |
| Coasts | Obstacles | A band from the lake's and the outer sea's water to six tiles inland | No beach and no lane behind it: nothing lands, nothing swims out |
| Promenade and approaches | Paths, rough ground in the doors | The circuit's promenade and every radial approach | Quicker to march; nothing can be built in a door |
| Farms | Grass in the climate's farm look; crossings Paths, caps and plot rings Barren (`dressFarmSand`) | Every home farm and court garden | Farm ground reads by climate; no sand stands in for a road or a ring |
| Base | Barren, paved round the swarm | Every home outside its farm, but a grass verge four vertices wide at the foot of its walls | Food grows on the farms, so the base is the climate's barren ground; the verge holds the quarry faces and the towers against the walls (a tile mixing grass and barren corners holds no building) |
| Lava | Lava | The central lake, in the volcanic climate | Volcanic maps show lava at their centre |
| Deep water | Deep water | Water five vertices or more from land | Slower swimming, no algae |


## Controls and supported requests

| Control | Values | Default | Effect |
| --- | --- | --- | --- |
| Court size | 80–120%, steps of 10 | 100 | Expands the court ring radially; trades home farming area for fighting ground. |
| Gate width | 5, 7, 9 tiles | 7 | Widens home approaches and inter-court gates. Wider gates allow more manoeuvring under tower fire. |
| Partition thickness | 1, 2, 3 tiles | 2 | Thickens the rock between courts, increasing separation for fire support. |
| Starting tower level | 0–3 | 1 | None, or one supplied tower at each home entrance at the selected game level. |
| Wheat amount | 0–300% | 100 | Scales extra home wheat and the contested gardens; opening grain remains. |
| Wood amount | 0–300% | 100 | Scales the nearby home woodlot; a minimum opening supply remains. |
| Algae amount | 0–300% | 100 | Scales shallow-water algae, including the home irrigation channels. |
| Fruit amount | 0–300% | 100 | Scales all three orchard fruits in every court; zero removes them. |

Resource amounts are planting requests: wheat can saturate its finite beds at the
highest settings. Even 0% retains opening grain and 30 wood tiles per home; fruit
and algae have no minimum guarantee.

Stone has no abundance slider: every home has one quarry face of 16 stone at the inside foot of its outer wall, between its doors, the towers' only supply beyond their reserve. Farm crossings remain
open by construction; allowing them to disappear would undermine this arena's
circulation contract. Towers begin with ammunition and reserve stone. Players
still need the ordinary economy and training to maintain and upgrade them. A
starting tower covers its own entrance's home-side mouth; a wide gate is not a
promise that every tile is under fire.

Maps need a shorter side of at least 256 tiles, 2–16 colonies, at least 48 tiles
of outer-court arc and 27 tiles of inner-court arc per colony, and 22 tiles of
depth behind the home anchor. This permits up to eight colonies at 256×256 and
sixteen at 512×512. Counts above twelve require 512×512; the inner ring
expands to preserve court widths instead of squeezing the defended fronts.
Requests that cannot meet these budgets are refused before generation. Rectangles
use a circular arena sized to the shorter side; extra sea does not advantage a
colony. A two-colony game has two different courts between the same opponents.

A named seed stream selects a jousting, bent-wall, or paired-garden court family;
other streams vary global orientation, gate offset, and the deal of home slots to
team indices. Paired gardens fall back to the jousting family when the inner
arc is too narrow; telemetry records the requested and effective family.
This is repeated-wedge fairness, not a claim of exact raster
symmetry. The finished map is checked for resources, building room, both home
exits, every court connection, and permanent circulation after crop spread.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
platform golden rows, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[GauntletGenerator.cpp](../../src/map/generator/generators/GauntletGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
