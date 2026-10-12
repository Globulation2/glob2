# Fingerprint

An organic labyrinth: a Turing pattern grown over the whole torus (`turingPattern`, `Patterns`)
draws long curving bands that fork, merge and dead-end the way a fingerprint's ridges do, and the bands
become the barriers.

- **Pattern.** `wavelength` (12-48, 30; the first play found 20 read like Everglades) is the
  crest-to-crest width, so about half of it is corridor;
  `grain` (0-100, 0) stretches the blur along one axis so the bands run rather than wander. `pattern`
  chooses the barrier's share of the map: Labyrinth 42% (bands and corridors read alike once a water
  band's beach has taken a tile of each side), Islands 60%, Channels 28%.
- **Barrier.** Water, so every corridor is beside water and all the land is farmland (a share of the
  fertile ground under crops in patches); or Stone, permanent walls with pools in the corridors, the
  only fertile ground. The pools lie at the bottoms of the field's troughs: every local minimum with
  nothing lower within two wavelengths (`windowMinimum`), grown to 40 tiles along its trough; a
  percentile of the field instead gave a hundred tiny pools whose beaches cut the stone into blobs.
- **Homes.** Clearings on a lattice (`latticeSites`), each a rough disc of `home-size` (10-24, 18)
  with a pond (`stampRoundHomes`) and a margin of three tiles of pattern cleared beyond it, shrunk so
  a band's width of pattern always runs between neighbours. Fairness is statistical, like Everglades'.
  The ambient layer is 22% of the fertile ground under wheat and 12% under wood, an outcrop per 600
  tiles and a grove per 1000 (the first play found half that "very empty").
- **Routes.** The pattern owes nobody a way through: `openColonyRoutes` opens the cheapest way from the
  first colony to any it cannot walk to, a sand ford across water or a gap cut in the stone.

## Implementation source

[FingerprintGenerator.cpp](../../src/map/generator/generators/FingerprintGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
