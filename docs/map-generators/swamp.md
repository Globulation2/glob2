# Swamp

**Swamp** (`swamp`, numeric ID 1) is a pond-riddled lowland: Leo Wandersleb's 2006 height-field
map, where one almost pure noise field leaves ponds and sloughs scattered everywhere rather than in
one shape. Water is close to every piece of land, so wheat and wood regrow almost anywhere and the
economy is rich; the cost is that movement and building room are broken up by ponds a colony must
walk round until it can swim.

Revision 6 (Swamp 2.0) draws the same lowland with the engine's full terrain palette: ponds with
dark middles, mud on the far shores, thicket hammocks on the highest ground between the ponds, the
odd boardwalk across a neck of water near a home, and (optionally) moss in the hollows. Revision 5
drew everything from grass, sand and water.

## How it plays

- **The same map underneath.** For a given seed, revision 6 builds the height field, ponds,
  beaches, resource bands, starts, starting crops, fruit groves and opening repairs exactly as
  revision 5 did. The 2.0 pass then repaints only ground nobody stands on, so every deposit and
  start is where it was and stays legal.
- **Ponds have deep middles.** Water at least four tiles in a straight line from any shore is deep
  (so a core's outline is round, not a rectangle): swimmable but
  slower, holds no algae, and waters the land like water. Every shore keeps two tiles of plain
  shallows, and deep water keeps 2.5 tiles off every algae bed, so the algae in a pond's middle
  reads as a shoal. Crossing a big pond takes longer than in revision 5: a change of feel.
- **Far shores are muddy.** A share of the beach sand (40% by default) is mud: slow, unbuildable,
  holds nothing, and unlike sand it does not hold crops back. Mud lies only 12 tiles or more from
  every swarm, so a home's own shores, the footpaths and building ring its first workers use on
  every trip, stay firm sand, and 6 tiles or more from any algae, whose regrowth wants sand on its
  shore.
- **Hammocks on the hummocks.** Rough knots of thicket (impassable, blocks arrows, holds nothing)
  stand on the highest ground between the ponds, three per 128×128 tiles by default, at least 20
  tiles from every swarm and three steps from water. A hammock that would part the walkable ground
  round it is not drawn, so none ever cuts anything off.
- **Boardwalks where the walk round is long.** For each colony, the straight crossing of at most
  six water tiles from ground within 40 steps of its swarm that saves the longest walk round is
  laid as boardwalk (fast, buildable, holds nothing) if it saves at least 30 steps. Both ends must
  already be walkable from the swarm, so a causeway shortens a detour and never joins ground that
  was apart. Most 256×256 maps with four colonies get one or two; some get none.
- **Moss (off by default).** With `moss` on, grass in the lowest third of the land, in patches and
  beyond each home's 12-tile garden, is moss: crop land that waters its neighbours a little. It is
  off by default because the growth tool loses food to it (see Food).
- **Looks only.** The climate picks one look for each kind of ground over the whole map, homes
  included: one grass, one sand, one deep water, one mud, one moss and one thicket. They change
  nothing in play. Revision 7 dropped the second beach sand, the lush and dry grass bands and the
  plain-grass discs round the swarms, which made the map hard to read.

## How it is built

`src/map/generator/generators/SwampGenerator.cpp`; the header comment and the constants' comments
record why. Revision 5's stages run unchanged (the field is copied out of the builder, not drawn
again), then the bog pass over a mechanic sketch of canonical types:

| Stage | What happens |
| --- | --- |
| Height field | `makeSwamp` noise; water is `water / (1 + water + grass)` of the map, the rest grass; `layBeaches` rings every pond. |
| Resources and starts | Algae, stone, wheat and wood as bands of the field; balanced starts, the starting-crop guarantee, fruit groves, `placeStarts`, `openStartsBuriedByResources`. |
| Hammocks | Seeds at the field's local maxima above the 78th percentile of the land, 16 tiles apart; a lobed disc of 3 to 5.5 tiles on vacant pure grass, trimmed to blocks two thick and drawn with `rockVertices`; any hammock whose blocked tiles part the walkable ground is dropped (`partingGroup`). |
| Deep cores | Water at least four tiles from land in a straight line (`distanceSquaredTo`), never at a corner of a tile holding a deposit or within 2.5 tiles of algae, opened, specks under 12 vertices dropped. |
| Mud | Beach sand within two steps of water, outside 12 tiles of every swarm and 6 of algae, ranked by smooth noise; the `mudflats` share of it. |
| Moss | Grass below the land's 36th percentile, where a patch field passes, outside 12 tiles of every swarm, in patches of 24 vertices or more (only with `moss` on). |
| Causeways | Per colony, the best straight crossing found by a walking flood from its swarm; two rows of boardwalk vertices, so the crossing tiles are pure boardwalk; causeways 16 tiles apart. |
| Looks | `composeLooks` by climate: one look per rule group, map-wide (the pass runs after every stage that needs exact grass). |

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Water, grass weight | 0–100 (35, 60) | Water's share of the map, `water / (1 + water + grass)`. |
| Smoothing | 1–8 (6) | Pond scale, from speckle to broad sloughs. |
| Fruit | 0–64 (4) | Fruit groves. |
| Deep water | on/off (on) | Deep cores in the bigger ponds. Routes on foot are the same either way. |
| Mudflats | 0–100% (40) | The share of the far beaches that is mud. |
| Moss | on/off (off) | Moss in the hollows, beyond each home's 12-tile garden. |
| Hammocks | 0–8 (3) | Thicket hammocks per 128×128 tiles. |
| Causeways | on/off (on) | Boardwalks across necks of water near homes where the walk round is long. |
| Climate | Random, Temperate, Tropical | Looks only: Temperate is a bog (plain grass, pale sand, black water, mud, moss, brown thicket); Tropical a mangrove swamp (lush grass, yellow sand, blue deep water, mudflats, loam, dark hedge canopy). Plays identically. |
| Wheat, wood, stone, algae amount | 0–300% (100) | The resource bands of the field. |
| Stone on hilltops | on/off (off) | Stone on the highest grass rather than above the beaches. |
| Repeat landscape | 0–5 (0) | Tile the field so colonies can share one patch. The bog pass runs once over the whole map, so the repeated patches no longer match exactly in hammocks, deep cores, mud, causeways and looks. |

