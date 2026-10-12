# Everglades

A dense wetland, the same everywhere and across the wrap. Pools and long sloughs lie on a jittered
lattice that tiles the torus, bigger where the land is wet and smaller where it is dry, and the
grass between them starts thick with wood and wheat. Every tile is a few steps from water, so wood
and wheat grow back at the engine's top rate; resources block movement, so the swamp grows shut
unless workers keep cutting it. Nothing grows on sand, and every pool's beach is sand, so the banks
between pools are the lanes that stay open, winding and narrow. Every colony starts in a clearing
with a pond and a kit, ringed by a sand levee with gaps the swamp creeps through; what lies beyond
is held only while it is kept cut. Nothing is symmetric on purpose: the landscape is one random
field over the whole map and the clearings are nudged off their ring, so it reads as country, not
as an arena. Fairness is statistical; the lobby keeps the best-scoring of several seeds.

- **Layout.** One pool per cell of a `pool-spacing` lattice that divides the map's width and
  height exactly, so the field wraps without a seam; each pool is jittered within its cell, its
  radius `pool-size` scaled by a smooth wetness field (0.6 at the driest ground to 1.5 at the
  wettest) and by a random factor, and `sloughs` percent of them are grown 1.4 times and
  stretched 2.2 times along a random line. The engine wraps every pool in a two-tile beach, so
  at the defaults (spacing 15, size 4) a 256 map is about 30% water, 34% beach and 36% grass;
  that sits at the threshold where the beaches stop linking into one network, so some lanes
  between neighbours stay open for ever and others close as the grass grows over. Closer or
  bigger pools link every beach into a permanent maze; sparser or smaller ones close the map. Clearings of `clearing-size` radius (rough outlines)
  sit near a ring at 58% of the half side, evenly spaced from a random start and each nudged by
  up to 12% of a wedge and 8% of the radius, with a pond at the middle, a `kLeveeWidth` band of
  sand round the outline of which `levee` percent stands (36 sectors, chosen at random), and no
  pool water for two tiles beyond it so the gaps open onto grass. The generator bends rather than
  refuses: a clearing that would not fit between its neighbours or inside the wrap at its fullest
  reach shrinks until it does (down to a radius of 6; the pond shrinks with it and a clearing
  under 9 has none, the swarm taking the middle), then the grass beyond the levee and the fit
  margin go, then the ring widens to 76% of the half side; pools whose
  expected coverage (mean pool area over cell area, overlaps discounted) would pass 60% of the map
  are scaled down to that, and `validateRequest` refuses only when even the smallest clearings
  cannot fit.
- **Resources.** Each home has an unscaled kit of 40 wheat and 30 wood beside its pond and a
  quarry on the side towards the map's centre; the swarm stands just past the pond's beach on
  the far side with the kit kept four tiles clear of it, so there is room to build. Off the clearings the
  swamp starts with wood on 55% of the grass and wheat on 15%, in patches, scaled by the amounts,
  with stone outcrops (which never grow) and fruit groves. Algae seeds the pools.
- **Routes opened, not hoped for.** Last of all, `openRoutes` walks from colony 0 and, for any
  colony it cannot reach, opens the cheapest way through: deposits on it are cleared and water on
  it becomes a sand ford (one sand corner per tile), so a boxed-in start is repaired rather than
  failed.
- **Checked, not assumed.** `validateWorld` rebuilds the design and requires every clearing's pond
  present, no deposit on any levee tile, and every colony reachable on foot from colony 0, with
  water, buildings and every resource blocking.

## Implementation source

[EvergladesGenerator.cpp](../../src/map/generator/generators/EvergladesGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
