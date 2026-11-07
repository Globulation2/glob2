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
  quarry face 9 to 22 tiles from the town, nearest first), placed right after the home's own bay
  farms and before every other field, pool, trail and portage takes the meadow, with the same quarry
  stone for every colony (7 tiles, or 5 on the small stencil every colony takes when one home cannot
  fit the large one) and a skirt of scree behind it. When even the small stencil cannot fit beside
  every home, every home keeps revision 7's stone patch instead (telemetry fallback
  `portage-lakes.knob.stencil`), so the knobs never refuse a map revision 7 made. On 16 sampled maps
  every home took the large stencil, its face 9 to 13 tiles out (see Limits). The forest knots carry ambient quarry faces by `stone-amount`, and a frayed scree
  apron (slow, barren, but no growth inhibition) where open ground meets them.
- **Trails pay.** Every opening trail and every portage approach is trail (the Paths group): twice
  the walking speed, buildable, nothing grows or stands on it, and unlike the old sand it inhibits
  no crops beside it. Where a trail meets wet ground it is drawn as boardwalk. Cutting a portage
  plug now joins two fast trail approaches, so the cut's payoff is measured in walking time as
  well as steps (telemetry `portage-lakes.portage.*`).
- **Bay farms stay grass.** Loam bay farms (Fertile: crop land that waters its surroundings at
  48/256 of water) were built and dropped: the loam watered the dry forest round the bays, where the
  structural wood must stand on ground nothing waters, and the forest shrank until a compact map lost
  its portage, the shortcut the map is about.
- **Wet shores.** Stretches of lake beach far from every home are mud (Rough: slow, holds nothing,
  like the beach it replaces, but inhibits no crops), so the shore meadows there grow better. Mud
  never lies in a home's buffer or beside the algae pools. Mud thins the forest by the lakes; when
  that costs the swim its shortcut, the layout is furnished again with revision 7's plain beaches
  (telemetry `portage-lakes.shore.margins`).
- **Deep lakes.** Lake centres three steps or more from land are deep water (`deep-lakes`):
  swimmers cross it at 192/256 speed, algae does not grow there, and it waters the land like any
  water. A shallow margin at least two vertices wide keeps the beaches, the algae and the landings
  working. **Feel change:** a swim across a lake takes longer than on revision 7, so swimming is
  a weaker shortcut beside the portages; the validator demands that every portage save a measured
  walk and every swim save a measured walking time (deep water and fast trails counted), by the same
  rule as steps: at least 8 plain-tile units and a fifth of the walk round (4 and a tenth on compact
  maps).
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
| Bay farms | Home bay wheat and timber plots, as revision 7. |
| Home knobs | Right after the home bays, before the algae pools, the lakes' extra shore fields, the neutral bays, the trails and the portages: one stencil (7×5 tiles, face of 7 on the town side; small 5×4 with a face of 5) turned by quarter turns to face each home, its face 9 to 22 tiles away on open ground at least 3 from water, clear of the home clearing and a tile round every home plot, nearest and squarest first; every home takes the small stencil when any cannot fit the large one, and every home keeps revision 7's stone patch when even that cannot. Every later stage keeps off the knob and the tile round it (the reservation; trails are routed round it, mud stays 3 tiles off), and the knob blocks the walks the portages and the swim are measured on. When the layout with the knobs first fails, the whole furnishing is done again with the knobs placed last, after the shore fields; and when no settlement proposal of a landscape works with the knobs first (a finished map can still fail its walk checks), every proposal is tried again with them last (telemetry fallback `portage-lakes.knob.order`), so the early order refuses no landscape the late one accepted (128×128 with 4 colonies, seed 1, needed it). A one-corner scree skirt round the back and sides. |
| Neutral bays, pools, extra fields | As revision 7. |
| Shore | Beaches laid, then lake beach outside every home buffer and away from the algae pools turns to mud where a 14-tile noise passes its cut. |
| Forest and knots | Dry ground (no growth chance) outside the home buffers is 75% forest; rock knots in the deepest forest (as revision 7). |
| Trails | The settlement network and the optional extra trails are routed and reserved as sand, then every corner they painted becomes trail. Portage approaches are drawn as trail directly, never touching the plug. |
| Portages, swim, shore fields | As revision 7. The swim is chosen by steps saved, as revision 7 chose it; the finished map must also save walking time. |
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
| Stone amount | 0–300% (100) | Ambient quarry faces on the forest knots and the country stone clumps; every home's knob quarry (or, without knobs, its stone patch) is guaranteed. |

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
| 8, defaults | 1754 | 67.4 (46) | 198.7 (142) |
| 8, shallow lakes (`deep-lakes` off) | 1754 | 67.4 (46) | 198.7 (142) |
| 8, greenstone | 1754 | 67.4 (46) | 198.7 (142) |
| 8 with loam bay farms (built, dropped) | 1861 | 86.2 (63) | 229.9 (135) |

