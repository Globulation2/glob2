# Symmetric arena

An orchard island behind a moat at the exact centre, crossed by sand causeways, giving 2, 4 or 8
colonies identical starting ground.

- **Symmetry.** A half turn for 2 colonies; a quarter turn for 4 on square maps and both mirrors
  for 4 on rectangular ones; all eight square symmetries for 8, on square maps only
  (`pointSymmetry`, `shared/Orbits`).
  `validateRequest` rejects other colony counts and layouts that leave too little room.
- **Built, not copied.** Every decision is either a function of a tile's whole orbit (integer
  noise summed over the orbit, the integer squared radius) or is made once for colony 0 (home,
  pond, starting kit, shortest route to its causeways, swarm and workers) and stamped onto every
  image, with the swarm's top-left anchor recomputed per image. Terrain is written with
  the beach rule applied to every vertex at once, so it does not depend on scan order, and resource amounts drawn from the engine RNG are equalised across each orbit.
- **Centre.** `centre-size` sets the island's radius and `moat-width` the water around it;
  `causeways` and `causeway-width` choose one gate straight towards each colony or two flanking it.
  With `moat` off (on by default) the island joins the land around it, with no causeways; the
  moat's width still spaces the homes. The orchard deals two-by-two fruit groves out an orbit at
  a time, turns some orbits to stone (`orchard-stone`, on by default) and always keeps all three
  fruits; the fruit amount keeps that share of the fruit orbits nearest the centre, never fewer
  than three. It is the map's only fruit.
- **Outside the ring.** Symmetric lakes (`lakes`), shore farmland, stone outcrops and algae scaled
  by `richness` and each one's own amount. Each home gets a pond and a fixed kit, and each
  colony's shortest route to its causeways is kept as clear land. `scatterResources` is not used.
- **Checked, not assumed.** `validateWorld` requires corner, terrain, deposit, building and unit
  invariance under every symmetry, with a consistent colony permutation that reaches every colony,
  and equal walking distances from every colony to wheat, wood, each fruit and the orchard. Only
  the starting state is symmetric: in-game growth uses the synced RNG. `scoreStarts` counts
  building sites by top-left anchor, so its fairness reads just under 1 on this map.

## Implementation source

[SymmetricArenaGenerator.cpp](../../src/map/generator/generators/SymmetricArenaGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
