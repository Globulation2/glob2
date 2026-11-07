# Bajada

**Bajada** (`bajada`, numeric ID 55) is a desert mountain front: the country where a dry
range meets its basin, as in the Mojave or the Great Basin. Long rock ranges cross the map, each cut
by a few passes. Below both faces of every range hangs a row of alluvial fans, each a spring at the
range's foot and a tree of streams that splits as it runs downhill, green along its streams and dying
out in pale washes. Grain size sorts down the slope: talus and coarse gravel at the foot of the
range, varnished pavement over the middle of the bajada, clay towards the playa. In the middle of
each basin lies the playa, a cracked hardpan pan with salt crust, shallow lakes and spring-fed
cienegas at its edge. Every colony's home is a fan of its own with its town on the fan's dry
shoulder and a quarry face in the range above it.

Revision 4 (Bajada 2.0) rebuilt the map for the engine's full terrain palette; revision 3 drew
everything from grass, sand and water.

## How it plays

- **A fan is a farm.** Crops regrow only where the growth probe finds water, so a fan's streams are
  its farmland. Every home fan carries its starter wheat in a garden between the town and the
  streams, beside a small garden pond, a few steps from the swarm.
- **The ranges are rock; stone is a place.** Range rock is impassable and permanent, and towers
  shoot over it (the Ridges rule group). Stone stands only on quarry faces: one in the range straight
  above every home's town, the same on every home, and more along the faces between the homes
  (`stone-amount`). A colony's back is safe except through a pass.
- **Talus is slow.** Scree skirts the rock and the passes' mouths: walkable at reduced speed, no
  buildings, nothing grows.
- **Pavement is room that never grows shut.** Desert beyond the reach of any water is mostly
  pavement: buildable, but no crop can stand on it and it slows growth near it as sand does. It lies
  only beyond the water's reach, so it costs the fans no farmland. Towns are ringed by pavement
  instead of sand, which seals them from crops without costing building room.
- **Neutral fans are the contested farms.** Fans between the homes and on the far face of each basin
  are fertile ground nobody starts on; their streams keep 26 tiles from every town.
- **The playa is a slow open middle.** Its hardpan floor is walkable at reduced speed and takes no
  buildings or crops. Lakes stand in it (swimming makes them short cuts), and **cienegas** at its
  edge (spring meadow round a seep, ground that waters itself) are farmland out in the basin.

## How it is built

`src/map/generator/generators/BajadaGenerator.cpp`; the header comment and the comments on the
constants record why each choice was made. The design keeps a mechanic sketch (the canonical type
of each rule group, which every shared helper and check reads) and paints looks over it with
`composeLooks` (`shared/Sketch.h`), so looks never change a rule.

| Stage | What happens |
| --- | --- |
| Plan | As many ranges as fit one `range-spacing` apart. Fans take 60% of the slope to the playa; the widest playa takes the rest. One home design and a mirror image are drawn per map, and one lithology. |
| Home stencil | One home fan in its own frame: springs at the range's foot (a spring-meadow ring for Cienega), a town of radius 11 with a pavement ring, a garden pond, a distributary tree (`growBranches`, the fullest of four), green ground along the streams, the kit (24 wheat, 14 wood) and the fan's share of farmland sown on the stencil's growth field. |
| Ranges | A swaying centre line that bends away from homes but never into them, straight across every home's footprint; massifs, a rough edge, spurs. |
| Passes | Wandering sand floors through the rock, clear of every home's apex and quarry face. A range whose passes cannot all keep clear of the quarries on a crowded map drops a pass (never its last) and says so in telemetry. |
| Neutral fans | Spring fans, dry fans (washes only) and empty slots, clear of the homes. |
| Playa | Lakes with salt rims and meadows, the hardpan pan across the basin floor with salt crust patches, and cienegas at its edge. |
| Homes stamped | Every home's footprint is erased and the stencil drawn over it, tile for tile, turned half a turn below the far face. |
| Quarries and talus | Each home's quarry face is found in the stencil's columns, so every home's is the same; ambient faces along the ranges; scree round the rock. |
| Terrain | Rock vertices only where all four tiles are rock, so the blocked region is exactly the drawn one and every quarry tile keeps four grass corners. Dune bands; pavement beyond the water's reach; looks by lithology and slope. |
| Resources | The stencil's crops, farmland on neutral fans, meadows and cienegas (`furnishGround`), outcrops and lone scrub on dry grass, fruit groves, algae, quarry stone. |

