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
their Q12 fractions relative to the captured target count. The selected model uses
only these seven inputs. Same-row grouped ablations did not establish a benefit
from adding contradiction counts or event rates, so those inputs are excluded.
Three cumulative movement counters remain in capture/save bookkeeping for causal
collector histories and backward compatibility; they do not enter inference.
Family, material, map-generator and AI identities are diagnostic metadata,
never model inputs.

The target semantics and deployment threshold are separate from feature
names. Models trained on the earlier 32-tick target are incompatible and their
coefficients are not reused or rescaled. The common integer model predicts
`P(help)*E(amount|help) - P(hurt)*E(amount|hurt)` using two logistic and two linear
heads. Integer Q12 log/Q16 sigmoid interpolation, bounded accumulators and portable
64-bit fraction division make inference deterministic. The default deployment threshold is
16,000 milli direction-cost units. Generated coefficients live in
`AdaptiveGradientModel.h`; `tools/models/adaptive-gradient.json` declares the
matching contract and whether the model is trained.

`fit` and `emit` accept `--threshold-milli`
(default `16000`), a positive signed 64-bit integer. The generated
`AdaptiveGradientModel::ThresholdMilli` is the engine's threshold authority.
Changing it does not rescale predictions or refit the four heads. Emitting an
existing research fit at a different threshold preserves its original metrics
and provenance, records their original evaluation threshold in `deployment`, and
requires separate prospective validation. Re-evaluate metrics at the selected
threshold rather than presenting the old fit report as a new threshold result.

H128 label semantics remain offsets 8 through 127 regardless of collection
policy. Historical H128v2 and spatial-v3 cohort declarations included
`refresh_threshold_milli`; loaders validate and remove only that behavior field
when comparing label contracts. All other label fields must match exactly. The current collector omits the
unpromoted spatial experiment; its explicit read-only v3 decoder preserves old
cohort bytes and reads only the unchanged H128 target and production inputs. Original
sealed contracts and hashes remain untouched; rows retain their source contract
hash and actual collection threshold. Explicit `policy.threshold_milli` is
authoritative, with the old declaration used only when policy metadata lacks it.
Conflicting explicit policy declarations are rejected. New label contracts omit
the threshold; old 128000 deployment models remain valid for comparisons.
New supplied bundles must explicitly attest `build_options.refresh_threshold_milli`
as a positive signed 64-bit integer. Every pool must agree, and preparation
records that attested value as actual collection policy. Missing or contradictory
attestations fail rather than assuming a default. Frozen historical exports remain
readable and their bytes are not amended.
Hash-only qualification receipts accept only the canonical declaration or the
explicitly reconstructed historical 128000 declaration.

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
separate diagnostic metadata; the seven selected features do not claim to fully
observe those changes.

## Collection

`game run --resource-gradient-probe` is the historical CLI spelling for the
**unified-gradient-h128-v4** all-family observer. It writes:

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
ticks after publication.

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

Fit the generic seven-input model and explicitly emit a research candidate:

```sh
python3 tools/adaptive_gradient_model.py fit \
  --manifest artifacts/exploration.cohort.json \
  --collector-contract unified-gradient-h128-v4 \
  --split-registry artifacts/map-splits.json --threshold-milli 16000 \
  --output artifacts/research.json
python3 tools/adaptive_gradient_model.py emit \
  --model artifacts/research.json --contract unified-gradient-h128-v2 \
  --threshold-milli 16000 --output artifacts/candidate.json \
  --header artifacts/AdaptiveGradientModel.h
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

When testing continuation across a save-version bump, use current-format saves
for both sides of the raw world-checksum comparison. `MapHeader::checkSum`
includes the loaded format version, while saving writes the current version
without changing that in-memory identity. To test an older save, advance it one
tick and save, continue that current-format checkpoint, then compare a later
checkpoint's resumed raw checksum suffix against that control. Also compare the
older-input run's snapshots and final saved state. Preserve and explain any
format-identity difference; do not change or mask engine checksums to pass.

### One-command research campaign

The permanent driver connects the components above. It uses the ordinary
tournament coordinator for all games and accepts explicit build/qualification
and optional remote-fit adapters:

```sh
python3 tools/adaptive_gradient_campaign.py run \
  --config artifacts/campaign-config.json --directory artifacts/campaign
