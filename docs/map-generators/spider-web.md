# Spider web

A frost web spun over a frozen sea. Spokes run from a glacier dome at the centre of the map out to a
frame thread, capture threads cross between the spokes sagging towards the dome, and every colony
starts on a green pad, a hot-spring valley where its spoke meets the frame. White roads run down
every thread; the threads themselves are snow-drifted tundra, building room where nothing grows.
The cells between them are frozen: a slow, cold shortcut across the web. The open leads, the sea
beyond the frame and the springs are where the green ground and the food are, and the dome's
orchard is the prize.

- **The frost web (revision 7).** A remake of the frosted night web (revisions 5 and 6, grass
  threads over open water crossed only by swimmers). Drawn on canonical mechanics with one look per
  rule group painted last (`composeLooks`):

  | Ground | Rules | Look | Where |
  | --- | --- | --- | --- |
  | Frozen cells | Ice: walkable at half speed, wears ground units down (8/256 of a hit point a step), nothing grows, no building | Frozen lake; wind-swept snow on the innermost cells round the dome | Every cell but the leads |
  | Leads, the sea, springs | Water | Water | 25% of the cells' distinct keys, at least one; the sea beyond the pads; a spring on every pad, in every dew drop and at the dome's heart |
  | Threads | Barren: buildable, nothing grows, holds crops back like sand | Arctic tundra | Every thread farther than 7 steps from a lead or the sea |
  | Green threads, pads, islets, dome | Grass | Lush grass | Threads within 7 steps of a lead or the sea, every pad, every dew drop, a clearing round every stone knot, the dome's plateau |
  | Beaches | Sand: nothing grows on it, holds crops back | Pale sand | Every grass vertex beside open water (the sea, a lead, a spring); tundra and ice meet the water bare |
  | Roads and ramps | Paths: twice as fast, nothing grows | Moonstone | Down every whole thread, and up the dome |
  | The dome's wall | Obstacles: stops feet and arrows | Glacier | Four tiles round the hub's edge, broken by one ramp per colony up its home spoke |

  A cell's key is its sector in the wedge and its radius, shared by its image in every wedge, so
  every colony gets the same leads. A cell holding a dew drop stays frozen, so its islet is reached
  on foot; the innermost cells round the dome never open. Springs keep three tiles off every
  spoke's centre line.
- **Playing it.** Contact no longer waits for swimming: any colony can walk across the ice, at half
  speed and slowly losing health, so the threads are the fast way round and the cells the direct
  one. Nothing can be built on the ice, so the threads, pads, islets and dome are all the building
  room there is. Food grows where liquid water is: the pads (watered by the sea and their springs),
  the green threads along the leads and the frame, and the spring islets. Stone stands only on
  grass, so every stone knot is a small grass clearing. The dome is entered only up a colony's own
  ramp; the other spokes end at its wall.
- **Web.** `spokes` per colony (raised on webs with few colonies so the web never reads as a
  star, lowered on crowded ones so halfway out neighbouring spokes keep 2.5 thread widths apart),
  each bowing sideways by a random amount that is greatest at mid-length. The frame joins the
  spoke ends. Capture threads are drawn with `bezierPath` and `strokePath`, `ring-spacing` tiles
  apart (squeezed on a small map to keep a whole turn between the hub and the frame) and
  `thread-width` wide (the width on a 256-tile map; a smaller map thins its threads with the square root of its size, to no less than 75%, so a 128-tile web still reads as threads), spokes and frame two tiles wider. Each sags towards the hub by `sag` and a
  random share of it; `torn-strands` percent of the distinct strands of a wedge tear, keeping a
  stub hanging from each spoke. With `spiral` (on) the capture threads are a spiral with one arm
  per colony that climbs one spacing per wedge; off, they are closed rings.
- **Fair by rotation.** A spiral arm climbing one spacing per wedge maps onto the next arm under a
  turn of one wedge, so every capture thread and its images share a key. Every choice about a
  thread — torn, sag, bow, stone at its knot — is a stateless roll of that key from the seed, and
  dew drops are chosen in one wedge and turned round the centre into every other, so every colony
  gets the same web.
- **Pads and hub.** Pads sit as far out as the wrap allows, `home-size` percent (60–200, 130 by
  default) of a standard pad of 13 tiles of radius, never more than 16% of the half side times that
  percentage so a small map's pads leave room for the web, and shrink down to 7 until neighbouring
  pads keep six tiles of sea between them; only then is the request refused. The hub is `hub-size` percent of the half side, with a pond once it is nine tiles across.
- **Resources.** Each pad has an unscaled kit of 40 wheat and 30 wood on the hub side of the swarm
  and a quarry beyond it. About 28% of knots carry a small stone deposit in a grass clearing,
  `dew-drops` hot-spring islets per colony carry a fruit grove or stone each, and the dome's plateau
  carries the orchard of all three fruits and a quarry. The green threads' own wheat and wood are
  ranked and split by `PeriodicNoise` sampled in the wedge frame, so every colony's stretch of web is
  farmed alike: 60% of their grass under wheat (three times revision 6's share) and 13% under
  wood, so the open ground is the web's breadbasket. Algae is counted over the shallows up to ten tiles out, one clump per 30
  tiles, and seeded on each colony's best-growing 30% of that water, the same number of clumps in
  every wedge.
- **Routes opened, not hoped for.** The roads keep the threads walkable, but a small dome can be
  stocked shut, so after `secureStartingCrops` an `openRoad` from colony 0 onto the hub and to
  every other colony clears any deposits in the way.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires every vertex as
  designed, every spoke's centre line to be walkable outside the dome, every colony reachable on
  foot from colony 0 (the ice counts as walkable) and the dome reachable too.
- **Paved roads.** With **Paved roads** (control ID `sand-roads`, kept for saved requests) on, a
  white road runs down the middle of every spoke and the frame two vertices wide and down every
  whole capture thread one vertex wide (`paveRoute`), off the pads and dew drops and stopping at the
  dome, which every colony climbs by the ramp up its home spoke; torn stubs, which are dead ends,
  get none. Roads double walking speed, can be built on, and nothing grows on or across them, so
  no field closes a green thread. Threads thin on small maps to no less than 75% of the control.
- **Rectangular maps.** The web is designed in a circle on the map's shorter side and placed on the
  map by `Stretch`, so on a rectangular map it fills the map as an ellipse; widths stay in tiles.
  Square maps are unchanged.

## Implementation source

[SpiderWebGenerator.cpp](../../src/map/generator/generators/SpiderWebGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
