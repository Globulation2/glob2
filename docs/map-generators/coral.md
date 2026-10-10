# Coral

Every colony is a sea fan of land. A single trunk leaves the colony's pad on the rim and forks, and
forks again, as it grows in towards the middle, so each colony's coral is a triangle opening
inwards: narrow and solid at home, wide and fingery at the far end. The water between sibling
branches is a triangle opening inwards too, the far ends of neighbouring fans reach into each
other's gaps, and all the fans meet in a tangle in the middle. Land gets thinner and richer the
further it is from home.

- **Grown, not drawn.** The trunk leaves the pad heading for the map centre, turned off it by `lean`
  degrees (every colony alike, so the map turns like a pinwheel); everything else is `growBranches`,
  which splits every tip in two, each child turned `fork-angle` degrees (with jitter) from its
  parent (the angle on a 256-tile map; smaller maps fork wider and bigger maps narrower with the
  square root of the long side, between 0.7 and 1.45 times, since a small fan needs wide forks to
  fill its wedge and a big fan's many levels curl into rings at a wide angle), a level at a time so both sides of every fork compete equally. `branching` sets the
  levels on a 256-tile map, one more per doubling of the map and one fewer per halving; the trunk is
  sized so the lengths of every level reach just past the centre, and where that trunk would be
  longer than 30 tiles each level keeps more of its parent's length (up to all of it) instead, so
  the fan fills out rather than hanging off a long bare trunk. On a small map levels are given up
  until the trunk clears its pad, so on 128-tile maps branching above 4 changes nothing.
  `branch-width` is the trunk's width; branches taper to about eight tiles.
- **Checked against every colony.** The fan is grown once, for colony 0, and turned round the map
  centre for every other colony, so it is fair however random the growth. Every branch and bud must
  keep `strait-width` tiles of water from all land already grown, from that land's copies in every
  other wedge and from its own copies; a refused branch is retried at half length, then dropped
  with its subtree. This refusal is what makes neighbouring tips interleave, and it keeps the land
  from ever reading as spokes and rings.
- **Bridges.** `land-bridges` per pair of neighbours join the narrowest straits between a fan and
  its neighbour's copy, 30 tiles apart, preferring straits away from the rim (where a bridge would
  join two trunks just below their pads); the search widens until every boundary has a bridge.
  With none, colonies meet only by swimming.
- **Pads.** Pads sit as far out as the wrap allows, `home-size` percent (60–200, 130 by default) of
  a standard pad of 12 tiles of radius (less on a small map, at most 15% of the half side), and
  shrink down to 7 until neighbouring pads keep six tiles of sea. A bigger pad is more room for the
  first economy; it also brings the trunk's root in, so at large sizes the fan has less room. Maps
  under 128 tiles across are refused, as are layouts whose trunk cannot clear its pad.
- **Resources.** Each pad has an unscaled kit of 40 wheat and 30 wood in front of the swarm and a
  quarry behind it. Remoteness is the walk along the coral from the nearest pad: 40% of forks
  carry stone growing from 3 to 9 tiles with it, and every tip a fruit grove from 3 to 12 tiles. The
  branches' wheat and wood are ranked by `PeriodicNoise` sampled in the wedge frame, leaning
  towards home: 30% of their grass under wheat and 21% under wood, since the branches are all the
  land there is. A few dry sand patches (8% of the branches' inland grass, three or more tiles from
  water, never on a pad, bud or bridge) decorate the wider branches. Algae is counted over the
  shallows up to ten tiles out, one clump per 35 tiles, and seeded on each colony's best-growing 30%
  of that water, the same number of clumps in every wedge.
- **Routes opened and checked.** After `secureStartingCrops`, an `openRoad` from every colony to
  its trunk's first fork, and with bridges from colony 0 to every colony, clears any deposits in
  the way. `validateWorld` rebuilds the design and requires every trunk's centre line to be land,
  every colony able to walk to its first fork and, with bridges, every colony reachable on foot
  from colony 0.
  middle of every branch that forks on and every bridge, off the pads and buds; dead-end tips get
  none, since they carry no traffic and their narrow land would be left without fields. Every tile
  touching the sand loses the pure grass a deposit or a building needs, so nothing can grow or be
  built across the road. Branches never taper below about eight tiles (`branch-width` 7–17, 11 by
  default, is the trunk's), so grass remains either side of the road.
- **Rectangular maps.** The fan is designed in a circle on the map's shorter side and placed on the

## Implementation source

[CoralGenerator.cpp](../../src/map/generator/generators/CoralGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