```

The same command resumes the same directory. It freezes configuration, tool and
worker identities, bundle manifests, maps, historical split assignments and
stage outputs. Changed inputs fail rather than silently starting another cohort.
A campaign lock prevents duplicate drivers. Use `status --directory
artifacts/campaign` to read its ledger. Each configured round has 256 preselected
slots: 128 exploration and 128 candidate, 16 of each role per format. Games retain
the frozen 1,800-second cap and one attempt; the driver never retries capped
games. It waits for every slot, preserves complete horizons from censored games,
and stops on unresolved slots or non-cap failures before fitting.

For example, a configuration using already generated maps is:

```json
{
  "schema": 1,
  "catalog": "catalog.json",
  "bootstrap_bundles": ["bundles/small-id", "bundles/linux-id", "bundles/mac-id"],
  "hosts": "hosts.json",
  "host_pools": {
    "pharaoh-1": ["small-pharaoh"],
    "pharaoh-2": ["small-pharaoh"],
    "rig": ["middle-linux"],
    "dev": ["middle-linux"],
    "mac": ["large-mac"]
  },
  "routing": "with-mac",
  "split_registry": "historical-map-splits.json",
  "expected_families": ["materials", "guard", "clear", "forbidden"],
  "rounds": [{"seed": 181101, "accepted_maps": "accepted-maps.json"}],
  "build_hook": {
    "argv": ["python3", "build-research-candidate.py", "{request}", "{receipt}"],
    "inputs": ["build-research-candidate.py"]
  }
}
```

Paths are relative to the configuration file, except bundle paths returned by
hooks, which must be absolute. Host names match the ordinary coordinator host
file; its CPU budgets, worker directories, transport and runtime environment
remain explicit. Unlike the general coordinator's optional discovery, every
campaign host entry must declare `"platform": {"os": "linux", "arch": "x86_64"}`
(or its actual platform, such as `darwin`/`arm64`). This is validated before any
plan or jobs are created. The driver derives the current bundle allowlists from these
pool assignments. Supply qualified immutable bootstrap bundles for every
required platform/pool. Bootstrap bundles are checked up front for ordinary O3
release flags, exact model/collector contracts and common source/model/header
identities. They may explicitly contain an untrained model for positive fixed
exploration intervals; development, PCH and unity builds are rejected.
Existing accepted maps must match the deterministic plan for that seed,
including original slot groups and predeclared fallbacks. Alternatively omit
`accepted_maps` and supply `map_generator_bundle`: one immutable runtime used for
central sequential pure-map generation. Only explicit generation rejections use
the existing fallback list; crashes and timeouts stop. Accepted maps and their
receipts are resumable. The driver appends every predeclared fallback identity
before generation, retains historical exclusions, and writes per-round registry
copies without modifying the supplied historical registry.

Optional `prior_manifests` supplies compatible sealed cumulative data; old
resource-only contracts remain forbidden. `intervals` changes the explicitly
fixed exploration mixture; candidate games never get that override.
Missing-family requirements never silently relax: the declared family
scope goes into every fit and its provenance.

The sequence is exploration collection/export, cumulative generic fit and
candidate emission, external build/qualification, reserved candidate collection,
then cumulative fit/emission including both roles. Further entries in `rounds`
repeat this sequence with new frozen seeds and cumulative compatible data.
Final JSON/header files are staged research outputs reported in `campaign.json`;
they are not silently installed as the stable model. Candidate installation and
ordinary compilation occur only through the explicit hook. Stable promotion,
commits and pull-request actions remain separate. No cross-platform build system
or source-version editor is hidden inside this driver.

Hooks are trusted **argv arrays**, executed synchronously without a shell. Both
`{request}` and `{receipt}` placeholders are required; `{directory}` is also
available. Declare adapter/script inputs so changing them invalidates resume.
They must finish their work and write a JSON receipt, not detach background
jobs. Logs and the actual argv stay beside each request. Every receipt includes
`request_sha256`, the SHA-256 of the exact request file. Reusing a receipt from
different data or coefficients fails. A receipt alone never establishes success:
the driver records a request/receipt/argv-bound success marker only after a
zero child exit. A remaining running-attempt record or a receipt without that
marker blocks resume, even if its recorded PID is no longer present. Inspect
the adapter and preserve its attempt evidence before explicit recovery; PID
reuse or a partially written receipt must not authorize another build or games.

By default fitting runs locally through the permanent fitter. For Rig execution,
provide `fit_hook` with the same argv/input shape. Its request pins manifests and
their hashes, registry and hash, family scope, contract and the required local
research-model output path. The adapter transfers immutable inputs, invokes
`adaptive_gradient_model.py fit`, and returns the model to that path. Its receipt
has `status: "fit-complete"` and `model_sha256`. Preserve requested manifest and
registry identities in the returned model provenance, recording any remote path
mapping in the adapter evidence. In particular, verify every transferred episode
against its sealed manifest hash, retain the remote input/hash inventory and
original-to-remote path mapping in the receipt, and map provenance back to the
canonical requested identities without changing their hashes. A supplied remote
adapter needs its own integration verification; the driver's mocked orchestration
tests do not establish remote transfer correctness. The driver verifies the
requested identities before
emission; a command that merely returns a plausible model is insufficient.

The build request pins the emitted JSON/header paths and hashes plus the
collector contract. The adapter applies the research coefficients in its own
checkout, performs the required simulation/version/golden updates and ordinary
build, runs the appropriate lightweight numerical/determinism gates, and
registers immutable bundles with the permanent bundler. It returns:

```json
{
  "request_sha256": "request-file-sha256",
  "status": "qualified-research",
  "stable_promotion": false,
  "model_sha256": "emitted-model-sha256",
  "header_sha256": "emitted-header-sha256",
  "collector_contract_sha256": "canonical-collector-contract-sha256",
  "checks": {"integer_parity": true, "determinism_smoke": true, "simulation_gates": true},
  "bundles": ["/absolute/qualified/bundle-id"],
  "binaries": [{"binary_sha256": "actual-elf-or-mach-o-sha256", "source_revision": "actual-source-sha", "platform": {"os": "linux", "arch": "x86_64"}}]
}
```

Supply every needed bundle and actual binary, not only the illustrative Linux
entry. Each bundle must pin the same source/model/header/collector, ordinary
release flags, execution pool and `unified_model_trained: true`. The driver checks
their actual files and qualification identities before candidate admission.
Adapter implementation is an explicit environment-specific boundary: a config
with missing adapters is not a self-contained build recipe. The driver and its
focused mocked orchestration tests can be verified without starting games;
adopting a live campaign or replacing its frozen tools is deliberately excluded.
