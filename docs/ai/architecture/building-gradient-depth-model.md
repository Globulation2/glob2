# The building-field depth model

Scheduled footprint fields prepare part of a search on a worker and transfer its
frontier to the serving generation at the fixed publication deadline, including
for cold requests. Hiring workers resolve candidates through a locked shared
lease without changing owner demand history. Live readers
resolve their requested cells before using them. Preparation depth changes where
CPU work happens, while the frozen input, resulting values and deadline remain
the same.

## Reader demand

`Map::predictBuildingDepth` uses two observations from the same field:

- `serving`: the current search's `requiredCost()`, or -1 when unknown.
- `previous`: the replaced search's final reader demand, retained in
  `Building::settledCostHint`, or -1 when unknown.

Worker preparation and completing a field for serialization do not contribute to
reader demand. Each scalar query records its required cost layer, including when
the worker already prepared the answer. Unreachable queries require exhaustion;
seeded goals and blocked cells require no propagation. Demand resets with each
new search, allowing preparation to shrink after lower-demand lifetimes.

## Production policy

Let `m = max(serving, previous)`. The retained fitted l0100 policy returns 32 when
both observations are unknown; otherwise it returns
`clamp(166 + ((292 * m) >> 8), 32, 65535)`, further limited to `COST_LIMIT` by the
caller. Clearing and combat fields still prepare their full depth.

The coefficients and reader-demand semantics are unchanged by the engine cleanup.
Experimental operating points, full/lazy overrides, search histograms, lifetime
exports and training commands have been retired. There is one production policy
in `src/map/gradient/BuildingGradientDepthPolicy.h`.

## Verification and limits

`BuildingGradientInvalidation` checks publication deadlines, transferred-frontier
continuation, exact full-field equivalence, and reader-demand retention across
rebuilds and save completion. `test/check_gradient_pipeline.py` checks real games
and continuation with pending work. Preparation cannot change simulation values,
but a poor prediction can move excessive work onto the owner or waste worker CPU.
Use retained paired whole-process benchmarks to assess changes to the policy.
