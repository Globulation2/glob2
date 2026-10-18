# Adaptive resource-gradient model

Natural ground-resource fields use adaptive 32 in every match; air routing
retains its existing refresh policy. `32` is the predicted
avoided direction cost over a 32-tick episode, not a refresh interval. The
scheduler consumes movement demand, admits a field no sooner than eight ticks
since its previous submission, and admits demanded fields at 128 ticks even when
the prediction is lower. Multiple accepted fields share one executor batch and
publish after the configured fixed delay. Market, consumer-specific and area
fields keep their existing mechanisms.
Owner distance/availability reads also request upkeep, including a stale negative
answer that would otherwise prevent recruitment. They do not increment the
movement-query features; the demanded 128-tick cap still guarantees a retry.

## Runtime and disabled diagnostics

`ResourceGradientPolicy` owns simulation state. Each allocated natural field
tracks serving capture/source epochs, last submission and exact 32/128-tick query
counts in a 128-bucket ring. Source bookkeeping counts tile stock transitions
between zero and nonzero; consuming part of a deposit does not count as removal.
Initialization scans source tiles once. Queries update counters without per-query
allocation. A rolling hash avoids scanning the history during checksums.

The predictor uses seven features, in generated order: serving capture age,
queries in 32 and 128 ticks, added sources, removed sources, and added/removed
counts divided by source count at capture in Q12. Inputs clamp at one million;
integer log2(1+x) interpolation produces Q12 features. Four linear heads estimate
positive/negative benefit probabilities and conditional magnitudes. Their
probabilities use an integer sigmoid lookup table. Accumulators use signed
64-bit arithmetic with export-time bounds. The final prediction is milli cost;
the engine compares it with 32000. It uses no material/category IDs, simulation
RNG, clock, floating point or platform-specific math library.

`ResourceGradientProbe` owns all diagnostic state. With collection disabled no
probe, output stream, reference field, episode copy, sampling map or diagnostic
source bookkeeping is allocated. Only null checks remain in tick/movement paths.
The policy's demand and turnover counters are required by gameplay regardless of
whether data is being collected. The collector reads copies of policy state and
unit RNG state; it must never advance those live inputs.

## Collect a tournament

Use a release binary and run from the repository root. No runtime dependencies
are added. The runner itself uses the Python standard library. Fitting uses the
pinned development numpy/scikit-learn dependencies in `requirements-dev.txt`.

```sh
scons release=1 --build=build/resource-gradient-release -j4 \
  build/resource-gradient-release/src/glob2
python3 tools/resource_gradient_model.py plan \
  --binary build/resource-gradient-release/src/glob2 \
  --output artifacts/resource-gradient-plan.json --games 256 --seed 5101
# Inspect the printed coverage and saved plan before starting collection.
python3 tools/resource_gradient_model.py run \
  --binary build/resource-gradient-release/src/glob2 \
  --plan artifacts/resource-gradient-plan.json \
  --output artifacts/resource-gradient-training \
  --ticks 90000 --jobs 4 --threads 4
```

The default is a **bounded 256-game stratified random sample**, rather than a
cross product or fixed map lineup. It uses the same `generator_samples` helper
and catalog `searchValues` domains as the AI rating tournaments:

- Thirty-two games each: 1v1, 3/4/8/16-player FFA, 2v2, 4v4 and 8v8.
- All non-editor generators advertised by the supplied binary are eligible.
  Choose among the least-used compatible generators, so each is sampled before
  any compatible repeat. A budget smaller than the generator pool samples a
  subset; the primary-plan coverage report explicitly lists unselected generators.
- Width and height draw from 64, 128, 256 and 512 tiles, including rectangular
  maps with aspect ratio at most 2. Registered dimensions/player counts must
  permit the combination. Density stays between 1,024 and 32,768 tiles per
  colony, avoiding extremely cramped starts and very sparse expensive games.
  Compatible shapes receive balanced exposure.
- Generator-specific controls and starting workers vary over their catalog
  search domains. These are the engine's curated search values, not arbitrary
  values invented by the collector. Geometry and player count take precedence.
- All native AIs in the catalog are eligible. Each game has a 50% chance of
  a homogeneous roster; otherwise it draws a mixed roster; a mixed draw can still
  coincide. AI seat exposure is balanced, and alliance seating is shuffled.
- Each game has separate random 32-bit map and simulation seeds. Plan order is
  shuffled, and the sampling seed, entire catalog snapshot, controls, roster and
  map dimensions are frozen before collection. The same inputs reproduce the
  same plan.

`--games`, `--seed`, `--generators`, `--sizes`, `--formats` and `--ais` constrain
the sampling design on `plan` or on `run` without a supplied plan. `plan` only
inspects the catalog and writes a plan; it runs no matches. The game budget must
cover every requested format at least once. A useful fit needs at least four
independent maps **with complete episodes** per format; the 256-game default
provides 32 planned maps each but sparse games may yield no usable episodes.
Inspect collection coverage before fitting or increasing the budget.

