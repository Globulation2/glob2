# Portage Lakes

**Portage Lakes** (`portage-lakes`, numeric ID 65) is Boundary-Waters lake country: long, crooked
glacial lakes in a forest of dry wooded ridges and bare granite knots, trails and portages between
the lakes, and farms in the sheltered bays. Its two investments change different connections.
Cutting opens a public land shortcut through a wooded ridge; swimming opens a crossing between
opposite shores. Initial land routes connect every colony without either investment.

Revision 8 (Portage Lakes 2.0) rebuilt the map for the engine's full terrain palette; revision 7
drew the rock knots as stone deposits on grass and the trails as sand, and everything else from
grass, sand and water.

## How it plays

- **Rock knots are rock; stone is a place.** The knots in the deepest forest are Ridges-group rock:
  impassable and permanent, and towers shoot over them. Every home has a granite knob of its own (its
  quarry face 9 to 22 tiles from the town, nearest first, sometimes in the edge of the woods but
  never with trees before its face), with the same quarry stone for every colony (7 tiles, or 5 on
  the small stencil every colony takes when one home cannot fit the large one) and a skirt of scree
  behind it. When even the small stencil cannot fit beside every home, every home keeps revision
  7's stone patch instead (telemetry `portage-lakes.knob.stencil`), so the knobs never refuse a map
  revision 7 made. The walk to the knob is not equal between colonies (see Limits). The forest knots carry ambient quarry faces by `stone-amount`, and a frayed scree
  apron (slow, barren, but no growth inhibition) where open ground meets them.
- **Trails pay.** Every opening trail and every portage approach is trail (the Paths group): twice
  the walking speed, buildable, nothing grows or stands on it, and unlike the old sand it inhibits
  no crops beside it. Where a trail meets wet ground it is drawn as boardwalk. Cutting a portage
  plug now joins two fast trail approaches, so the cut's payoff is measured in walking time as
  well as steps (telemetry `portage-lakes.portage.*`).
- **Bay farms stay grass.** Loam bay farms (Fertile: crop land that waters its surroundings at
  48/256 of water) were built and measured, and dropped: growth potential rose, but in
  seat-rotated four-AI games the same maps without the loam bred a quarter more workers, and the
  loam's watering shrank the dry forest until a compact map lost its portage (see Food).
- **Wet shores.** Stretches of lake beach far from every home are mud (Rough: slow, holds nothing,
  like the beach it replaces, but inhibits no crops), so the shore meadows there grow better. Mud
  never lies in a home's buffer or beside the algae pools. Mud thins the forest by the lakes; when
  that costs the swim its shortcut, the layout is furnished again with revision 7's plain beaches
  (telemetry `portage-lakes.shore.margins`).
- **Deep lakes.** Lake centres three steps or more from land are deep water (`deep-lakes`):
  swimmers cross it at 192/256 speed, algae does not grow there, and it waters the land like any
  water. A shallow margin at least two vertices wide keeps the beaches, the algae and the landings
  working. **Feel change:** a swim across a lake takes longer than on revision 7, so swimming is
  a weaker shortcut beside the portages; the validator still demands that every swim and every
  portage save a measured walk.
- **Forest floor.** The forest stays Grass (Barren holds no wood); it is drawn as dry grass under
  the trees, lush grass by the water and in the best meadows, plain grass in the home gardens, the
  kit plots and every quarry's approach.

## How it is built

`src/map/generator/generators/PortageLakesGenerator.cpp`; the header comment records the design.
The layout keeps a mechanic sketch (WATER, SAND, GRASS, TRAIL, MUD, SCREE, RIDGE_ROCK,
DEEP_WATER: the canonical type of each rule group, which every shared helper and check reads) and
looks painted over it with `composeLooks` (`shared/Sketch.h`), so looks never change a rule.
`validateWorld` rebuilds the design from the request and the ordered starts and compares every
vertex, looks included.

| Stage | What happens |
| --- | --- |
| Lakes | As revision 7: lake sites spread and relaxed, each a spline stroke bent into a curve, an S or a fork, elongated by `lake-elongation`; beaches; separate swimming lakes required. The shield is drawn from its own stream. |
| Settlements | Four complete settlement proposals scored by the shared start scorer, up to 24 landscapes, as revision 7. |
| Bay farms | Home and neutral bay wheat and timber plots, as revision 7. |
| Shore | Beaches laid, then lake beach outside every home buffer and away from the algae pools turns to mud where a 14-tile noise passes its cut. |
| Forest and knots | Dry ground (no growth chance) outside the home buffers is 75% forest; rock knots in the deepest forest (as revision 7). |
| Trails | The settlement network and the optional extra trails are routed and reserved as sand, then every corner they painted becomes trail. Portage approaches are drawn as trail directly, never touching the plug. |
| Portages, swim, shore fields | As revision 7. |
| Home knobs | One stencil (7×5 tiles, face of 7 on the town side; small 5×4 with a face of 5) turned by quarter turns to face each home, its face 9 to 22 tiles away on open ground at least 3 from water, clear of the home clearing, a tile round every plot, trails, plugs, landings, clearings and their paths and the algae pools (woods may stand under and round the knob, not before its face), nearest and squarest first; every home takes the small stencil when any cannot fit the large one, and every home keeps revision 7's stone patch when even that cannot. A one-corner scree skirt round the back and sides. |
| Rock | Ambient quarry faces (up to 3 tiles, `stone-amount` × a quarter of the knots) on the open side of forest knots, then in the woods; rock vertices only where all four tiles are rock (`rockVertices`); a knot tile the rock cannot block keeps its wood. |
| Scree, deep water, looks | `talusApron` round the forest knots on open ground only; `deepCore` three steps into every lake (pools left shallow); then the looks. |

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Lake elongation | 125–300% (200) | Longer, narrower lakes increase the difference between a shore walk and a crossing. |
| Portage depth | 2–8 (4) | Tree rows in each portage plug. |
| Extra trails | 0–100% (25) | Additional open trails beyond the settlement network; on one/two-colony maps, trails to neutral bays. |
| Deep lakes | on | Deep water in the lake centres (slower swimming, no algae). Off gives revision 7's shallow lakes. |
| Bedrock (`shield`) | Random, Granite, Greenstone | Looks only: rock (ridge rock or outcrop), deep water (deep or dark water) and shore mud (mud or mudflat). Plays identically. |
| Wheat, wood, algae, fruit amount | 0–300% (100) | As revision 7; starter supplies are guaranteed. |
| Stone amount | 0–300% (100) | Ambient quarry faces on the forest knots and the country stone clumps; every home's knob quarry is guaranteed. |

