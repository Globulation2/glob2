# Rice terraces

Long terraced hillsides that run diagonally across the whole torus. Every hillside is one
continuous stripe: a dry crest where the towns stand, narrow contour strips of rice and
irrigation channels stepping down both of its slopes, and a valley floor of shared ground with a
river along its middle. With a slant, a hillside is one long spiral round the torus, so the
terraces join into a single stretch of landscape. The contours sway together along the hillside,
so the terraces meander rather than run ruled. Sand stairs climb straight across the terraces and
carry on across the valley as roads that ford the river.

This map replaced an earlier generator of the same name on 2026-09-16. That one drew
concentric crop and water rings round point summits and is now [Hills](HILLS.md). A
maintainer found that the rings did not read as rice terraces, however lobed, because real
terraces are not rings round a point. They are many narrow strips following the contour of a
long slope, stacked and winding across whole hillsides. This design starts from those signatures
and borrows Rain shadow's slanted stripe field for the geometry. The
[tuning playbook](../../.agents/skills/glob2-map-design/references/tuning-playbook.md)
records the lesson as "model the real thing, not the nearest primitive".

## Play contract

- **Crests are the towns.** Every colony starts in a round grass clearing on the middle line of a
  crest. Towns are spread along the crests as far apart as they go: the first at a seeded crest
  tile, each next one the crest tile farthest from the towns before it. The terraces bend round
  each town inside a two-corner sand ring. A town has a completed starter inn with a first meal,
  a quarry tile, 14 wheat tiles on the first terrace of each slope below it and 12 wood tiles on
  one of them. These starter supplies are kept at every resource amount.
- **Terraces are the farms.** Each terrace is five corners of rice (wheat) and two of water at
  100% band width. Every rice tile lies within the engine's growth probe of several channels, so
  the terraces regrow. Half the fertile rice ground starts under wheat and 6% under wood, in
  patches. Sand caps above and below each slope's terraces, and each town's ring, keep crops off
  the towns and the valley however long the game runs. The validator checks that no pure grass
  reachable from a town can ever be a terrace strip.
- **Stairs are the only way through.** Water and crops block walking, so the terraces are a wall
  between a crest and its valley. Stairs five corners wide cross them every `stair-spacing` tiles
  along the hillside. The two slopes of a valley are staggered by half a spacing, so no stair
  lines up with the one facing it. In the valley each stair continues as a three-corner sand road
  to the middle and past it, fording the river.
- **Valleys are the commons.** Quarry outcrops and fruit groves are scattered over the valley
  floor, and algae in the channels and river. Colonies on facing slopes meet in the valleys.

## Controls

| Control | Values; default | Effect |
| --- | --- | --- |
| Hillsides | 1–6; **1** | Hillsides per 256 tiles of the map's longer side. On a 256 map at slant 1, one hillside crosses the map twice. |
| Slant | 0–3; **1** | As in Rain shadow: how many hillsides a line across the width climbs. With a slant every crest is one spiral round the torus. |
| Terraces per slope | 2–12; **8** | Terraces down each side of a crest, cut to what fits. |
| Contour band width | 80–140%; **100%** | Scales each terrace's rice and water widths. |
| Waviness | 0–16, step 2; **12** | Amplitude in tiles of the contours' sway along the hillside. |
| Stair spacing | 24–96, step 8; **48** | Target tiles between stairs along a hillside, rounded so a whole number fit per turn. |
| Home size | 10–18; **12** | Town radius. The crest is three tiles wider on each side. |
| Valley river | on/off; **on** | A river along every valley's middle; roads ford it. |
| Wheat / wood / stone / algae / fruit amount | 0–300%; **100%** | Terrace crops, valley outcrops and groves, and algae. Town starter supplies stay. |

## Construction

1. **Fitting.** Hillsides start at the control's count scaled to the map. Hillsides are added
   until the crests' total length (map area over the stripe spacing) gives every town a town's
   width of crest on each side. They are taken away one at a time until a slope holds the
   terraces asked for beside a crest, the narrowest valley (16 tiles) and the sway. Then the
   terraces are cut to what fits. Only a slope with no room for one terrace refuses the map.
2. **Hillsides.** `stripePhase` gives every tile its place across the hillside, with a fine
   fractal grain of 3% of a spacing. The sway adds the same number of tiles to every contour at
   a point along the hillside: three sine waves of about 110, 60 and 36 tiles (whole numbers of
   waves per turn along, so the stripes still wrap) sharing the waviness in the proportions 50,
   30 and 20. A shared shift keeps each terrace's width along the slope's normal and cannot fold
   a contour; only where the hillside turns steeply do the terraces narrow.
3. **Zones.** By distance from the crest: crest, cap, terraces (rice then channel, down the
   slope), cap, valley. Stairs replace the cap and terraces within 2.5 corners of their line.
   Roads replace valley ground within 1.5 corners of it. The river runs within 1.5 corners of the
   valley's middle.
4. **Towns.** Farthest-point selection on the crests' middle line, then a clearing of the town's
   radius and a sand ring where the clearing meets anything but crest.
5. **Beaches** last, then the colonies, the starter inn, quarry and crops, the terraces'
   furnishing, the valley's outcrops and groves, algae, and the shared first-crop rescue, which
   may plant only on a terrace strip.

The starter crops search for the first-terrace tile nearest the town on each slope, skipping any
pocket a stair or the town's ring has cut too short for the whole patch.

## Verification

- 60 generation cases at revision 1 all complete. The ten sizes and colony counts the golden
  sweep uses (128/2, 128/4, 256/2–12, 512/4, 512/12) ran on four seeds each. Every slant,
  hillside, waviness, band width, home size, stair spacing, terrace count and river extreme ran
  at 256/4 on two seeds.
- The golden table and sweep cover it like every registered generator.
- No AI tournament or human play has been run on this map yet.
