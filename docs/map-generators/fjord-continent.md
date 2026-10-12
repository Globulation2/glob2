# Fjord continent

The richest generator, and the one most of this framework's resource work was proven against:

- **Shape.** A `ShapeTransform`-warped `RadialShape` coastline; an untouched core disc
  (`coreR`, currently 0.23x the continent radius) that every fjord stops short of, so it always
  stays connected land regardless of coastline roughness. A fjord is carved between every pair of
  angularly-neighboring teams as a smooth S-curve centerline from just outside the coast in to
  `coreR` (or, in lake-connected mode, well inside the lake — see below). On a rectangular map
  the continent is stretched along the longer side by `Stretch` on top of its own transform, so it
  fills the map as an oval; square maps are unchanged.
- **Central lake.** `lake-size` (0–90%, of `coreR`; 0 disables it) carves a lake at the exact
  center, ringed by a sandy no-man's-land wider than an ordinary beach
  (`sandy-lake-shore`, on by default; off, the lake has an ordinary beach).
  `lake-connected` (a switch, default off) decides whether the fjords actually cut through into the lake —
  every peninsula then water-isolated from its neighbors, boats required — or stop short behind a
  solid land ring, keeping mutual land connectivity; the latter is verified with an explicit
  flood-fill after construction rather than assumed, and only runs in that mode, since a
  lake-connected map is *supposed* to fail a same-landmass check by design. A fjord's carved tip
  is widened specifically in connected mode, since the beach pass turns any water vertex with a
  grass neighbor in its own 3x3 neighborhood into sand and a narrow tip is entirely coastal by that rule.
- **Core resources.** The ring around the lake carries several stone clumps and a grove of every
  fruit type — a genuinely rich destination, not a single token deposit. The lake itself gets a
  center-anchored algae clump plus bonus clumps drawn from well inside its shoreline, so a
  deposit never reads as merely stuck to one edge.
- **Outlier islands.** `resource-islands` (0–20) places small, unconnected islands out in the
  open sea, each themed to one resource. Each candidate is checked directly against the
  coastline's `radiusAt()` at its own position rather than against one global worst-case bound,
  so an island can land close to a narrow stretch of coast even while the coastline bulges out
  far away in some other direction.
- **Fjord banks.** Every fjord guarantees one corn and one wood clump per side, placed last so
  nothing else can overwrite the guarantee, plus six lighter best-effort clumps per side mixing
  corn/wood/stone along the same bank (`bank-deposits`, on by default). Each amount places its
  rolled clumps that many hundredths of a time, drawing any chance from a stream of its own.
- **Ambient layer and backstop.** `scatterResources` fills the continent interior at the end
  (corn:wood 2:1, fruit, stone; algae left to the dedicated shoreline/lake passes above), and
  `guaranteeStartingResources` runs last as the reachability backstop described above.

## Implementation source

[FjordContinentGenerator.cpp](../../src/map/generator/generators/FjordContinentGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