`--jobs` bounds concurrent games; `--threads` is compute participants per game.
Collection timings are diagnostic and never CPU evidence. Custom legacy JSON
lists are also accepted: records need `format`, `generator`, `width`/`height`
(or square `size`), `players`, `alliances`, `map_seed`, `game_seed`, optional
per-seat `ais` (or uniform `ai`) and optional displayed `generator_params`.
`format` should identify the match rules (such as `4ffa`), not a unique map or
seed: it is the balancing/holdout stratum. Generator parameters cannot override
the separately recorded geometry, player count or map seed. Individual catalog bounds do not encode every generator's relationship constraints.
Each sampled match therefore freezes up to 32 map candidates: its balanced primary
draw followed by random alternatives from the same eligible pool. The format,
AI roster, alliances and simulation seed stay fixed while map inputs vary.
Collection tries the candidates in order, accepting the first playable map.
Only structured invalid-request, placement and world-validation rejections are
retried. Crashes, script errors, missing dependencies and other failures stop the
run; exhausting candidates also fails rather than shortening the tournament.

Every rejection keeps its specification, command, JSON report and log. The
manifest records the actual selected map and its validation-group identity;
`coverage.json` reports actual completed-match coverage separately from planned
primary coverage. This samples playable configurations conditional on successful
generation, so acceptance can change the generator/dimension mix. Inspect both
coverage and rejections before claiming broad representation; a catalog entry
alone does not establish a feasible generator/size/player combination. Custom
lists have no alternatives unless explicitly supplied in `map_candidates`.

The output retains the frozen plan, binary/model hashes, exact commands, map
hashes, losslessly compressed episode/query CSVs and an atomically updated manifest. Repeating the exact
command resumes completed games; changing its plan or build requires a fresh
output directory. Generation has a ten-minute process timeout; matches default to
`--timeout-seconds 7200`. Failures are retained in `failures.json`; other queued
games still finish, compress and enter the manifest before the runner exits
unsuccessfully. Repair failures and resume the frozen plan rather than omitting
those states. Preserve raw data outside Git under `artifacts/`. Incomplete
32-tick episodes remain in the CSV but never enter a fit.

Before interpreting a new collector or policy implementation, run identical
starting state/seed/orders with probing enabled and disabled and compare per-tick
checksums, also across the supported platforms. Repeated collection from a state
stays in the same validation group, even if collected with a later model.

## Fit, validate and replace coefficients

```sh
python3 tools/resource_gradient_model.py fit \
  --manifest artifacts/resource-gradient-training/manifest.json \
  --output artifacts/resource-gradient-training/model.json
python3 tools/resource_gradient_model.py emit \
  --model artifacts/resource-gradient-training/model.json \
  --header artifacts/resource-gradient-training/ResourceGradientModel.h
python3 -m unittest tools.test_resource_gradient_model tools.test_resource_gradient_tournament
```

The feature list, transforms and regularization are fixed. Every fit standardizes
using its training rows only, fits logistic heads with C=1 and conditional ridge
heads with alpha=100, then folds standardization into exported integer
coefficients. Constant outcome classes are represented explicitly. Weights give
equal mass to each format, then each starting state, then collection game, then
episode. All collection rounds, AI-roster changes and seat swaps of the same generated
map stay together. New manifests record this map identity separately from the
full game identity; older manifests with saved map specifications derive the same identity.
Manifests without map metadata retain their historical starting-state groups.

At least four independent maps with complete episodes per format are required.
Approximately a quarter of maps are excluded from coefficients as a holdout;
development maps have three validation folds. Seeded `StratifiedGroupKFold`
assigns whole maps, stratifying map/format pairs rather than episodes. This
handles maps reused across format labels without splitting them. Every format
must retain at least one holdout map, three development maps, and at least one
validation/two training maps in each fold; otherwise fitting fails explicitly
and requests more independent maps. Split coverage is recorded in the fit report. Reports
score the quantized integer model, including holdout slices by match format,
generator and dimensions. Each slice reports its sample/map count; small slices
are diagnostic, not evidence of precise generalization performance. Held-out error and selected-episode benefit
are useful diagnostics, but do not prove lower full-match CPU or better gameplay.
Measure prospective matched maps after refitting, because a different scheduler
changes the trajectories that generate the next dataset.

The committed seed model is `tools/models/resource-gradient.json`; its provenance
records the frozen diverse refit used to select A32. To replace it, retain the new
fit report and data as review evidence, copy the reviewed model JSON to that
path, and run `python3 tools/resource_gradient_model.py emit`. The generated
header is the only model used by the engine. No model-selection environment
variables or alternate runtime thresholds are supported. The Python tests check
header reproducibility, overflow bounds, dataset hashes, balancing and C++/Python
integer parity under undefined-behavior sanitization.

Every coefficient/policy change changes simulation behavior. Bump `SIM_REVISION`,
regenerate the golden match, and verify per-tick deterministic execution,
collector on/off equivalence and save continuation including in-flight batches.
New scheduling state is persisted from save format 174; older supported saves
load with conservative empty demand history and retain their saved fields and
deadlines. Saves keep their separate historical compatibility floor; earlier
replays and mixed-version network matches use the updated acceptance gates.
