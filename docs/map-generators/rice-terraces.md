# Rice terraces

Long terraced hillsides that run diagonally across the whole torus. Every hillside is one
continuous stripe: a forested crest where the towns stand in paved clearings, narrow contour
paddies of rice and irrigation water stepping down both of its slopes, each held up by a dark
earthen bund, and a valley floor of shared ground with a river winding along its middle between
green meadow banks. With a slant, a hillside is one long spiral round the torus, so the terraces
join into a single stretch of landscape. The contours sway together along the hillside, so the
terraces meander rather than run ruled. Stone stairs wind down through the terraces and carry on
across the valley as roads that ford the river on boardwalks.

Terraces are narrow strips following the contour of a long slope, stacked and
winding across whole hillsides. This design uses those signatures and Rain shadow's
slanted stripe field. For crop and water rings around point summits, see
[Hills](hills.md).

Since 2.0 (revision 6) the map is drawn on the terrain catalogue in one fixed look: plain grass
crests and valleys, lush-grass paddies, mud bunds, clay caps and town rings, slate stairs, roads
and town squares, boardwalk fords, spring-meadow banks, pale-sand bars inside the river's bends,
and ridge-rock crags and outcrops. There are no beaches.

## Play contract

- **Crests are the towns.** Every colony starts in a round grass clearing, with a paved square at
  its middle, on the middle line of a crest. Towns are spread along the crests as far apart as they
  go: the first at a seeded crest tile, each next one the crest tile farthest from the towns before
  it. The terraces bend round each town inside a two-tile clay ring. A town has a completed starter
  inn with a first meal, a small rock crag at its edge (the same place along the crest for every
  town) whose quarry face holds its stone, 14 wheat tiles on the first terrace of each slope below
  it and 12 wood tiles on one of them. These starter supplies are kept at every resource amount.
- **The ridge forest is the timber.** Between the towns, the crests carry clumps of wood on the
  ground farther than 16 tiles from any water, beyond the engine's growth probe, so the forest never
  grows back or spreads onto a town. 45% of that ground is wooded at 100% wood amount.
- **Terraces are the farms.** Each terrace is ten tiles of rice (wheat), six of water and a two-tile
  mud bund at 100% band width. Every rice tile lies within the engine's growth probe of several
  channels, so the terraces regrow. Half the fertile rice ground starts under wheat and 6% under
  wood, in patches. Clay caps above and below each slope's terraces, and each town's ring, keep
  crops off the towns and the valley however long the game runs. The validator checks that no pure
  grass reachable from a town can ever be a terrace strip.
- **Bunds are footpaths, not gates.** A bund is Rough ground: walkable, unbuildable, and nothing
  grows on it. It runs along its contour below a channel, so workers reach a paddy from its downhill
  side as well as its ends. Water seals every bund from the levels above and below, so a bund never
  opens a way between a crest and a valley.
- **Stairs are the only way through.** Water and crops block walking, so the terraces are a wall
  between a crest and its valley. Stairs five tiles wide cross them every `stair-spacing` tiles along
  the hillside, each winding down the slope in its own seeded jog. The two slopes of a valley are
  staggered by half a spacing, so no stair lines up with the one facing it. In the valley each stair
  continues as a three-tile road across the river, on a boardwalk ford, to the far bank. The
  validator shuts every stair and checks that no colony can walk from a crest to a valley.
- **Valleys are the commons.** The river meanders across the valley's middle, its width varying,
  with spring-meadow (Fertile) banks three to seven tiles wide. 35% of the banks start under wheat
  and 15% under wood, in clumps. Sand bars inside the strongest bends let algae regrow. Two
  ridge-rock outcrops per colony stand at the valleys' most open ground, each with a three-tile
  quarry face; fruit groves are scattered over the valley floor. Colonies on facing slopes meet in
  the valleys.

## Controls

