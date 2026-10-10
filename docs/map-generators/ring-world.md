# Ring world

One continental belt wraps the map along its longer axis (horizontally on square and wide maps),
and the seas either side meet across the other wrap: there are no corners, and every colony has
exactly two land neighbours.

- **Belt.** The centre line and width are whole-number harmonics of the map length and the coast
  is lattice noise whose cells tile the torus, so the seam can't be seen. A dry spine beside the
  centre line and one shared coast budget keep the belt a single landmass and the ocean a band
  whatever `belt-width` (a percentage of the map's breadth) and `coast-roughness` ask for. With
  `winding-belt` off (on by default) the centre line runs straight round the map. Terrain
  is stamped with the order-independent beach pass (`layBeaches`).
- **Lakes and islands.** `lake-density` adds inland lakes that keep land between them and the
  ocean and stay off the spine; `resource-islands` (per 128×128) adds themed islands out at sea.
- **Colonies and resources.** Evenly spaced, jittered slots alternate coasts (`both-coasts`, on
  by default; off, every colony takes the same coast) and sit on the
  spine-connected belt at one fixed distance from water, with identical starter patches; then
  `scatterResources`, a shallows algae pass and `guaranteeStartingResources`. A final road pass
  clears only the deposits on the cheapest walk from each colony to the next, closing the loop.
  `validateRequest` needs at least 24 tiles of belt length per colony.
- **Checked, not assumed.** `validateWorld` floods from colony 0 while counting seam crossings,
  with water, buildings and every resource blocking, and requires every colony reached, a walkable
  loop around the map, and a body of water that wraps beside the belt.

## Implementation source

[RingWorldGenerator.cpp](../../src/map/generator/generators/RingWorldGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
