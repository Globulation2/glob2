# Locust

## On this page

- [Controls](#controls)
- [Implementation](#implementation)
- [Reusable framework additions](#reusable-framework-additions)
- [Playtest limitations](#playtest-limitations)
- [Verification](#verification)
- [Implementation source](#implementation-source)

## Controls

- **Wheat amount:** scales the default 65% coverage of eligible dry ground outside home clearings
  (full cover from about 155%). Every tile holds three to five harvests. At zero, only starting rations remain.
- **Wood amount:** scales the default 20% scatter chance on eligible ground within eight tiles
  of water. At zero, the starter wood remains.
- **Home size:** requested radius 16–30, default 24. Homes shrink when needed to preserve the
  intervening fields, using the same spacing rule as Old Growth.
- **Lakes / Lake size:** zero to four requested lakes per 128×128 area, of 40–160 water corners.
  Defaults are one and 90. Lakes that cannot fit the clearance budget are omitted and reported
  in telemetry. More lakes add timber sites and reduce the finite dry wheat area.

Map dimensions use the shared 64–512 power-of-two controls, including rectangles. Colony counts
must fit the shared clearing geometry; excessive counts receive an explicit rejection. The
vacancy fallback rescues some awkward seven- and eleven-colony rectangles but cannot fit
colonies onto genuinely cramped 64-tile shapes. Seed
variation changes the fields, shorelines, lake placement and assignment of colonies to homes.
Asymmetric layouts are not claimed to be exactly fair. The candidate scorer gives fertility
zero weight because renewable food is deliberately absent.

## Implementation

`ClearingLandscape` shares Old Growth's layout construction, preserving its arithmetic, seed
streams and traversal order for already playable requests. Locust opts into a vacancy
fallback only when the ordinary clearing geometry cannot fit; Old Growth does not. Locust
has its own furnishing and final validation. Common crop
repairs are deliberately avoided because they could insert renewable wheat; trail and room
repairs can only remove resources. Numeric ID 47 is additive; save, replay and network formats
are unchanged. The geometry fallback applies only to requests the ordinary layout
cannot fit. Generated maps use the existing full-world serializer.

Telemetry records home geometry, requested/placed lakes, starting deposits, eligible dry field
area, effective coverage, ambient deposits and the final total number of food rations. Default
play is intended to put a deadline on growth and military investment. Large maps and high wheat
amounts lengthen that deadline. Human play is needed to judge that pacing.

### Shared geometry numbers

Homes use Old Growth's roomiest lattice, dealt randomly to team indices. Their effective radius
is the smaller of the requested radius and `floor(spacing / 3 - 3 - 2)`: one-third of spacing,
minus a three-tile clearing margin and two-tile sand containment ring. If the shared pond/swarm
room test fails, Locust alone tries the opt-in vacancy lattice before rejecting. The fallback
compares the ordinary team-count lattice with grids of one to four extra sites. At each removal,
it picks the site whose omission gives the largest minimum wrapped whole-tile separation;
ties keep the first site in lattice order. It accepts only a strict spacing improvement that
passes the same `homeHasRoom` radius rule. Four extra sites cover the difficult 8–11 counts
through the 12-site composite grid and cap the search cost. Degenerate dimensions return an
empty result before row arithmetic, and a caller's larger vacancy limit is clamped to four.
The sand ring prevents shoreline wood from spreading
across the entire home boundary into the exterior fields.

The home outline uses 15% radial variation; its central pond uses 30%. Their shape and placement
are identical to the existing shared Old Growth design. Optional distant lakes keep 20 tiles
from settled clearings and other lake shores: this separates the lake's 15-tile irrigation reach
from homes and makes lakes read as distinct landmarks. Lake count scales with area/16384
(128×128), rounded to the nearest integer. A seed is chosen at greatest remaining clearance;
water grows by distance priority in thousandths of a tile, perturbed by up to 2.5 tiles of
periodic noise. The noise priority uses `2500 / 65536` because its samples span 0–65535.
Existing arithmetic and stream names are preserved for Old Growth compatibility.

### Fallback and error policy

- Shrinking homes and omitting lakes are the shared layout's explicit, telemetered fallbacks.
  Locust requests no peripheral home pools because they would eliminate dry starting ground.
- Only a crowded, failing Locust layout invokes the vacancy search. A passing result records
  both its count and a fallback message; if none passes the existing room rule, the request
  remains an explicit geometry rejection. Old Growth never invokes it.
- A kit search may leave the home outline to find dry wheat, but never ignores fertility,
  swarm clearance, occupancy or terrain. The finished world must still meet walking limits.
- Missing or partial wheat, wood or quarry kits fail the candidate. They are never repaired by
  silently planting wet wheat or remote wood. Failed partially populated worlds are discarded.
- Trail carving and cramped-start relief may remove resources. A fresh final audit checks
  stock, dry wheat, shore-only wood, required supply access, building room and colony contact.
- The CLI reports failure for that exact request/seed. The lobby's existing candidate mechanism
  may try another recorded seed; no generator loop depends on time or changes settings secretly.
- Invalid calls to shared stock-cap and access-validation operations throw `GenerationFailure`.
  These catch programmer mistakes; they are distinct from a valid request that cannot place its
  promised world. Failures retain the generation stage and collected telemetry.

## Reusable framework additions

- `ClearingLandscape`: layout construction extracted unchanged from Old Growth. Locust supplies
  its home/lake settings and telemetry namespace; the resource policy stays outside the primitive.
- `pureTiles(Map, TerrainType)`: finished-map counterpart to `pureTiles(TerrainSketch, Torus, type)`.
  Mixed beach tiles are excluded. This supports final-world habitat validation after repairs.
- `capResourceStock`: caps existing stocks without refilling, creating deposits, changing sprites
  or drawing RNG. Returns tile count and remaining stock from the same pass for telemetry. It
  does not disable growth; callers must establish dryness separately. Locust sets three to five wheat stock per tile directly; the primitive is
  available to finite-crop designs that need a cap.
- `startingAccessFailure`: read-only supply and room validator. Callers supply `MaterialId` values through `MaterialAccessRule`,
  names and travel budgets; it supports stone or fruit targets as well as wheat and wood. It
  floods from actual workers with the engine's non-swimmer predicate and reports the first
  unmet rule with colony and observed distance/site count. It never plants a repair resource.

Existing callers retain their existing repair behavior. Only Old Growth is redirected through
an extracted implementation, so its golden fingerprints must remain unchanged.

### Tuning from retained failures

The first 256×128, five-colony sweep (seeds 201–204, home size 30, no lakes) exposed a
placement heuristic problem. Shared spacing shrank the effective home radius to 12. The nearest
dry pocket held only ten tiles, so a single `growPatch` returned ten and the candidate failed
even though other nearby dry ground was available.

The reusable response is **`growPatchesNear`**, not a larger water allowance or a smaller food
budget. It grows the nearest eligible patch, then the next nearest eligible patch until the
budget is filled or eligible seeds are exhausted. Existing deposits and terrain/occupancy the
engine disallows are excluded internally. Each successful iteration places at least one tile,
so the requested tile budget bounds the iterations. It returns both tiles and patch count;
Locust records a per-colony `split-rations` fallback when more than one patch is needed.
Seed searches stay within the requested box, but patch growth can extend outside it wherever
the predicate permits; the final walking audit remains mandatory. A dedicated disconnected-
pocket fixture verifies both complete placement and termination when the habitat is exhausted.

This keeps placement mechanics general and leaves only the habitat choice and opening budget
in Locust. The original failed requests are retained alongside the corrected sweep.

Retesting that rectangle fixed all ration shortfalls and exposed a second instance of the same
problem: seed 202's fourth colony had a one-tile nearest quarry pocket. Wood and quarry budgets
now use the same shared operation as wheat. Per-colony patch counts and `split-wood` /
`split-quarry` fallbacks expose that construction. The four retained seeds are also exercised
by the existing CI-wired defaults harness.

The JSON report's `canonical_quality` intentionally retains the common cross-generator weights;
it is not Locust' candidate-selection score. Use the raw supply/room/contact measurements and
the generator's own weights when interpreting this deliberately dry scenario.

## Playtest limitations

Finite dry wheat is opening stock, not renewable food. Evaluate whether a controller establishes supply around lakes before its starter rations run out.

## Verification

The source definition and its request/world validators define accepted settings and
finished-map guarantees. Run the focused `MapGeneratorDefaults` contracts and the
platform golden rows, inspect small/large rectangles and resource extremes, then
check populated games with complete seat rotations. See [generator verification](verification.md)
for commands, revisions, save/replay checks and evidence requirements.

## Implementation source

[LocustGenerator.cpp](../../src/map/generator/generators/LocustGenerator.cpp) owns this landscape's construction, controls and validation.
See the [catalog](catalog.md) for its stable command and legacy IDs.

Related: [map generators](README.md).
