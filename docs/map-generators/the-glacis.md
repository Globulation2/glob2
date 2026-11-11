# The Glacis

Star forts in open country. Every colony starts, with an ordinary swarm and the lobby's workers, in
the courtyard of a bastioned star fort: an angular trace of arrowhead bastions, a water moat
following every salient and re-entrant, a crop-lined outer bank, a paved covered way, and the glacis
itself, a broad ring of bare scree standing out against the woods that crowd up to it. Paved
tracks leave four gates over causeways; between the forts run meandering streams, crossed by the
country roads at contested fords.

Since revision 6 the map is drawn from the terrain catalogue in one fixed look of its own
(`kGlacisLook`, "grey Vauban", not a shared climate):

| Feature | Terrain (group) | Play |
| --- | --- | --- |
| Curtains (scarp) | Cliffs (Obstacles) | Block units and arrows |
| Bastion faces and flanks, beyond the curtains' line | Ridge rock (Ridges) | The towers in the bastions shoot out over them |
| Glacis and its foot | Scree (Rough) | 160/256 speed, unbuildable, nothing grows: attackers cross slowly under the bastions |
| Covered way, causeways, tracks, garden lanes, roads | Slate ashlar (Paths) | Fast; nothing grows across them, so they contain the moat banks and gardens |
| The wood garden's arc | Clay (Barren) | Holds the wheat and wood apart |
| Fords | Boardwalk (Paths) | |
| Ford quarries | Outcrop (Ridges) with a quarry face | |

The walls are drawn on vertices (`wallVertices`, three vertices thick), leaning away from the gates.
They cannot be mined. The stone they used to hold comes back as two quarry faces of 12 at the
courtyard's wall foot, as far from the gates and from each other as the courtyard allows. Every fort
is a translation of the first, so they stand at the same offsets in every fort, at every stone
setting.

- **The fort.** One stencil (`turnStencilTile`/`turnStencilVertex`, `Orbits.h`) stamped at every
  colony of a lattice (`latticeSites`, `dealStarts`) turned by one quarter turn drawn per map, so
  every fort is an exact translation of every other to avoid orientation differences in axis-scanning AI heuristics. `fort-size` (20-36, 30) is the bastion tip radius;
  `bastions` (Mixed, Four-pointed, Five-pointed, Six-pointed; Mixed draws one per map) chooses the
  trace, whose curtain radius is 0.62 to 0.68 of the tip (a slender 0.56 trace read best but left a
  courtyard too small to grow a town in). Outwards from the trace: a wall 2.2 tiles thick, a berm
  of 2.5, three water corners of moat, up to five corners of grass bank, two paved corners of
  covered way, `glacis-width` (4-14, 10) tiles of scree glacis and one corner of scree foot.
  `gates` (1-4, 4) open in the curtains facing front, back and the flanks, each a three-tile gap,
  a five-corner causeway and a three-corner track to the foot. `starting-towers` (0-3, 0) raises
  two towers per colony in the front bastions (the bastions flanking the front), scored by the
  fort's own approaches (`chooseTowerSites`, `settleStartingTowers` with the gates as goals).
- **The gardens.** Every courtyard tile 1.5 tiles behind the fort's middle is garden, closed off
  from the town in front by one row of paved corners and split by a paved lane as wide as the back
  gate. The left half is wheat with a cistern towards its back; the right half is wheat with a
  second cistern inside an arc of clay at 0.98 of the courtyard's radius and wood beyond it, in its
  bastion (forts under 26 have no arc: the right half is all wood). Where a line meets the wall at a slant, any
  diagonal of grass it could not close becomes wall. Wheat is planted over 75% of each wheat
  garden, nearest the swarm first (a solid band from the garden line back), and wood over 45% of
  the wood garden, scaled by the amounts, never below 40 wheat and 20 wood. Renewable water exposure and harvest frontage support the food-heavy layout.
  Cisterns stay at the back so they do not interrupt the contiguous wheat rectangle
  nearest the swarm: Numbi's `estimateFood` scans that rectangle rather than the
  entire food inventory, making patch continuity and orientation relevant.
- **Moat banks.** Where the original layout leaves room, an outer grass bank follows the moat,
  with wheat and wood in clumps across 65% of its plantable ground at default amounts. The
  same noise pattern repeats in each fort's frame, giving every colony the same frontage.
  Gate causeways are paved through the bank; the covered way outside it keeps crops off the
  glacis. The bank uses only spare space after normal fort/glacis sizing:
  it narrows from five corners when needed and is omitted below three. Existing courtyards
  and the minimum country gap are preserved. Wheat and wood amounts scale these crops; zero
  disables the corresponding moat crop without removing the ordinary starter garden floors.
- **The country.** Every tile belongs to its nearest fort; along every boundary of more than 24
  tiles between two forts' country runs a stream, the boundary walk displaced by two sine waves of
  wavelength and phase drawn per stream and tapered to nothing at its ends, three corners wide.
  Its middle is a 5x5 boardwalk ford, and a road runs from it to the nearest gate of each fort it lies
  between (`cheapestWalk` over the uplands noise, sharing roads, avoiding water and the ground
  within eight tiles of a glacis foot). A pond per 1,800 country tiles, grown irregular round a
  noise key, waters fields out in the country. Forest covers 30% of the country (scaled by wood),
  densest against the glacis foot; `furnishGround` lays fields, outcrops and groves; every ford
  has three fruit groves on both banks and, a little further out, a rock outcrop with a quarry face
  of 8 stone (scaled by the stone amount). Extra wheat and wood clumps cover 30%
  of the remaining fertile ground within seven tiles of actual streams, split between the two
  crops and scaled by their amounts. These patches keep off roads, forts and existing prizes;
  moats and country ponds do not count as streams.
- **Negotiation.** When the lattice is too tight for two forts' zones and 16 tiles of country the
  glacis narrows by two down to 4, then the fort by two down to 20, then the map is refused;
  garden floors scale with the fort (never below 0.3 of the default's). Supported: 256x256 up to
  6 colonies, 512x512 up to 12, 128x128 up to 2, rectangles in proportion.
- **Checked, not assumed.** Every vertex as `sketchTerrain` designed it; no way into a fort but its
  gates, walking or swimming, with the gates shut (`leakWithShut`); nothing planted on a glacis and
  none of it ground a crop could grow on; the gardens sealed from the town and moat banks sealed from other grass (floods of
  the finished grass); both crops present on every planted moat bank when their amounts are nonzero; every colony with the same number of towers; every colony walkable from the
  first; wheat within 24 and wood within 32 of every colony (`startingAccessFailure`).

## Implementation source

[GlacisGenerator.cpp](../../src/map/generator/generators/GlacisGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
