# Verify recursive maps

Gardens and Hilbert layouts need both integer-geometry contracts and populated-game checks.
Their design and reusable interfaces are in [recursive layouts](fractal-maps.md).

## Geometry and compatibility

Run the `MapGeneratorDefaults` and `CustomGameSetup` engine suites and the platform's
`MapGeneratorGoldenTest` with required golden rows. Cover exact subdivision,
rectangular Hilbert paths/orientations, bounded failures, crossing connectivity,
torus seams, contained crop plots, and disjoint building footprints with access.
Check smallest and largest supported rectangles and dense colony requests.
Record refusals separately from accepted requests that fail generation.

## Economy and routes

Use complete seat rotations, fixed map/game seeds and a comparable reference map.
Inspect opening meals, starvation, peak population, inn upgrade downtime, renewable
food/wood, building room and eventual contact. A capped game is separate from an
engine victory. Stalls on a reference map can indicate an AI limitation; that does
not clear the generated map of all defects.

The historical implementation studies did not complete every planned all-AI,
held-out and four-colony cohort. Their unfinished jobs cannot establish acceptance.
Static fairness and individual AI results do not certify balance or human fun.

## Record a new check

Use [generator verification](verification.md) and the [tournament workflow](../tools/tournaments.md).
Retain executable/data identity, request settings, platform/toolchain, maps, reports,
saves and replays with the PR evidence. Keep new run logs and analyses in ignored
`artifacts/`; do not append a delivery diary to the maintained design guide.

Related: [map generators](README.md).
