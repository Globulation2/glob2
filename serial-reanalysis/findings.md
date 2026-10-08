# Next optimization candidates

These profiles contain no additional game changes. The maintainer selected precomputed bitmask wrapping as the first follow-up experiment: production map dimensions are powers of two, while arbitrary food-ledger dimensions retain modulo. That separate candidate is being validated on `codex/food-ledger-wrap-masks`; it is not part of the merged revision profiled here. Telemetry ownership transfer and the tower query remain other small candidates. Keep each independently measurable and preserve all orders, deadlines, iteration order and serialized results.

| Priority | Candidate | Evidence and proposed experiment | Correctness boundary |
|---|---|---|---|
| 1 | Move completed AI telemetry into publication | Fresh native samples show memory copying on the owner. Supplemental GCC 15 Callgrind attributes 13.06% of sparse-owner instructions specifically to copies below `AI::publishDecision`. The worker has already placed telemetry in an owned `Command`; the owner copies it again, then clears or destroys that command. Test transferring the completed values instead. This is not a claim of 13% CPU savings. | Preserve owner-clock publication timestamps, observation-clock field timestamps, diagnostics, telemetry history and save/load. Confirm every command consumer before changing the internal signature. Never move from mutable worker telemetry. |
| 2 | Scan live enemy buildings in tower-range queries | `Unit::locationIsInEnemyGuardTowerRange` still scans every one of 1,024 building slots per enemy team. Native samples identify the routine; `hasClearShotTo` reads state without mutating entity membership. Use sorted live buildings with identical order and early returns. | Sparse and reused slots, alliances, zero-range buildings, wraparound, range boundaries and terrain occlusion. Keep authoritative integrity scans independent. |
| First experiment | Precompute power-of-two wrapping masks | Native worker samples spend substantial time in `Input::normalizeX/Y`; optimized GCC 13 disassembly contains a variable signed `idiv` in each. `Ledger::walk` normalizes coordinates for indexing and again for footprint exclusion. First replace modulo with a precomputed mask for positive power-of-two dimensions, retaining modulo for arbitrary or directly changed dimensions. This keeps traversal unchanged; broader search restructuring can wait. | Preserve BFS visitation/neighbor order, stopping at the same threshold-crossing cell, negative coordinates, seams, tiny and non-power-of-two dimensions. Keep a general modulo fallback unless caller bounds are proved. Do not globally replace modulo with a bitmask. |
| 4 | Reduce snapshot capture work by component | Snapshot capture is a recurring owner hotspot. Component sharing, pooled buffers and dirty-chunk copying already exist; a generic additional cache is not yet justified. First attribute bytes, entity-record construction and dirty-chunk churn per component using existing capture counters. | Immutable outstanding leases, eight-tick delays, generation/identity invalidation, reload and AI/render/growth consumers. |
| 5 | Reduce gradient and hiring work | Dense-owner profiles concentrate in building-gradient resolution plus hiring predicates; workers spend heavily on gradient propagation and food searches. Inspect repeated work and invariant inputs after identifying the specific request types. | Preserve expansion order, budgets, partial-result availability, equal-score ties, failure tallies and application deadlines. This is a larger, higher-risk project than the first two. |

## Current critical path

The headless owner gathers local/AI orders, opens an immutable read boundary, dispatches work and joins due AI decisions, publishes completed telemetry/orders, then executes orders and advances the simulation. Inside the simulation, gradient results are applied before ordered team updates; unit movement, building hiring and building actions remain owner work, followed by map work, statistics/events and checksum/telemetry consumers. Workers compute AI food/planning searches and prepared gradients against captured state. Join elapsed time is dependency waiting plus scheduling delay from the host, not owner computation.

Sparse/hiring cases expose fixed per-tick copy and bookkeeping costs. Dense cases shift toward gradient resolution and hiring. Established/combat cases have substantial worker food-ledger work and joins. Consequently, reducing worker CPU can shorten the owner critical path when a due result is awaited; it may have little immediate wall-time effect when it finishes ahead of its deadline.

## Where to be cautious

`Team::syncStep` contains mutation-bearing traversal. `Team::integrity` deliberately compares authoritative slots against live lists. Their profile cost is not justification for mechanically converting them to live-list iteration.

The sparse counter run recorded 11,453 context switches with four participants and 192,210 with 32; the owner kernel sample share rose from about 9.5% to 20.6%. This is evidence to investigate executor wakeups and contention, not an isolated thread-count recommendation.

Executor controls use one, four and 32 participants (32 is this machine's current automatic size). They are useful for detecting wakeup/locking cost, but concurrent unrelated builds make wall-time comparisons unsuitable for choosing a new default. Any follow-up executor change must preserve lane/dependency/barrier behavior and the configured simulation delays.

## Source locations at the merged revision

- [AI telemetry publication](https://github.com/Globulation2/glob2/blob/6f8cf442fe5e35aa7e7773f51f3854fa3c8e0ccf/src/ai/AI.cpp#L243) and [delivery lifecycle](https://github.com/Globulation2/glob2/blob/6f8cf442fe5e35aa7e7773f51f3854fa3c8e0ccf/src/ai/engine/AIPipeline.cpp#L116).
- [Tower-range query](https://github.com/Globulation2/glob2/blob/6f8cf442fe5e35aa7e7773f51f3854fa3c8e0ccf/src/unit/UnitDisplacement.cpp#L463).
- [Food normalization](https://github.com/Globulation2/glob2/blob/6f8cf442fe5e35aa7e7773f51f3854fa3c8e0ccf/src/ai/maxima/AIMaximaFoodLedger.cpp#L71) and [ledger walk](https://github.com/Globulation2/glob2/blob/6f8cf442fe5e35aa7e7773f51f3854fa3c8e0ccf/src/ai/maxima/AIMaximaFoodLedger.cpp#L139).
- [Snapshot refresh](https://github.com/Globulation2/glob2/blob/6f8cf442fe5e35aa7e7773f51f3854fa3c8e0ccf/src/engine/sim/snapshot/WorldCapture.cpp#L189).

See report.md for exact-revision native sampling, counters and timing scope, and food-normalization-disassembly.txt for optimized instructions. The earlier GCC 15 Callgrind caller excerpts are supplemental evidence from the five-patch combination before subsequent upstream gradient-policy changes.
