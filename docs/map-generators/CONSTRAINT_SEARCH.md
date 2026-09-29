# Search as a map-generation stage

`shared/Solve.h` provides bounded simulated annealing, named objectives, per-seed briefs
and telemetry. It composes with the ordinary [map toolkit](MAP_GENERATOR_FRAMEWORK.md):
construct terrain and homes, measure candidate decisions, search the remaining choices,
then validate the finished world. It is heuristic optimization, not a general constraint
solver or a certificate of balanced gameplay.

Use search for a small set of coupled decisions that are cheap to re-score. Marchland
constructs its country with `Territories`, `Homes` and `Growth`, computes walking costs
once with `Contact`, then searches prize placements. Even Ground searches a coarse terrain
lattice and crop allocations; its uneven gameplay illustrates that improving a proxy
objective does not establish fair openings. See their generator files for complete callers.

## Composition and feasibility

| Responsibility | Existing tools | Search's role |
| --- | --- | --- |
| Geometry and wrap | `Grid`, `Geometry`, `Drawing`, `Sketch` | Work in the same toroidal coordinates and masks |
| Homes and room | `Territories`, `Homes`, `Room` | Reserve required space before proposing changes |
| Routes and access | `Morphology`, `Contact`, `Channels` | Precompute unchanged costs; invalidate/recompute costs when terrain changes |
| Rivers and crossings | `Rivers` | Construct a river, score placements, select connections |
| Preferences | `Objective`, `Brief` | Rank feasible alternatives with named, measurable terms |
| Finished-world checks | `Pipeline`, generator `validateWorld` | Check actual settled, furnished terrain independently of search cost |

Hard requirements must not disappear into a weighted sum. Construct them, preserve them in
proposals, or reject invalid candidates explicitly. A finite penalty can always be traded
against another term. Optional `Brief` emphases belong to character (clustering, shoreline,
water-body count), not required access or colony survival. Zero search effort should leave
a defined baseline; document any weaker validation policy the generator deliberately offers.

The final validator remains necessary: beaches, settlements, resource planting and repairs
can change the geometry after optimization. A coarse lattice or precomputed cost field is
not the finished game's walkability map.

## A local search state

Prefer a small generator-local state type over a set of lambdas sharing mutable caches.
The driver needs these operations; it imposes no base class or allocation scheme:

```cpp
struct PlacementSearch
{
    // Arrangement, cached measurements, current move and best snapshot live here.
    bool propose();   // Apply one legal move; false leaves scored state unchanged.
    double cost();    // Finite cost of current arrangement; no RNG draws.
    void undo();      // Restore the prior arrangement AND incremental caches.
    void remember();  // Snapshot initial/new best arrangement.
    void recall();    // Restore best arrangement AND synchronize caches.
};

PlacementSearch state = /* generator-specific inputs */;
const Anneal schedule{moves, initialHeat, finalHeat, "my-map-placement"};
const SolveReport run = anneal(schedule, context, state);
// Recompute final diagnostics from the restored arrangement, then report them.
reportSolve(context.telemetry, "my-map.placement", run);
```

`anneal` calls `remember` initially, including for a zero-move run. It remembers strict
improvements and recalls the best state if the accepted walk finishes worse. A rejected
proposal can leave scratch diagnostics describing the rejected candidate; refresh those
before reporting. The driver does not insert extra scoring calls that could change a
caller's execution cost or floating-point accumulation.

The callback overload remains available for small, uncached searches. Both interfaces have
the same draw order. An exception aborts the search; the driver does not attempt to roll
back a partially applied throwing callback. Treat such state as unusable unless the caller
provides its own exception guarantee.

Incremental updates need a separate full-rescan oracle. Compare them over accepted and
rejected moves, adjacent and wrapped swaps, and best-state restoration. For floating-point
sums, compare with an appropriate tolerance: applying an inverse arithmetic delta is not
necessarily a bit-exact undo. Preserve the established operation order during refactors.

## Objectives and schedules