| Control | Values; default | Effect |
| --- | --- | --- |
| Hillsides | 1–6; **1** | Hillsides per 256 tiles of the map's longer side. On a 256 map at slant 1, one hillside crosses the map twice. |
| Slant | 0–3; **1** | As in Rain shadow: how many hillsides a line across the width climbs. With a slant every crest is one spiral round the torus. |
| Terraces per slope | 2–12; **8** | Terraces down each side of a crest, cut to what fits. |
| Contour band width | 90–140%; **100%** | Scales each terrace's rice width, and its water width above 100%; channels never narrow below six tiles, since they seal the crests. |
| Waviness | 0–20, step 2; **16** | Amplitude in tiles of the contours' sway along the hillside. |
| Stair spacing | 24–96, step 8; **48** | Target tiles between stairs along a hillside, rounded so a whole number fit per turn. |
| Home size | 10–18; **12** | Town radius. The crest is three tiles wider on each side. |
| Valley river | on/off; **on** | A river along every valley's middle, with its banks and bars; roads ford it. |
| Wheat / wood / stone / algae / fruit amount | 0–300%; **100%** | Terrace and bank crops, the ridge forest, valley outcrop faces (above 100% stone also grows at an outcrop's foot), algae and groves. Town starter supplies stay. |

128-tile maps refuse at the defaults: a hillside there has no room for a terrace beside a crest and
the narrowest valley.

## Construction

1. **Fitting.** Hillsides start at the control's count scaled to the map. Hillsides are added
   until the crests' total length (map area over the stripe spacing) gives every town a town's
   width of crest on each side. They are taken away one at a time until a slope holds the
   terraces asked for beside a crest, the narrowest valley (16 tiles) and the sway. Then the
   terraces are cut to what fits. Only a slope with no room for one terrace refuses the map.
2. **Hillsides.** `stripePhase` gives every tile its place across the hillside, with a fine
   fractal grain of 3% of a spacing. The sway adds the same number of tiles to every contour at
   a point along the hillside: three sine waves of about 220, 120 and 72 tiles (whole numbers of
   waves per turn along, so the stripes still wrap) sharing the waviness in the proportions 50,
   30 and 20. A shared shift keeps each terrace's width along the slope's normal and cannot fold
   a contour; only where the hillside turns steeply do the terraces narrow.
3. **Zones.** By distance from the crest: crest, cap, terraces (rice, channel, bund, down the
   slope), cap, valley. Each stair follows its line along the hillside plus two summed sine jogs of
   its own (`rice-stairs`); it replaces the cap and terraces within 2.5 tiles of that line, measured
   across the line however steeply it turns. In the valley a road from either slope follows the same
   line within 1.5 tiles until it is past the river's far edge; where it crosses the river it is a
   boardwalk ford. The river winds round the valley's middle in two long waves of its own
   (`rice-river`), up to 7 tiles either side and less where the valley is narrow, 3 to 5 tiles
   either side of its line.
4. **Towns.** Farthest-point selection on the crests' middle line, then a clearing of the town's
   radius and a clay ring where the clearing meets anything but crest. A crag 2.6 tiles round
   stands 12.5 tiles from every town's middle along the crest, its face looking back at the town.
5. **Valley.** Outcrops (`rice-outcrops`) at the valley grass farthest from water, paving, sand and
   the caps, 24 tiles apart, each with a quarry face towards the nearer crest. Then the banks
   (`rice-banks`), frayed by noise and kept four tiles off every rock, and the sand bars on the
   inside of every bend that swings at least 60% of the meander.
6. **Forest ground.** Crest grass beyond the growth probe of all water and six tiles clear of every
   town and crag.
7. **Look.** Paved squares round every town's middle, then the fixed look painted over the
   mechanics.
8. **Resources.** The colonies, the starter inn, crag stone and crops, the terraces' furnishing,
   the ridge forest (`rice-forest`), the banks' crops (`rice-bank-crops`), the outcrop faces, the
   valley's groves, algae, and the shared first-crop rescue, which may plant only on a terrace strip.

The starter crops search for the first-terrace tile nearest the town on each slope that can hold
the crop, skipping any pocket a stair or the town's ring has cut too short for the whole patch.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees: the finished terrain matches the design vertex for vertex, every crag and
outcrop is Ridges, stairs and roads are walkable, terrace crops can never grow into a town, every
town has building room, the shut stairs seal every crest from its valley, no rock parts the
walkable ground, and every colony can walk to every other. Run the focused `MapGeneratorDefaults`
contracts and the sim-diff map fingerprints, inspect small/large rectangles and resource extremes,
then check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[RiceTerracesGenerator.cpp](../../src/map/generator/generators/RiceTerracesGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
