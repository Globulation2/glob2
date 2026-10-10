# Rain shadow

A map with a grain: long stone ridges run across the torus, parallel and a valley apart, and the wind
blows across them. The windward foot of every ridge catches the rain (a chain of pools, and the
farmland), the lee side is a band of dry sand where nothing grows or builds, and passes cut every ridge
at intervals staggered from ridge to ridge, so moving along a valley is easy and crossing is slow.

- **Ridges.** `ridges` (2-8, 4) crossings of the map, tilted by `slant` (0-3, 1) so following one across
  the width climbs that many ridges: stripes that wrap the torus exactly (`stripePhase`), so with a slant
  every ridge is one spiral, which is what staggers its own passes from one crossing to the next
  (straight ridges stagger by parity instead). On a small map ridges are taken away until a valley
  holds a home with its margins, the pools and the sand (four suit a 256 map, a 128 map gets two).
  `ridge-thickness` (2-6, 3): three tiles is sealed against diagonal steps and within a level-1 tower's
  reach across (`towerReach`).
- **Passes.** Every `pass-spacing` tiles (24-96, 48) along the ridge, `pass-width` (3-9, 5) wide,
  laid out by the phase along the ridges (`alongStripes`), so they repeat seamlessly.
- **Wind.** Streams run along the windward foot, 20 tiles long every 24 and 7 deep, four tiles out from the stone
  (three took the ridge's windward row with them: a pool's beach spoils the tiles round it and stone
  stands only on pure grass); the lee sand starts two tiles behind the ridge for the same reason and
  runs `lee-width` (2-12, 6) tiles, measured downwind from the stone itself (`upwindSteps`) so it stops
  where a pass lets the rain through.
- **Pass roads, lakes and rivers.** A line of sand runs through every pass and 18 tiles into the
  valley on either side, so a pass never grows shut, and on the windward side
  it ends in a lane of sand along the middle of the foot out to the streams either side of the pass,
  so the road runs into their beaches instead of stopping short of them in the grass; no stream lies
  within 4 tiles of a pass along the ridge. `inland-lakes` (on) throws lake sites 56 apart, keeps those within 18% of a valley's middle
  phase, grows a rough lake of radius 5 at each and joins it to the nearest stream by a wandering river
  one tile wide (`wanderingPath`); `sand-patches` (0-20, 6) percent of the valleys' grass is sprinkled
  into sand patches (`sprinkleSand`). These features add variation within the parallel valleys.
- **Homes.** On a lattice, each slid along the wind to the middle of its valley, so every home has the
  same ridge behind it and the same foot before it. Crops go on the fertile ground, which the pools make
  the windward side of every valley: a third of it under wheat and a fifth under wood.
- **Checked, not assumed.** Every ridge tile that could hold stone does, every pond present, every
  colony walkable from the first through the passes.

## Implementation source

[RainShadowGenerator.cpp](../../src/map/generator/generators/RainShadowGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
