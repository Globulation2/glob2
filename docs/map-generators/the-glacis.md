# The Glacis

Star forts in open country. Every colony starts, with an ordinary swarm and the lobby's workers, in
the courtyard of a bastioned star fort: an angular stone trace of arrowhead bastions, a water moat
following every salient and re-entrant, a pale covered way, and the glacis itself - a broad ring
of bare, cleared grass with a sand foot - standing out against the woods that crowd up to it.
Sand tracks leave four gates over causeways; between the forts run meandering streams, crossed by
the country roads at contested fords.

- **The fort.** One stencil (`turnStencilTile`/`turnStencilVertex`, `Orbits.h`) stamped at every
  colony of a lattice (`latticeSites`, `dealStarts`) turned by one quarter turn drawn per map, so
  every fort is an exact translation of every other to avoid orientation differences in axis-scanning AI heuristics. `fort-size` (20-36, 30) is the bastion tip radius;
  `bastions` (Mixed, Four-pointed, Five-pointed, Six-pointed; Mixed draws one per map) chooses the
  trace, whose curtain radius is 0.62 to 0.68 of the tip (a slender 0.56 trace read best but left a
  courtyard too small to grow a town in). Outwards from the trace: a wall 2.2 tiles thick, a berm
  of 2.5 (the moat's beach never reaches a wall tile), three water corners of moat, two sand
  corners of covered way, `glacis-width` (4-14, 10) tiles of glacis and one sand corner of foot.
  `gates` (1-4, 4) open in the curtains facing front, back and the flanks, each a three-tile gap,
  a five-corner causeway and a three-corner track to the foot. `starting-towers` (0-3, 0) raises
  two towers per colony in the front bastions (the bastions flanking the front), scored by the
  fort's own approaches (`chooseTowerSites`, `settleStartingTowers` with the gates as goals).
- **The gardens.** Every courtyard tile 1.5 tiles behind the fort's middle is garden, closed off
  from the town in front by one row of sand corners and split by a sand lane as wide as the back
  gate. The left half is wheat with a cistern towards its back; the right half is wheat with a
  second cistern inside an arc of sand at 0.98 of the courtyard's radius and wood beyond it, in its
  bastion (forts under 26 have no arc: the right half is all wood). Where a line meets the wall at a slant, any
  diagonal of grass it could not close becomes wall. Wheat is planted over 75% of each wheat
  garden, nearest the swarm first (a solid band from the garden line back), and wood over 45% of
  the wood garden, scaled by the amounts, never below 40 wheat and 20 wood. Renewable water exposure and harvest frontage support the food-heavy layout.
  Cisterns stay at the back so they do not interrupt the contiguous wheat rectangle
  nearest the swarm: Numbi's `estimateFood` scans that rectangle rather than the
  entire food inventory, making patch continuity and orientation relevant.
- **The country.** Every tile belongs to its nearest fort; along every boundary of more than 24
  tiles between two forts' country runs a stream, the boundary walk displaced by two sine waves of
  wavelength and phase drawn per stream and tapered to nothing at its ends, three corners wide.
  Its middle is a 5x5 sand ford, and a road runs from it to the nearest gate of each fort it lies
  between (`cheapestWalk` over the uplands noise, sharing roads, avoiding water and the ground
  within eight tiles of a glacis foot). A pond per 1,800 country tiles, grown irregular round a
  noise key, waters fields out in the country. Forest covers 30% of the country (scaled by wood),
  densest against the glacis foot; `furnishGround` lays fields, outcrops and groves; every ford
  has three fruit groves and a quarry on both banks.
- **Negotiation.** When the lattice is too tight for two forts' zones and 16 tiles of country the
  glacis narrows by two down to 4, then the fort by two down to 20, then the map is refused;
  garden floors scale with the fort (never below 0.3 of the default's). Supported: 256x256 up to
  6 colonies, 512x512 up to 12, 128x128 up to 2, rectangles in proportion.
- **Checked, not assumed.** Every wall tile stone and every gate open (`wallStanding`); no way
  into a courtyard but its gates (`pieceLeak`); nothing planted on a glacis and no pure grass
  joining a glacis to grass a crop could spread from; the gardens sealed from the town (a flood of
  the finished grass); every colony with the same number of towers; every colony walkable from the
  first; wheat within 24 and wood within 32 of every colony (`startingAccessFailure`).

## Implementation source

[GlacisGenerator.cpp](../../src/map/generator/generators/GlacisGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
