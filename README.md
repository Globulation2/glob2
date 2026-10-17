# PR 1196: why ordinary-terrain saves still have overhead

Source `eddff43fbb57f772f570a9a23741ece5a6b4ecc5`; recorded and freshly fetched master `486a2230a4ddb257ba9dee34d3aace05f2260d6f`. No production source or PR code changed during this investigation.

## Main finding: ordinary terrain does not imply direct-flight eligibility

The hybrid checks `Map::hasAirRoutingConstraints(teamMask)` in `src/map/MapCells.cpp`. It returns true for **any air terrain constraint, any air-blocking resource anywhere on the map, or any forbidden paint anywhere for that team**. Thus a forbidden patch near one colony disables direct steering for that team's flyers throughout the map.

The midgame, islands and swamp saves have no air terrain modifiers, but their relevant teams have forbidden paint. Sampled air queries report zero air-blocking resources, and hundreds of forbidden cells in islands (e.g. 266–351 in the initial query samples). Nicowar deliberately creates forbidden farming protection in `src/ai/nicowar/Farming.cpp`; this is normal AI map state, not just a terrain-design edge case.

Diagnostic counters inserted only into a separate copy of MapCells.cpp show:

| Input | Constraint=true calls | Direct-eligible calls | True because of terrain | Summary rescans |
|---|---:|---:|---:|---:|
| islands | 270 | 0 | 0 | 18 |
| midgame24k | 151 | 0 | 0 | 13 |
| swamp | 105 | 0 | 0 | 10 |
| ordinary32 | 0 | 2624 | 0 | 1 |
| lava-shield | 2 | 0 | 2 | 0 |
| constrained64 | 1641 | 0 | 1641 | 0 |

These are calls to the eligibility function, **not counts of units or every type of flight**, over the whole 384-tick run (128 warmup plus 256 measured ticks). The diagnostic changes no decisions; all six initial/final checksums and tick counts match the unmodified optimized binaries. In the three broader ordinary-terrain saves every observed eligibility call rejects the direct path. The synthetic ordinary32 save does take it. This explains why switching from field-only to hybrid barely changed the broader saves.

The implementation gate is broader than the original terrain-only suggestion. It conservatively preserves forbidden-area and air-resource avoidance, but it also suppresses the desired ordinary-flight behavior in these common AI saves. A local/path-relevant fallback would be a materially different and potentially better policy; this investigation has not silently relaxed forbidden-area behavior.

## Where the extra work goes

Hardware perf counters are unavailable (`perf_event_paranoid=4`); no system settings were changed. Callgrind was used instead, with matching optimized production objects and an instrumented copy of Headless.cpp for each source. Collection starts after the same 128 warmup ticks and stops after final gradient/growth/hiring drains, matching the native CPU window. Four compute participants, GPU off, compute telemetry off. These are instruction counts, not CPU milliseconds.

All four profiled runs match their unmodified native initial/final checksums and tick caps. Every thread's exclusive instruction sum equals its footer, and all thread footers sum exactly to Callgrind's global Collected counter. Both the owner and three workers are included.

| Input | Extra total instructions vs master | Scheduled building fields: share of extra | Food selection: share | Material seed cache: share | Eligibility check: share |
|---|---:|---:|---:|---:|---:|
| islands | 8.13% | 48.9% | 21.2% | 13.6% | 3.2% |
| midgame24k | 4.08% | 63.0% | 3.6% | 15.5% | 3.2% |

These four entry-edge groups do not overlap in these captured call paths. Food selection includes its own synchronous field preparation/search, so the synchronous seed functions are not added again. Remaining extra work includes other building searches, clear-area seeding, propagation and small plumbing differences. Instruction proportions explain work, not the exact CPU-time proportions: the native comparison measured +5.7% CPU on islands and +4.2% on midgame, while instructions rise 8.13% and 4.08% respectively.

On islands, `buildScheduled → buildBuilding` grows from 279.14M to 391.86M instructions; `Unit::handleActivity → Team::findNearestFood` grows from 1.61M to 50.55M. The cumulative 384-tick counters show 226 scheduled building jobs versus 206 on master, and 26 synchronous builds versus 8. Midgame has 374 versus 355 jobs and 51 versus 37 synchronous builds. These counters include warmup; the instruction profiles exclude it.

Master's ordinary-air service selection uses geometric distance. The PR routes flying food/healing/upgrade queries through `buildingAvailable(..., AIR_MOVEMENT_CLASS, Footprint)`, which can allocate, seed and partially propagate a building field for each candidate. Warm fields then continue scheduled refreshes. Direct steering alone does not remove destination-query fields, and the painted-map gate also leaves field following enabled here.

The material-seeding path also costs more (islands +31.32M instructions; midgame +10.28M). That is a secondary target in the shared code. The larger air-aware cache and runtime air branch are relevant source differences; this profile does not isolate their respective contributions from the changed request mix, so it does not claim that all of this increment is unused-air cache maintenance.

The eligibility check itself is only about 0.24% of the hybrid's total instructions on islands and 0.13% on midgame. Its rescans are worth avoiding where possible, but optimizing that boolean test cannot recover most of the regression.

## What to improve next

1. Refine the all-map/per-team fallback. On flat air terrain, distant forbidden paint should not automatically force unrelated flights onto fields. A cheap certificate for a legal direct segment, followed by field fallback when relevant obstacles require it, is a candidate design. Keep deterministic traffic handling and escape behavior, and avoid mode-switch oscillation. This is a proposal, not a validated implementation.
2. Avoid materializing destination fields unnecessarily: use safe distance lower bounds to prune service candidates; use analytic octile distance to a footprint when the relevant region is provably open. Preserve service priority, tie order and starvation rules. Simply using Euclidean distance in painted maps would change which destination is chosen.
3. Profile and specialize the material-seeding hot loop after the first two changes; retain the ground fast path and lazy air data.

A measurement-only analytic footprint prototype was built. It never qualified in the representative saves because of forbidden paint; the synthetic courier save had no footprint queries in this interval. Its validation counter was zero in all six benchmark inputs, so it establishes **no correctness coverage or measured saving for that prototype**. It was not pushed to the PR. The failed hypothesis led to the eligibility diagnostics above.

## Evidence and limits

Exact compiler/link command arrays, source-copy instrumentation diffs, binary hashes, four raw all-thread Callgrind runs and reconciled analyses, six native-equivalent eligibility runs, query diagnostics and prototype source are retained. The original fixtures/hashes and ten-run native timing samples are in the earlier performance evidence linked from the PR. No source build, tests or compatibility qualification beyond the instrumented/native checksum checks is newly claimed; production code is unchanged. No GPU, other-platform, combat, long-match or human feel qualification is added.

Setup ledger: perf access was rejected, so Callgrind was used. The master compile command was in the first-stage build log rather than the resumed log. Reusing a completed game output directory was rejected; the final diagnostic pass uses separate output directories. The analytic prototype's first successful-query counter stayed zero; added query/gate diagnostics explained why rather than treating that as successful analytic validation.

Correction to the prior play-evidence description: the custom ablation-5/6 catalog has FLY=28 at level 0 (ability index 5). The visual fixture overrides it to 32. The earlier evidence README incorrectly said 32 versus 8. The native timing fixture retains its authored FLY=28; no recorded timings, traces or captures changed. This wording correction does not establish default-unit balance coverage.
