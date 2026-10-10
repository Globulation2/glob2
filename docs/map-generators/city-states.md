# City states

A large shared commons in the middle of the map, ringed by a strait, and round it one big home for
every colony: a wedge of the outer land with its own lake, fields and quarry, cut off from its
neighbours by water channels and from the commons by the strait. The only way off a home on foot is
its causeway, a road across the strait lined with stone, landing on the commons at the home's own
angle; a wall of stone round every home's coast keeps anything landing from the sea on the beach.
The commons is where the game is fought, richer towards its centre, where an orchard of all
three fruits stands round the central lake; once swimming pools let armies cross water anywhere, the
causeways stop being the only way in.

City states stays round on a rectangular map, in a circle on the shorter side: stretching it gives
the homes different shapes (deep and narrow along the long axis, wide and shallow across it), which changes field area and fertility. Keeping a circle preserves comparable home geometry.

- **Geometry.** A pure function of the request (`geometryFor`). The commons' radius is
  `commons-size` percent of half the shorter side, the strait `strait-width` percent of the shorter
  side, and the homes reach out to the map's half side less a rim of sea, so opposite homes never
  meet across the wrap. Both coasts are radial shapes (`coast-roughness`); the strait follows the
  commons' coast. Every home is the same wedge turned round the centre, so the layout is fair for
  any colony count. `validateRequest` needs at least 20 tiles of home between the strait and the
  sea, and at each home's inner coast at least the causeway plus ten tiles of arc beyond its
  channel.
- **Causeways and walls.** One causeway per home at the wedge's middle: a `causeway-width` road
  across the strait with shoulders either side. With `stone-walls` (on), stone stands on every
  solid-grass shoulder tile and, round every home, on every solid-grass tile that touches the
  sea's margin (the land whose corners the beach reaches, and any beach joined to it), so every
  step off a beach lands on stone and the road is the only way in. Grass may never touch water, so the beach pass always
  leaves a sand lane outside the stone that a unit can land on and walk along but never leave; the
  causeway as a barrier, and as the ground kept clear of deposits with both its approaches,
  includes its lanes. Home lakes keep seven tiles from the sea so the two beaches never meet.
- **Sand roads.** With `sand-roads` (on), a line of sand two vertices thick, which nothing
  can grow over or be built on, runs from the heart of the commons along every home's axis, over
  its causeway, to a main street seven tiles inside the gate that follows the strait out to both
  flanks. Side streets turn inland from it past both sides of the swarm and at its ends, stopping a
  few tiles behind the swarm and at most half the home's depth in, short of the lake and the kit's
  fields. A ring road at half the commons' radius, or at the fords when the heart is a delta, joins
  every home's road. A vertex only turns to sand on land, off ridges and causeway shoulders, at
  least five tiles from any water and three from any sand patch inside a home, and two from water on
  the commons, and clear of the swarm's square. Sand there could open a gap in a wall, since stone
  stands only on grass. Where a road would break a rule it simply stops. The sea's margin never
  spreads along a road, and valley lakes keep clear of roads.
- **Every roll differs.** Both coasts are random harmonic profiles periodic in the wedge
  (`coast-roughness`): bays and headlands with a finer ripple on the commons' coast, which the
  strait follows, and bays into the flanks of every home's inner and outer coasts, all held flat
  around each causeway; every channel bows sideways by the same random amount. One home layout
  and one heart layout are drawn per map. Homes: Lakeland (one lake), Riverside (a creek from the
  lake towards one flank with a sand ford), Highland (two stone ridges out to the sea with a pass
  each) and Marsh (four ponds). Hearts: a
  lake with the orchard on its shore, a stone crag with a gap towards every landing, an island in
  the lake reached by a ford from every landing, a delta of rivers from the lake to the strait
  between the landings with a ford each, and a belt of forest round the orchard. `sand` adds
  patches of sand (per 64×128 of land) over homes and commons, clear of lakes, landings and
  coasts. Every home feature is designed in the wedge's frame, arc across it and radius out, and
  stamped into every home alike, so any roll stays fair by rotation.
- **Homes.** Each has one lake at six tenths of its depth (held clear of both coasts), the swarm
  between its causeway and the lake, an identical unscaled kit of 40 wheat and 30 wood beside the lake and a stone deposit beyond
  it, then its own scaled ambient farmland on fertile ground, outcrops and a grove.
- **Commons.** A central lake of `kHeartShare` of its radius, `valleys` extra lakes (per 128×128 of
  commons) likelier towards the centre, farmland on fertile ground weighted by depth inward per
  `frontier-richness`, outcrops and groves by the same weight, and the orchard of the three fruits
  on the central lake's shore. Algae seeds every shallows. `guaranteeStartingResources` runs with the
  walls' stone protected, causeways and approaches are cleared, and a cheapest-walk pass keeps a
  way open from every swarm to its causeway and from every landing to the heart.
- **The archipelago.** The sea outside the design circle includes the corners that
  wrap into one ocean and, on rectangular maps, the outer bands.
  Round islets of radius 11 dot it:  Round islets of radius 11 now dot it: one on the wrap point, then `islands` (0-4, 2) rings of
  eight, sixteen and so on at equal angles round it, at least two radii plus the moat apart, so the
  set has the map's own four-fold symmetry with mirrors; on a rectangle the two points across the
  wrap on each axis get the same rings. An islet whose disc and three tiles of water round it do not
  lie wholly in the sea - too near a home's outer coast, or another islet - is left out with its
  mirror images, so the symmetry holds. Every islet carries the farms' 10x4 building plot
  (`stampFarmPlot`, a clearing of grass in a two-vertex ring of sand) at its middle, kept clear of
  everything. Wheat fills every plantable tile outside that sand border, on all sides of the
  island, independently of the ambient resource amount controls. Arrivals must harvest an approach
  through the wheat to reach the plot. An islet is only ever reached by swimming: a forward post,
  not a stepping stone. The archipelago
  cannot be turned round the centre for every colony like the rest of the design, so with three,
  five or six colonies it lies nearer some homes than others, which the lobby's best-of-five rolls
  cover, as on Canals. A 128 map's corners hold only the islet on the wrap point; a 256 map holds
  thirteen at the defaults.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires every causeway road
  and ford walkable and every designed stone tile present (shoulders, walls, ridges and crag), every colony and the heart reachable on foot from
  colony 0, every islet's plot buildable and no islet reachable on foot, no colony reachable from any beach with the roads shut, no home able to reach the
  commons or another home with the causeways shut, and the colonies' walks to their landings within
  twelve steps of each other. `validateRequest` needs at least 20 tiles of home depth.

## Implementation source

[CityStatesGenerator.cpp](../../src/map/generator/generators/CityStatesGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
