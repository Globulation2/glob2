# Allotments

Villages among allotment gardens. The map is parcelled by sand lanes into warped quadrilaterals
(`squareTessellation`, `warpCorners`), most of them garden sites - rows of narrow plots side by
side, each tended differently, with a ditch across the rows - and the rest commons (meadow round a
pond) and woodlots. Every colony's village is an open green behind its own block of plots.

- **The village.** One stencil stamped at every lattice site, turned by one quarter turn drawn per
  map: a square of
  half-size 24 ringed by a lane, its front 29 tiles an open green (about a thousand 4x4 build
  sites) and its back a block of plots nine tiles wide either side of a ditch four corners wide,
  planted in a fixed pattern (wheat, wheat, wood, wheat, wheat, fruit, wheat, wood, wheat) over
  75% of each plot, unscaled. Village plots are wide whatever `strip-width` says because Numbi
  estimates food from a contiguous wheat block by its swarm; narrow strips can constrain
  its opening even when total food is abundant. A lane round the village's margin closes off the
  parcels it cuts into.
- **The parcels.** `site-size` (28-48, 36) is the tiling's pitch. Each parcel draws its kind
  (commons by `commons`: Few 12%, Some 25%, Many 40%; woodlots 15%; the rest garden sites; a parcel
  with under 150 tiles of interior is always a commons), and a garden site draws its orientation,
  its plot width (a tile either side of `strip-width`) and its plot length (5-8): bands of two rows
  of plots either side of a four-corner ditch, a sand path between every two plots.
- **The plots are what the terrain makes of the pattern.** Every eight-connected patch of pure
  grass in a garden site, once the ditches' beaches are laid, is one plot with a style drawn by
  `plot-mix` (Tended 58/14/8, Mixed 46/24/7, Overgrown 30/44/5 percent wheat/wood/fruit, the rest
  bare); patches under six tiles are bare. Deriving plots from the pattern's arithmetic instead
  missed the patches a warped lane cuts short, which then joined two plots. Wheat and wood plots
  are planted over 75% of their ground, fruit plots with six bushes, and a bare plot has a shed (a
  stone) at one end in 45% of cases. Woodlots are wooded over 60% of their noisiest ground;
  commons get a pond of radius 2.5-4.5 and a fruit grove or a small quarry.
- **Checked, not assumed.** A flood of every plot's pure grass at once, labelled by plot, never
  reaches another plot or grass outside the plots; every colony walkable from the first; wheat
  within 24 and wood within 32 of every colony.

## Implementation source

[AllotmentsGenerator.cpp](../../src/map/generator/generators/AllotmentsGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
