# Coral

Every colony is a sea fan of land. A single trunk leaves the colony's pad on the rim and forks, and
forks again, as it grows in towards the middle, so each colony's coral is a triangle opening
inwards: narrow and solid at home, wide and fingery at the far end. The water between sibling
branches is a triangle opening inwards too, the far ends of neighbouring fans reach into each
other's gaps, and all the fans meet in a tangle in the middle. Land gets thinner and richer the
further it is from home.

- **A tropical reef (revision 5).** The coral is drawn from the terrain catalogue on canonical
  mechanics, with one fixed look per kind of ground painted last (`composeLooks`): lush grass
  everywhere, homes and expansion courts included (revision 7; earlier revisions mottled it with
  plain grass and left the courts plain), pale sand on every shore and court ring (kept,
  since algae regrows only from sand), a paved road (Paths) down every branch that forks on, the
  land bridges grass and beach like the branches (revision 8; reef flats before), and a turquoise shelf of shallows ringing the whole reef
  with dark deep ocean beyond. Deep water (`deepenWater`) takes open water at least 8 tiles from
  land, and only the cores joined to the ocean beyond the pads' ring, so its edge follows the
  coasts and the straits and lagoons inside the fans stay shallow: swimming within the tangle is
  as it was, swimming out at sea is slower. The deep ocean takes part of the algae band, so the
  band's tiles per clump and its best-growing share are scaled by the band's size before and
  after (`algaeBandTiles`), which keeps the algae supply within a few percent of revision 4's.

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
- **Expansion nodes.** By default, up to two neutral building courts widen junctions within
  each colony's branching fan. The same sites repeat for every colony, at different network
  depths where space allows. Each court has a circular grass interior of radius 4 and a complete
  one-corner sand perimeter. Ordinary crops cannot cross that perimeter; the court starts empty
  and can fit one 4×4 building with access around it. Roads meet the sand ring
  rather than cutting through the building ground. Nodes remain reachable from their colony,
  including when the roads are disabled. The **Expansion nodes** control requests 0–3 per fan
  (default 2); crowded layouts omit nodes that would overlap homes, another node or a rival fan's
  water strait. These are construction sites, without starter buildings or resource kits.
- **Resources.** Each pad has an unscaled kit of 40 wheat and 30 wood in front of the swarm and a
  quarry behind it. Remoteness is the walk along the coral from the nearest pad: 40% of forks
  carry stone growing from 3 to 9 tiles with it, and every tip a fruit grove from 3 to 12 tiles. The
  branches' wheat and wood are ranked by `PeriodicNoise` sampled in the wedge frame, leaning
  towards home: 30% of their grass under wheat and 21% under wood, since the branches are all the
  land there is. Algae is counted over the
  shallows up to ten tiles out, one clump per 35 tiles, and seeded on each colony's best-growing 30%
  of that water, the same number of clumps in every wedge.
- **Routes opened and checked.** After `secureStartingCrops`, an `openRoad` from every colony to
  its trunk's first fork, and with bridges from colony 0 to every colony, clears any deposits in
  the way. `validateWorld` rebuilds the design and requires every trunk's centre line to be land,
  every colony able to walk to its first fork and, with bridges, every colony reachable on foot
  from colony 0.
  With **Paved roads** (control ID `sand-roads`, kept for saved requests) enabled, a road two
  vertices wide (`paveRoute`, so the traced tiles are pure Paths) runs down the middle of every
  branch that forks on, laid after the beaches and off the pads, buds and expansion courts; dead-end
  tips get none, since they carry no traffic and their narrow land would be left without fields.
  Roads double walking speed, can be built on, and nothing grows on or across them; the tiles
  beside them are seams nothing can be built on, and the beaches keep a walkable lane along every
  edge. The land bridges carry no road: since revision 8 they are grass with a beach on both shores,
  like the branches (they were reef flats, slow and unbuildable, until then); nothing is planted on
  them, but a building or grown crops can now narrow a crossing. The roads take branch grass, so
  the branches' standing wheat and wood (shares of their grass) are about 10–17% lower than
  revision 4's. Branches never taper below about eight tiles (`branch-width` 7–17, 11 by
  default, is the trunk's), so grass remains either side of the road.
- **Rectangular maps.** The fan is designed in a circle on the map's shorter side and stretched
  along the longer side. Expansion courts retain their circular shape and building capacity.

## Implementation source

[CoralGenerator.cpp](../../src/map/generator/generators/CoralGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