```cpp
Objective score;
score.add("access-spread", accessWeight, accessSpread)
     .add("field-clustering", fieldWeight, clusteringError);
const double cost = score.total();
reportObjective(context.telemetry, "my-map.placement", score);
```

Terms retain their raw residual and weighted contribution; the same expression supplies the
search and its telemetry. The fixed capacity is eight terms. Overflow, duplicate/empty
names, non-finite terms and non-finite totals throw. Names are borrowed `string_view`s:
use literals, or storage that outlives every copied objective. Negative weights are allowed
for explicitly named rewards; they are not hard constraints.

`residual(name)` is a required lookup and throws on a missing term. Use
`findResidual(name)` for an optional term. Do not silently substitute zero for a miss,
especially when a residual controls whether a feature is accepted.

Schedules require nonnegative move counts, positive finite endpoint temperatures and a
nonempty stream name. Cooling advances on every attempt, even when no candidate can be
formed. `SolveReport` distinguishes attempted draws, proposed candidates, accepted moves
and new best states. Telemetry includes `attempted`, `skipped`, `proposed`, `taken`, `kept`,
`cost-before` and `cost-after`; a high skipped count can reveal an over-restrictive move set.

Named streams, iteration order, tie-breaking and floating-point operation order are part of
map identity. Do not reseed or rename streams as cosmetic cleanup. Finite scores and bounded
integer random draws do not guarantee cross-platform identity of `exp`, `pow` or the scores.

## Rivers are construction plus selection

`drawRiver` constructs a periodic centre line; `riverWater` rasterizes its closed seam.
`bestRiverAcross` compares candidate placements using the caller's `Objective`. Check
`RiverChoice::found` before reading the winning score. Validate hard placement limits
separately, as Marchland does for town overlap and commons share.

`fordSites` reports bank component pairs. `fordsToRejoin` returns a `FordConnections`:
selected site indices plus the number of remaining components. `connected()` reports
whether the available candidate graph was connected. The chosen edges are a spanning
forest, not a promise that all banks could be rejoined. `fordsSpreadAlong` adds optional
crossings; `layFord` rasterizes them after beaches. Check actual finished-world routes
because the candidate graph alone cannot prove rasterized crossings are usable.

Marchland retains its existing behavior when the candidate graph is incomplete: record
unjoined components, draw available crossings, and let finished-world validation decide
whether generation must refuse. It does not silently accept disconnected colonies.

## Study the mechanism and the outcome

`tools/map_generation_study.py` is the shared local-study adapter for the native
`--headless-catalog` and `--generate-map` JSON interfaces. It translates tile dimensions to
exponents, isolates temporary profiles, applies timeouts, checks return codes and distinguishes
`completed`, `refused` and `execution_error`. Infrastructure errors are not map refusals.
The native result retains its more specific status and diagnostic.

The skill's `control_study.py`, `refusal_sweep.py` and `final_map_stats.py` use this adapter.
Control-study schema 2 keeps its existing mean metrics (`tel:<key>`) and adds
`tel-min:<key>` / `tel-max:<key>`. It retains raw telemetry records, including subjects,
choices and fallbacks, so an average cannot hide an unserved colony. A control study writes
binary/catalog metadata and flushes completed rows. Refusal and final-statistics studies
print summaries; refusal rates exclude execution errors and show them separately. Historical schema-1 rows remain
reportable, but cannot recover subject detail that was never recorded.

Tournament verification selects games round-robin across generators, then maps, then
rotations. Request at least as many repeats as generators to cover each one. Final-state
repeat checks still do not establish per-tick or cross-platform simulation equivalence.

For behavior-preserving changes, keep existing golden fingerprints and compare additional
paired seeds/settings with the previous frozen binary. Test the shared contracts separately:
invalid objectives/schedules, skip/reject/restore paths, incomplete crossing graphs, and
structured-tool failures. Re-run gameplay studies when layouts or objectives change; do not
reinterpret unchanged gameplay evidence as proof that a refactor fixed balance.
