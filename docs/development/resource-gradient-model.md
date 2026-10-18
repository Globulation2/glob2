# Adaptive resource-gradient model

Natural ground-resource fields use adaptive 32 in every match; air routing
retains its existing refresh policy. `32` is the predicted
avoided direction cost over a 32-tick episode, not a refresh interval. The
scheduler consumes movement demand, admits a field no sooner than eight ticks
since its previous submission, and admits demanded fields at 128 ticks even when
the prediction is lower. Multiple accepted fields share one executor batch and
publish after the configured fixed delay. Market, consumer-specific and area
fields keep their existing mechanisms.

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
python3 tools/resource_gradient_model.py run \
  --binary build/resource-gradient-release/src/glob2 \
  --output artifacts/resource-gradient-training \
  --seeds 5101 5102 5103 5104 --ticks 90000 --jobs 4 --threads 4
```

The default plan has four independently seeded maps per format: Isles 64 duel,
River 128 four-player FFA, River 256 4v4, and Savannah 256 sixteen-player FFA.
All use Nicowar. `--jobs` bounds concurrent games; `--threads` is compute
participants per game. Collection timings are diagnostic and never CPU evidence.
A custom `--plan plan.json` is a list of records with `format`, `generator`,
`size`, `players`, `alliances`, `map_seed`, `game_seed` and optional `ai` fields.
Keep `format` consistent for comparable generator/size/player/alliance settings.

The output retains the frozen plan, binary/model hashes, exact commands, map
hashes, losslessly compressed episode/query CSVs and an atomically updated manifest. Repeating the exact
command resumes completed games; changing its plan or build requires a fresh
output directory. Preserve raw data outside Git under `artifacts/`. Incomplete
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
python3 -m unittest tools.test_resource_gradient_model
```

The feature list, transforms and regularization are fixed. Every fit standardizes
using its training rows only, fits logistic heads with C=1 and conditional ridge
heads with alpha=100, then folds standardization into exported integer
coefficients. Constant outcome classes are represented explicitly. Weights give
equal mass to each format, then each starting state, then collection game, then
episode. All collection rounds for a starting state stay together.

At least four independent states per format are required. A quarter of states per format
(rounded up), selected by sorted state identity without labels, are excluded from
coefficients as a holdout; development states have three grouped validation folds. Reports
score the quantized integer model. Held-out error and selected-episode benefit
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