Trails and mud inhibit nothing where the old sand did, which adds a little; the deep cores water
the land like any water. The home knobs, placed before the lakes' extra shore fields and the neutral
bays, take some of the meadow those fields would have used: the map total is 4% under revision 7,
the 48-step yield round the homes is level and the worst colony's rises (142 against 129). The shield is a look and measures the same as the defaults.

Loam bay farms raised the 24-step yield by a quarter, but their watering reached into the dry
forest round the bays and cost a compact map its portage, so the farms stay grass. In seat-rotated four-AI games (20,000 ticks, seeds 101 and 102, four
rotations each) the loam maps bred 152 workers a game against 190 without it; that matches the
AIs' reading of Fertile ground as water, an AI defect to fix in the AI, not a reason for this choice.

Calibration, the final design: 12 games (seeds 101–103, four seat rotations each) per revision.

| Revision | Births a game | Wheat | Wood | Buildings | Units at 20,000 | Colony under 12 units |
| --- | --- | --- | --- | --- | --- | --- |
| 7 | 205 | 2,740 | 423 | 73 | 267 | 0 of 48 |
| 8 | 232 | 3,043 | 455 | 80 | 297 | 0 of 48 |

The single game the brief asks for (seed 101, Nicowar, Cortex, Cabino, Maxima in that order) gave
219 births, 3,150 wheat, 449 wood and 84 buildings against revision 7's 178, 2,446, 396 and 76.
Seeds 101 and 103 fell back to stone patches, so only seed 102's games include home knobs. These
games predate placing the knobs before the fields, which changes the maps (seeds 101 and 103 now
take knobs); they were not replayed.

## Limits

- Stone is a place now: about a seventh of revision 7's stone tiles at the defaults (97–178
  against 832–907 on seeds 101–103), because the forest knots are rock with a face here and there. Each home's
  knob face holds 7 stone; ambient faces and the country clumps scale with `stone-amount`.
- The home knobs are equal in stone, not exactly in distance: they are placed nearest first. On 16
  sampled maps (128×128 with 2 colonies, 256×256 with 4 and 8, 512×512 with 8; seeds 1–4) every home
  took the large stencil, its face 9 to 13 tiles out, walks of 5 to 11 steps and at most 4 steps
  between a map's nearest and farthest knob. Placed after every field, trail and portage, as first
  built, the knobs fell back to stone patches on 6 of those maps and to the small stencil on one,
  with faces up to 22 tiles out and walks of 5 to 21 steps (up to 15 apart on one map).
- Swimming is slower across the deep cores, so a swim saves less time than steps (on the 16 maps:
  28 to 67 steps saved, 27 to 77 plain-tile time units). The swim is still chosen by steps; a finished
  map whose swim saves too little time is refused and the next proposal or landscape is tried.
- Mud is dropped for the whole map when it would cost the swim shortcut; on such maps every lake
  beach is sand, as on revision 7.
- Scree aprons round the forest knots are rare: the knots sit inside the forest, and scree is laid
  only on open ground so no tree loses its ground. The home knobs' skirts carry most of the scree.
- Generation is slower: a 512×512, 8-colony map took 161 s and 386 s on a -O0 development build
  against 24 s for an unchanged generator there (13× its release time), roughly 12–29 s release
  equivalent beside revision 7's 11–22 s release on the same seeds; a map that falls back to plain
  beaches furnishes its layout twice, and one whose knobs placed first leave no room furnishes it
  again with the knobs last. The walking-time measures of the portages are taken only when telemetry
  is recorded; the swim's walking-time check runs always, its walk round followed only until it is
  long enough to pass. On 512×512 with 8 colonies (seeds 3 and 4, two runs each, -O0, a heavily shared
  machine) this pass measured 246 s of CPU against 268 s before it, within the noise. Not measured on
  a release build.
- One calibration set of 12 games on three seeds is a calibration, not a tournament; the AIs' use
  of the trails and portages was not measured directly.

## Implementation source

[PortageLakesGenerator.cpp](../../src/map/generator/generators/PortageLakesGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