## Food

Growth potential (the engine's crop growth chance summed over farmland, by rule group: sand
inhibits, mud does not, deep water waters like water, moss waters at 48/256), 256×256, 4 colonies,
seeds 1–3, revision 6 against revision 5 on the same seeds (the starts are identical; the map report
records none, so the swarm corners come from revision 6's telemetry):

| Variant | Total | Within 24 of swarm (worst) | Within 48 (worst) |
| --- | --- | --- | --- |
| Revision 5 | 2262 | 82.4 (44) | 251.2 (158) |
| Revision 6, defaults | 2275 | 82.7 (44) | 256.5 (162) |
| Each climate, no deep water | 2275 | 82.7 (44) | 256.5 (162) |
| Mudflats 100% | 2303 | 83.3 (44) | 259.6 (166) |
| Mudflats 0% | 2254 | 82.1 (44) | 254.7 (158) |
| Hammocks 8 | 2265 | 82.6 (44) | 254.8 (162) |
| No hammocks | 2280 | 82.7 (44) | 256.9 (162) |
| No causeways | 2278 | 82.9 (44) | 252.8 (162) |
| Moss on | 2039 | 77.3 (43) | 234.2 (155) |

Mud carries the gain (it does not inhibit growth as sand does); without mud the hammocks' few tiles
of grass leave revision 6 0.4% under revision 5 in total. Deep water and the climate change no food.
Moss loses a tenth of the map's food in the tool and 6% within 24 steps, because the tool scores
every grass/moss edge tile as no farmland; that is why it is off by default.

## Limits

- One calibration map (seed 101, 256×256, Nicowar, Cortex, Cabino and Maxima, 20,000 ticks, game
  seeds 1–3), for information: worker births 447 against revision 5's 416 (+7%), per seat Nicowar
  152 against 159, Cortex 57 against 28, Cabino 228 against 220, Maxima 10 against 9; wheat
  harvested 5,930 against 5,430; 193 buildings against 194. Single games vary by up to 40%. With
  every layer off the same games reproduce revision 5 exactly. With moss on, births were 412 and
  Nicowar's fell to 120: Nicowar and Maxima read Fertile ground as water and keep their buildings
  away from it, a known AI bug being fixed in the AIs, so the map does not keep moss away from homes
  for it. No rotation tournament or human playtest has been recorded for revision 6.
- Revision 6 refuses nothing revision 5 accepted: every shape (128 to 512, square and 4:1) and colony
  count (1 to 12), two seeds each, generates at the defaults and at each variant value of the new
  controls.
- The pass costs about a third of the generation time in the same build (512×512, 8 colonies:
  5.5–5.8 s against 4.2 s with every layer off, unoptimised development build); revision 5 takes
  about 0.3 s in a release build.
- Deep cores need water at least seven vertices across, and the algae in a big pond's middle keeps
  its own shallow shoal, so cores are rings round algae beds as often as solid middles.
- Hammocks need vacant high grass 20 tiles from every home (their room for buildings); crowded maps
  place fewer than wanted (telemetry `swamp.hammocks.short`).
- Causeways are rare by construction (none on some maps) and promised to nobody; their savings are in
  `swamp.causeways.planned-saving` and `mean-saving-hundredths` per colony.
- At the maximum resource amounts revision 5's crops already overgrow some homes' building room
  after a few thousand ticks of unharvested growth; the pass does not change that.
- `SIM_REVISION` still describes revision 5.

## Verification

Run the focused contracts (`GLOB2_CONTRACTS=swamp`, `MapGeneratorContracts.h`) and the generator's
default case, sweep every shape and colony count against revision 5, and compare growth potential
with swarm corners from the `swamp.starts.swarm-x/y` telemetry. See
[generator verification](verification.md) for commands, revisions, save/replay checks and evidence
requirements.

## Implementation source

[SwampGenerator.cpp](../../src/map/generator/generators/SwampGenerator.cpp) owns this landscape's
construction, controls and validation; the height-field stages are shared in
[Terrain.cpp](../../src/map/generator/shared/Terrain.cpp). See the [catalog](catalog.md) for its
stable command and legacy IDs.

Related: [map generators](README.md).
