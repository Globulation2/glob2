# Historical building-field depth model

Simulation format 167 publishes complete building fields. The depth predictor no
longer controls live scheduled fields, and readers never extend a published
frontier. This guide records the earlier policy and its retained scalar predictor;
it does not describe current publication behavior.

The earlier implementation prepared part of a footprint search on a worker and
transferred its frontier at the fixed deadline. Readers then resolved requested
cells. The policy below moved work between worker and owner without changing the
frozen field's mathematical result.

## Reader demand

The earlier `Map::predictBuildingDepth` used two observations from the same field:

- `serving`: the current search's `requiredCost()`, or -1 when unknown.
- `previous`: the replaced search's final reader demand, retained in
  `Building::settledCostHint`, or -1 when unknown.

Worker preparation and completing a field for serialization do not contribute to
reader demand. Each scalar query records its required cost layer, including when
the worker already prepared the answer. Unreachable queries require exhaustion;
seeded goals and blocked cells require no propagation. Demand resets with each
new search, allowing preparation to shrink after lower-demand lifetimes.

## Previous production policy

Let `m = max(serving, previous)`. The retained fitted l0100 policy returns 32 when
both observations are unknown; otherwise it returns
`clamp(166 + ((292 * m) >> 8), 32, 65535)`, further limited to `COST_LIMIT` by the
caller. Clearing and combat fields prepared their full depth.

The coefficients and reader-demand semantics are unchanged by the engine cleanup.
Experimental operating points, full/lazy overrides, search histograms, lifetime
exports and training commands have been retired. The retained historical policy is in
`src/map/gradient/BuildingGradientDepthPolicy.h`; live scheduled jobs always use
`COST_LIMIT`.

## Current verification

`BuildingGradientInvalidation` and `DelayedGradientPublication` check complete
field equivalence, cold Pending reads, fixed publication deadlines, save
continuation and identical trajectories across compute participant counts.
`GradientExecutionAudit` checks that simulation-owned shared field preparation
executes no owner kernels when workers are available. The serial configuration
keeps identical publication semantics.
