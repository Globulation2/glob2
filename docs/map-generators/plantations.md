# Plantations

An archipelago of small farm islands. Every island is a plantation: a square plot of grass ringed
with one vertex of sand at its heart, the only ground a building can stand on, and wheat or wood
over everything between that ring and the beach; the sea waters the crops, since no tile of an
island is more than about ten from the water. Two sand lanes run from every plot to the shore.
Nothing joins the islands: units swim, and the straits between the Voronoi cells (`Channels`'
`straitsBetweenCells`) are an exact corner width, four by default, so only a level-3 tower reaches
the next island's first grass. A plot is smaller than a base (10 tiles square), so every colony
holds several islands from the first minute: its home island with the swarm and a completed
swimming pool, and granted islands with a swarm, a pool and an inn each. On every one of a colony's
plots the swarm and the pool stand side by side along the top, the pool against the right edge
with a clear tile all round for its level-1 upgrade (4x4 to 6x6) and the swarm against that ring,
so a plot of 10 fits swarm, ring, pool and ring with upgrade clearance. Every shipped AI built swarms on islands without a pool and pools on islands without a
swarm, breeding units that could never leave, so the pair is seeded rather than left to them (`claimNeighbourCells` deals them
round by round, as many to each colony as to any other), with a rock islet beside them. The rest
are neutral plantations of wheat, wood or both, orchard islets and rock islets. Every island is
first stamped as the nominal rounded square and the homes and granted islands dealt on that; then,
where the seed threw at least twice the islands the colonies need, the islands are reshaped: a
colony's own keep their size and vary only in squareness, wobble and a slight stretch (one that
loses its plot goes back to nominal), and the neutral ones draw the whole range, smaller or larger
(a large one fills its cell to the strait), stretched along a random heading with the area held,
rounder or squarer, more or less wobbled, so the archipelago reads as one coast's islands rather
than a tray of the same biscuit without changing what any colony was dealt. Room is the
scarcity and the swim is the cost; fairness is statistical (farthest-apart homes, nearest
granted islands) and the lobby keeps the best-scoring roll. See the generator header for the
sizes at the defaults, the shrinking order on small maps (crop band, then granted count, then
refusal) and every constant's reason.

## Tradeoffs

Room limits expansion, and travel between islands adds feeding pressure. Keep the
swarm/pool pairing when changing grants: disconnected reproduction can strand new
units. Reshape owned islands only after dealing their nominal homes and grants,
and preserve their area/plot constraints so visual variety does not silently
change each colony's allocation. Use complete seat rotations and populated games
to assess fairness and swimmers' access to inns.

## Implementation source

[PlantationsGenerator.cpp](../../src/map/generator/generators/PlantationsGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
