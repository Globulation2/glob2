# Adaptive gradient scheduling and model collection

Periodic material, shared-market, guard, clearing and forbidden-escape fields use
one sparse demand policy. Building-specific and consumer-specific fields retain
their lazy initialization/invalidation paths. Skipping one field does not cause
another family to fill an executor slot. With no initialization, reactive request
or accepted demanded refresh, no periodic work is submitted.

A logical field key contains a stable family, team, movement class and optional
material identity. Its publication carries the immutable capture tick, effective
seed count and change epochs, movement-counter baseline and history-known flag.
Requests made while a replacement is pending do not move its deadline. Published
cells remain available until replacement publication, normally eight ticks after
capture. Adaptive admissions retain an eight-tick minimum interval and have no forced
maximum-age refresh. Age remains a model input; an idle or old field does not
refresh solely because it aged. Demand is
registered before cached-unreachable returns, so a new source or opened route
can recover. Team aliveness is not a substitute for actual demand.

## Generic inference contract

`AdaptiveGradientFeatures.h` defines seven base inputs: serving-capture age,
movement queries over 32 and 128 ticks, effective target tiles added/removed, and
their Q12 fractions relative to the captured target count. Three cumulative
since-capture counters add invalid-goal events, reachable required-travel queries
that produced no move, and their movement-query denominator. Two Q12 event rates
complete the 12 inputs. Rates use uncapped 64-bit counts before bounded count/log
transforms. Family, material, map-generator and AI identities are diagnostic
metadata, never model inputs.

The target semantics and decision threshold version independently of feature
names. Models trained on the earlier 32-tick target are incompatible and their
coefficients are not reused or rescaled. The common integer model predicts
`P(help)*E(amount|help) - P(hurt)*E(amount|hurt)` using two logistic and two linear
heads. Integer Q12 log/Q16 sigmoid interpolation, bounded accumulators and portable
64-bit fraction division make inference deterministic. The threshold is
128,000 milli direction-cost units. Generated coefficients live in
`AdaptiveGradientModel.h`; `tools/models/adaptive-gradient.json` declares the
matching contract and whether the model is trained.

An untrained header is an explicit placeholder, not a mapped resource-only model.
Ordinary execution retains cold/reactive/history initialization without a forced
maximum-age refresh and without pretending that placeholder predictions
establish adaptive quality. Collection bootstrapping requires explicit positive fixed intervals.
Interval 0 is available only with a trained generic model. Engine promotion still
requires total-compute and per-family quality evidence.

Target membership follows the actual seed predicates. Market seeds include
eligible provider footprints and natural sources; guard membership is measured
before crowding changes seed costs; clearing follows clear/farm/resource rules;
forbidden destinations are eligible free outside tiles, with border seeds derived
by the existing builder. Painted-tile counts are not interchangeable with these
sets. Route-change epochs describe persistent changes, excluding ordinary moving
unit congestion. Seed-cost changes, including weighted guard crowding, are
separate diagnostic metadata; the initial 12 features do not claim to fully
observe those changes.

## Collection

`game run --resource-gradient-probe` is the historical CLI spelling for the
**unified-gradient-spatial-h128-v3** all-family observer. It writes:

- `gradient-episodes.csv`: frozen causal inputs, complete future direction labels
  and separate outcomes, with explicit family/team/movement/material/key columns.
- `gradient-quality.csv`: live direction cost, reachability, invalid goals, missed
  moves, stalls and forbidden escape failures.
- `gradient-planning-quality.csv`: separate planning reachability outcomes.
- `gradient-contradictions.csv`: non-overlapping cumulative movement totals.
- `gradient-decisions.csv` when an explicit exploration override is enabled.

The observer preserves each family's real selector and pre-decision RNG context,
including guard balancing and forbidden lexicographic base/option ordering.
Counterfactual alternatives use independent RNG copies and a private current-world
oracle; they never publish or request live fields. Equal-quality directions have
zero cost. Actual guard-balancing moves can be direction-scored without treating
an intentional guard stay, clearing arrival or completed escape as a stall.
Movement counterfactual columns `defer_false_unreachable`,
`refresh_false_unreachable`, `defer_false_reachable` and
`refresh_false_reachable` compare each alternative’s raw field value at the
query cell (`>1` means reachable) against the current fresh field. Their
denominator is episode movement `queries`; per-arm false-reachable and
false-unreachable events are mutually exclusive. They remain distinct from
planning-query errors and selector arrival/intentional-stay outcomes. Missing
columns are rejected, never filled with zeros. Planning, reachability and escape
failures are not silently added to direction cost. Episodes apply the real publication delay and retain only complete initial
future horizons. The 128-tick window starts at capture, includes offsets 8 through
127 under the normal delay, and completes at age 128; it is not 128 additional
ticks after publication. The training contract requires the capture-anchored offsets 8..127 (complete at age 128 ticks).

