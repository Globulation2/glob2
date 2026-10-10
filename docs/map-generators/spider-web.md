# Spider web

An orb web of land spun over open water. Spokes run from a hub at the centre of the map out to a
frame thread, capture threads cross between the spokes sagging towards the hub, and every colony
starts on a pad where its spoke meets the frame. Threads are wide enough to build on, but every
ground route follows them: the knots where threads cross are the places to hold, the hub's
orchard is the prize, and the water between is crossed only once colonies can swim.

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
  and a quarry beyond it. About 28% of knots carry a small stone deposit, `dew-drops` islets per
  colony carry a fruit grove or stone each, and the hub carries the orchard of all three fruits
  and a quarry. The threads' own wheat and wood are ranked and split by `PeriodicNoise` sampled in
  the wedge frame, so every colony's stretch of web is farmed alike: 20% of their grass under wheat
  and 13% under wood, so any stretch of thread passes a field or a copse. A few dry sand patches
  (7% of the threads' inland grass, three or more tiles from water) decorate the threads, sampled in
  the wedge frame too. Algae is counted over the shallows up to ten tiles out, one clump per 30
  tiles, and seeded on each colony's best-growing 30% of that water, the same number of clumps in
  every wedge.
- **Routes opened, not hoped for.** Beaches keep the threads walkable, but a small hub can be
  stocked shut, so after `secureStartingCrops` an `openRoad` from colony 0 onto the hub and to
  every other colony clears any deposits in the way.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires every spoke's centre
  line to be land, every colony reachable on foot from colony 0 and the hub reachable too, with
- **Sand roads.** With `sand-roads` (on), a line of sand one tile thick (`tracePath`) runs down the
  middle of every spoke and whole thread, off the pads and dew drops; torn stubs, which are dead
  ends, get none. Every tile touching the sand loses the pure grass a deposit or a building needs,
  so nothing can grow or be built across a thread and close it. Threads thin on small maps to no
  less than 75% of the control, so grass remains either side of the road.
- **Rectangular maps.** The web is designed in a circle on the map's shorter side and placed on the
  map by `Stretch`, so on a rectangular map it fills the map as an ellipse; widths stay in tiles.
  Square maps are unchanged.

## Implementation source

[SpiderWebGenerator.cpp](../../src/map/generator/generators/SpiderWebGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
