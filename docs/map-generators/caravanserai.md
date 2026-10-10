# Caravanserai

Oasis towns on a desert trade route. Sand dunes in long bands with scrub in the hollows, rock mesas,
and scattered oases, each an irregular pool ringed with green, palms and grain; every colony's
home is a large oasis town round a lake with a ring of fields and a palm grove; half way between
neighbours stands a caravanserai, a square walled courtyard with a well, in an oasis of its own
with orchards and a quarry.

- **The home oasis.** One stencil stamped at every lattice site, turned by one quarter turn drawn
  per map: an outline of
  `oasis-size` (18-30, 24) radius and roughness 0.2, a lake of 0.3 of it set back 0.42, a ring of
  fields six tiles wide round the lake's shore closed off from the town by a sand path, and a palm
  grove in the back of the ring between two sand spokes. Wheat on 55% of the ring (never below
  60) nearest the town's swarm, a solid arc, and palms on 60% of the grove (never below 30)
  nearest the water, scaled by the amounts. The swarm stands on the town tile farthest from any
  other kind of ground. This preserves open swarm surroundings for Numbi's
  initial placement search rather than placing the swarm against the field path.
- **Caravanserais.** One at the midpoint of every pair among each colony's `caravanserais`
  (1-3, 2) nearest neighbours (`nearestPairs`, `midpointAcross`), wherever its oasis of radius 14
  keeps clear of the homes and the other caravanserais: a wall at Chebyshev 6 with three-tile gates
  at both ends of the route, a well of 2x2 corners, a pond of radius 2.8 eleven tiles to one side,
  three fruit groves and a quarry on the other, grain and palms by the pond. Homes shrink by two
  down to 16 to make room, then the caravanserais are left out.
- **Oases.** Route oases every 32 tiles along each colony's straight way to each caravanserai it
  shares, then scattered oases one per 3,200, 1,800 or 1,100 desert tiles by `oases` (Sparse,
  Normal, Many); radii 4 to 10, roughness 0.3, stretched up to 1.8 along a drawn heading, a pond at
  a third of the radius off centre, palms and grain in proportion to the radius.
- **The desert.** The caravan routes are the cheapest walks from every home to its caravanserais
  and are kept clear. Mesas are the top 0, 5 or 11 percent of the map by a relief noise, by
  `desert` (Open erg, Dunes and mesas, Canyon country), kept six tiles from every oasis, home and
  route, with stone on every tile whose four corners they raised. Dunes are stripes wrapping the
  torus (`stripePhase`) about 12 tiles apart; single grass corners of scrub fill 16% of the hollows
  and 30% of a three-tile halo round every oasis, never two side by side.
- **Checked, not assumed.** Every caravanserai wall standing, no way into a courtyard but its
  gates; the field ring and the palm grove sealed from the town and from each other; every colony
  walkable from the first; every colony's walk to its nearest caravanserai gate within 32 steps of
  every other's (a lattice that is not exact leaves neighbours at unequal distances); wheat within
  24 and wood within 32 of every colony.

## Implementation source

[CaravanseraiGenerator.cpp](../../src/map/generator/generators/CaravanseraiGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