Reference arrays, snapshots, oracle selectors, sampling state and streams exist
only when collection is enabled. This is an expensive diagnostic mode; its CPU
must not be reported as production-policy CPU. Collection-off behavior must be
checked against the same policy with collection on.

For cheap total-work evidence without quality collection, use
`game run --gradient-work-stats artifacts/work.csv`. The explicit observer writes
raw per-family events and adjacent `.summary.csv` and `.intervals.csv` files.
It is independent of quality collection and defaults off. Combine these counts
with ordinary release matched-game CPU/wall/tick measurements; field-cell work
and jobs are not substitutes for actual CPU time.

## Balanced tournaments and fitting

Create a 256-game plan from a frozen engine catalog:

```sh
python3 tools/adaptive_gradient_tournament.py plan \
  --catalog artifacts/catalog.json --seed 181101 --routing with-mac \
  --output artifacts/plan.json
```

The plan reserves 128 exploration and 128 candidate games, 16 of each role per
format and exactly half of every size pool per role (16 small, 96 middle and
16 large in with-Mac mode). A bounded deterministic search assigns extras in
odd format/pool strata; size/hash alternation chooses slots within each stratum.
All assignments precede map-generation or game outcomes. It freezes map/seed identities and size pools before outcomes: small work
on Pharaoh hosts, middle work on Rig/devlaptop and the largest work on the
MacBook. Current campaign invocation explicitly uses `--routing with-mac`, which
freezes 32 small, 192 middle and 32 largest Mac slots. The optional
`--routing remote-only` capability instead freezes 32 small and 224 remote
middle-pool slots when local games must be excluded. Routing changes require a new plan before outcomes;
preparation rejects relabelled pool assignments. Both modes run largest work
first within each pool. If predeclared generation fallbacks change dimensions,
create an explicit mechanical actual-size routing amendment before simulation
outcomes; it changes execution hosts only, preserving every accepted map, role
and seed. Actual role/pool totals can differ from the original planned halves
and are reported without relabelling roles.
Preparation uses immutable accepted map inputs and architecture-specific ordinary
release bundles pinned to the same source/model/contract. The shared coordinator
supplies leases and artifact transport; no separate SSH scheduler is introduced.
Each game has a 1,800-second actual-start wall cap and one process attempt.

Before preparing either role, append actual accepted map identities to a copy of
the persistent registry that retains historical held-out exclusions:

```sh
python3 tools/adaptive_gradient_tournament.py freeze-splits \
  --maps artifacts/maps.json --split-registry artifacts/map-splits.json
```

Create the immutable accepted-size amendment, then pass it to preparation:

```sh
python3 tools/adaptive_gradient_tournament.py route-accepted \
  --plan artifacts/plan.json --maps artifacts/maps.json \
  --output artifacts/actual-routing.json
```

Use `adaptive_gradient_tournament.py prepare --help` to prepare one role, with
`--routing-amendment artifacts/actual-routing.json`.
Exploration defaults to per-field intervals 8,16,32,64,128,256,512,1024,4096,
assigned from a per-game seed and the entire stable field key. Fields in a game do
not all receive the same interval. The new candidate role remains unavailable
until a generic model has been trained; an untrained bootstrap must not be
presented as an adaptive candidate.

`adaptive_gradient_export.py` exports completed trajectories and separately
recovers complete original episode records from explicitly censored ones. It
preserves raw receipt/transport hashes, excludes unterminated final CSV bytes and
incomplete horizons, validates derived rows with the same loader, and records
censoring and unknown whole-game totals. It never calls a capped game completed.

Fit the unchanged generic 12-input model and explicitly emit a research candidate:

```sh
python3 tools/adaptive_gradient_model.py fit \
  --manifest artifacts/exploration.cohort.json \
  --collector-contract unified-gradient-spatial-h128-v3 \
  --split-registry artifacts/map-splits.json --output artifacts/research.json
python3 tools/adaptive_gradient_model.py emit \
  --model artifacts/research.json --contract unified-gradient-h128-v2 \
  --output artifacts/candidate.json --header artifacts/AdaptiveGradientModel.h
```

