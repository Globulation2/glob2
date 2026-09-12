# Cortex placement performance

Each placement search snapshots live building positions, upgrade reservations,
and existing inn occupancy. Candidate checks reuse that geometry and compare
four wrapped clearance strips. The snapshot expires at the end of the search.
Scan order, scoring, RNG calls, and observation cadence remain unchanged.

On Apple M3, optimized integration revision c71373bb, an eight-team Playground
match (seed 20260907; Numbi, Castor, Warrush, ReachToInfinity, Nicowar, Maxima,
Cortex, Maxima) took 119.77 seconds before and 84.19 seconds with Cortex alone.
Retired instructions fell from 2.098 trillion to 1.526 trillion. The full replay,
initial save, and final autosave were byte-identical. These are single-run,
workload-specific observations; build activity overlapped some elapsed timings.
The unpublished integration commits are not part of this change.

The standalone Cortex patch on master also produces the original complete
SmallForTwo replay for seed 42 (Cortex/Nicowar): SHA256
524317223299ed21c1e7c9d028b5f5ac7713c1fb77799eceff646ccc07996741.
This split verification is a correctness check, not an isolated timing claim.

Build `scons release=1 cortex-geometry-test`, then run
`./build/src/CortexGeometryHarness`: 57,600 candidates are compared with the
original tile-scan helpers, including wrapped corners, upgrade reservations,
construction sites, map-only occupants, dead buildings, and empty colonies.
Linux CI runs the harness. This PR contains no save-buffering changes.