## Controls

| Control | Range (default) | Effect |
| --- | --- | --- |
| Range spacing | 128–256 (128) | Ranges across the height; with one range, longer fans and wider lakes. |
| Ridge width | 8–16 (12) | Width of the rock ranges. |
| Passes | 1–6 (2) | Passes per range; a crowded range may drop one to keep clear of the homes' quarries. |
| Fans per range | 3–8 (6) | Fan slots per 256 tiles of range. |
| Stream reach | 70–150 (100) | Length and forking of every stream tree. |
| Playa | 0–100 (80) | Lake width; 0 leaves a dry hardpan pan. The pan always fills the basin floor. |
| Cienegas | 0–100 (50) | Spring-fed marshes at the pan's edge (up to six per edge per 256 tiles). |
| Home design | Random, Broad fan, Long fan, Twin springs, Canyon mouth, Cienega, Coalescing pair | The one home stencil every colony gets. |
| Rock | Random, Granite, Sandstone, Volcanic | Looks only: rock, talus and pavement artwork. Plays identically. |
| Wheat amount | 0–200% (100) | Neutral fan, meadow, cienega and home farmland. The kit is guaranteed. |
| Stone amount | 0–300% (100) | Ambient quarry faces and outcrops; every home's quarry is guaranteed. |
| Wood, algae, fruit amount | 0–300% (100) | Linear. |

## Food

Growth potential (the engine's crop growth chance summed over farmland, counting pavement as an
inhibitor and spring meadow as a weak source), 256×256, 4 colonies, seeds 1–3, each design pinned,
revision 4 against revision 3:

| Design | Total | Within 24 of swarm (worst) | Within 48 (worst) |
| --- | --- | --- | --- |
| Broad fan (r3) | 481 | 26.0 (24) | 69.8 (48) |
| Broad fan | 518 | 25.5 (23) | 69.7 (47) |
| Long fan (r3) | 503 | 28.5 (26) | 72.5 (59) |
| Long fan | 541 | 27.2 (24) | 73.8 (58) |
| Twin springs (r3) | 511 | 31.1 (24) | 77.0 (64) |
| Twin springs | 558 | 29.6 (23) | 76.4 (63) |
| Canyon mouth | 504 | 25.3 (23) | 65.5 (52) |
| Cienega | 532 | 35.2 (32) | 76.3 (64) |
| Coalescing pair | 543 | 26.7 (24) | 77.8 (59) |

Within 24 steps the original designs lost 2–5% to pavement inhibition and kept their food within 48;
total food rose 7–9% with the cienegas. Canyon mouth is the leanest design within 48 steps. The
terrain dump counts a tile between two looks of one rule group as that group (`map study`), so
these figures supersede the first revision 4 measurements, which undercounted every such tile.

## Limits

- Home fans are identical copies along their row: fair, and visibly stamped; six designs vary the
  stamp between maps.
- The range behind a home may stand up to three tiles further back from one home than another (it
  bends away from homes, never into them); every home's quarry face holds the same stone.
- 128-wide maps take at most 4 colonies; single-range maps put homes on both faces.
- Stone is scarcer than in revision 3, where every range tile was an infinite quarry.
- No rotation tournament or human playtest has been recorded for revision 4.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused Bajada contracts (`MapGeneratorContracts.h`) and the
platform golden rows, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[BajadaGenerator.cpp](../../src/map/generator/generators/BajadaGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