The new loader rejects resource-only datasets. Older tools/data remain available
for reproduction; reuse would require an explicit validated semantic mapping.
Whole-map splits persist across roles, models, families and platforms, including
historical held-out exclusions. Weights balance format, role, cohort, map, family,
collection and episode. OS is not a separate cohort. Missing-family support is
reported rather than fabricated. Fit gates require each declared campaign family
in grouped cross-validation and held-out maps. By default all five families are
required. If a campaign deliberately cannot exercise some families, declare its
coverage with `--expected-families materials,guard,clear,forbidden` before fitting.
This changes coverage gates only: all observed eligible rows and their weights
remain, and the report records required, not-required and unmeasured families.
An unmeasured family receives neither synthetic zero labels nor a validation
claim. Reports also score the same unified rows with
the previous weighting, so a weighting change is not mistaken for a feature gain.

Review pooled and per-family results, format/generator/size, age/demand,
target-change, route-change and weighted-cost-change slices. A good pooled score
cannot waive a guard or forbidden-escape regression. Coefficient emission is not
production approval: compare total gradient work and ordinary release CPU against
master, the resource-only reference, and useful fixed-interval baselines using
matched maps/seeds, repeated interleaved order and controlled physical-core/SMT
load. Keep diagnostic quality repeats separate from production timings.

Review age/demand joint support explicitly before qualifying an uncapped model;
older-field extrapolation is a coverage limitation, not a reason to add a hidden
forced-refresh fallback.

Scheduling/state changes require save/load, thread/platform checksum and
simulation/replay compatibility checks. Later coefficient-only rounds retain the
same numerical/contract gates and the agreed focused validation workflow.

## Spatial seed-overlap research

The spatial-h128-v3 collector extends the dataset, independently of the 12-input
`unified-gradient-h128-v2` inference contract. It initially validates natural ground
resource and clearing fields. Markets, air, weighted guard seeds and derived
forbidden borders report explicit unsupported reasons. Supported fields attached
after publication have unknown history until a real replacement publishes.
Neither case is silently treated as measured zero.

For each eligible query the observer compares its aligned 4×4 patch against the
field actually read. Old seed classes come from the published gradient; current
classes use the authoritative family seed predicate. Goal differences and
passability differences excluding goal cells count once per query/category.
Directional additions/removals and blocked/opened categories are diagnostic.
No previous full seed buffer, world heatmap or per-query allocation is required.
These comparisons exist only with diagnostic collection enabled. To isolate
patch bookkeeping overhead using the same binary and other oracle diagnostics,
compare `--resource-gradient-probe --gradient-spatial-overlap on` with
`--resource-gradient-probe --gradient-spatial-overlap off` in repeated interleaved
ordinary release runs. The isolation flag requires the probe and result metadata
records `spatial_overlap_collection`; it is not a production scheduling toggle.

Overlap counters and their denominator reset at actual publication. They do not
use the capture-time movement-counter baseline. Episodes freeze the generation,
publication tick, known/support flags, support reason and raw counters at their
start, independently of the subsequent label horizon. The loader requires the
exact spatial-h128-v3 contract and all columns; it never fills older data with zeros.
Raw partial recovery also preserves the explicit categorical support reason.
An explicit spatial-disabled isolation run retains the supported-family flag but
reports reason `disabled` and unknown history; its zeros are never training data.

Compare research variants without changing generated inference:

```sh
python3 tools/adaptive_gradient_spatial.py \
  --manifest artifacts/exploration.cohort.json \
  --split-registry artifacts/map-splits.json \
  --output artifacts/spatial-research.json
```

Every variant uses the same eligible known resource/clear rows, family-balanced
weights and persistent map splits. Variants compare the baseline 12 inputs with
overlap counts, fractions, their combination and directional diagnostics.
Development-fold regret selects a research variant; held-out comparisons remain
exploratory. Reports include separate family/scenario slices, calibration around
the 128-unit threshold and benefit captured at equal weighted episode-selection
shares. Those shares are offline ranking diagnostics, not measured simulation
refresh rates or CPU savings. Stalls, planning and escape outcomes remain separate
from direction-cost labels. Missing resource/clear fold coverage blocks the fit;
unsupported guard/forbidden/market/air support remains a reported gap.

There is no augmentation emitter or automatic promotion. Retaining any overlap
inputs requires real collection data, prospective quality/compute validation,
measured enabled/disabled overhead and scheduling-state compatibility work.
