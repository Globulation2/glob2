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
so a plot of 10 is exactly swarm, ring, pool, ring (8 until 2026-09-16, when a pool could not
upgrade). Every shipped AI built swarms on islands without a pool and pools on islands without a
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

- **Played.** A rotation tournament (six 256×256 maps, four colonies, every cyclic team
  rotation, four Nicowars, 45,000 ticks) found the archipelago even and hard-fought: pooled
  per-start peaks of 124 to 158 units, 28 to 44 warriors, three eliminations in 96
  colony-games, a root-mean-square position bias of zero, and 45 to 65 starvation deaths per
  colony as swimmers outrun their inns, which is the map's cost of the swim. The defaults stand.
- **Seen.** A maintainer's first look found the islands too alike, all one size and all round;
  revision 2 reshapes them as above. A first cut reshaped the colonies' islands with the rest,
  and the same tournament paid for it with a 30-point position bias and pooled per-start units
  from 104 to 183: what a colony is dealt must stay what it was. Reshaped after the deal, the
  same tournament came back to revision 1's numbers: position bias zero, pooled per-start units
  87 to 128 (revision 1: 81 to 125), wheat harvested per colony 208 against 201, births 190
  against 189, seven eliminations in 96 colony-games.

## Implementation source

[PlantationsGenerator.cpp](../../src/map/generator/generators/PlantationsGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