Sides are powers of two from 64 through 512, including rectangles. A 64-tile side selects compact
construction. The colony cap is `min(12, max(2, width * height / 4096))` on smaller shapes, and
sixteen on 512×512; one colony is a practice map.

## Food

Growth potential (`.agents/skills/glob2-map-design/scripts/growth_potential.c`: the engine's crop
growth chance summed over farmland, by rule group), 256×256, 4 colonies, seeds 1–3, revision 8
against revision 7.

| Revision | Total | Within 24 of swarm (worst) | Within 48 (worst) |
| --- | --- | --- | --- |
| 7 | 1821 | 69.1 (46) | 197.2 (129) |
| 8, defaults | 1834 | 69.5 (45) | 198.8 (128) |
| 8, shallow lakes (`deep-lakes` off) | 1834 | 69.6 (45) | 198.8 (128) |
| 8, greenstone | 1834 | 69.5 (45) | 198.8 (128) |
| 8 with loam bay farms (built, dropped) | 1861 | 86.2 (63) | 229.9 (135) |

Trails and mud inhibit nothing where the old sand did, which adds a little; the deep cores water
the land like any water. The shield is a look and measures the same as the defaults.

**Growth potential is not the economy.** Loam bay farms raised the 24-step yield by a quarter, yet
in seat-rotated four-AI games (Nicowar, Cortex, Cabino, Maxima; 20,000 ticks; seeds 101 and 102,
four rotations each, both maps played by the same release binary) the maps with loam bred 152
workers a game against 190 for the same design without it and 194 on revision 7, and one Cortex
seat stalled at 7 units. The cause was not pinned down: the field's edge tiles are transitions that
cannot be farm-painted, and Maxima's farm planner treats fertility sources like water. So the farms
stay grass.

Calibration, the final design: 12 games (seeds 101–103, four seat rotations each) per revision.

| Revision | Births a game | Wheat | Wood | Buildings | Units at 20,000 | Colony under 12 units |
| --- | --- | --- | --- | --- | --- | --- |
| 7 | 205 | 2,740 | 423 | 73 | 267 | 0 of 48 |
| 8 | 232 | 3,043 | 455 | 80 | 297 | 0 of 48 |

The single game the brief asks for (seed 101, Nicowar, Cortex, Cabino, Maxima in that order) gave
219 births, 3,150 wheat, 449 wood and 84 buildings against revision 7's 178, 2,446, 396 and 76.
Seeds 101 and 103 fell back to stone patches, so only seed 102's games include home knobs.

## Limits

- Stone is a place now: about a seventh of revision 7's stone tiles at the defaults (97–178
  against 832–907 on seeds 101–103), because the forest knots are rock with a face here and there. Each home's
  knob face holds 7 stone; ambient faces and the country clumps scale with `stone-amount`.
- The home knobs are equal in stone, not in distance: they are placed nearest first, and where
  the meadow round a town is full of fields and lake the knob lands as far as 22 tiles away (walks
  of 5 to 21 steps to the face). On 6 of 16 sampled maps (128 to 512, 2 to 8 colonies) no
  stencil fits beside every home and every home keeps revision 7's stone patch instead; placing
  the knobs before the fields, rather than after, is the obvious next step.
- Swimming is slower across the deep cores, so the swim shortcut saves less time than steps (one
  seed: 61 steps saved, 62 plain-tile time units; another 54 steps, 21 time units). The validator
  checks steps, as revision 7 did, not time.
- Mud is dropped for the whole map when it would cost the swim shortcut; on such maps every lake
  beach is sand, as on revision 7.
- Scree aprons round the forest knots are rare: the knots sit inside the forest, and scree is laid
  only on open ground so no tree loses its ground. The home knobs' skirts carry most of the scree.
- One calibration set of 12 games on three seeds is a calibration, not a tournament; the AIs' use
  of the trails and portages was not measured directly.

## Implementation source

[PortageLakesGenerator.cpp](../../src/map/generator/generators/PortageLakesGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
