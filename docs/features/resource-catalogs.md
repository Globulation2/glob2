# Resource catalogs and materials

A **resource** is a deposit on a map square. A **material** is inventory held by
a worker, building or market. Harvesting converts stock in a resource into one
unit of the requested material; suppliers move materials, not map deposits.
Buildings specify material costs and capacities. AI supply searches ask for a
material and consider every resource that can supply it.

The fixed material order is `wood`, `food`, `paper`, `stone`, `algae`, `cherries`,
`oranges`, `prunes`, `gold`, `metal`, `glass`, `fabric`. This order belongs to the
engine and save adapters. Adding a material requires an engine change. Resource
identities are separate, map-local 16-bit IDs resolved from stable catalog keys.

C++ map APIs use distinct `MaterialId` and `ResourceId` arguments. Integer-indexed
material kernels use explicit `Slot` names; resource-index adapters use `ByIndex`
names. Use the typed operations at domain boundaries and the indexed helpers for
validated compact loops or legacy input adapters; a deposit ID is never a material slot.

The installed definitions are `data/resources/registry.json`. Existing deposits
are trees, wheat, papyrus, rocks, algae and cherry, orange and prune trees. Gold
ore, iron ore, sand and cotton are supplied as an optional editor experiment;
stock generators, building costs and service recipes do not use them. New material
inventory rows appear only when a material is present in map sources, carried
packets or inventory and the building uses that material.

## Importing resources

Use **Map editor → Menu → Import Resource Definitions** to import a JSON file,
then **Resource palette** to select its deposits. The palette also offers switches
for experiments declared by the map's catalog. Installed catalog experiments are
available in **Settings → Experiments**. Imports validate a complete replacement
before changing the map. Existing keys retain IDs; new keys append in sorted
order. Definitions remain immutable while a match runs.

The format has `schemaVersion: 1`, a `resources` array and an optional `experiments`
array. Unknown fields, duplicate keys, out-of-range integers, unresolved materials
and undeclared experiment gates are errors. Catalogs are limited to 16,384 resource
definitions and 32 MiB, including the fully resolved snapshot saved with a map.

```json
{
  "schemaVersion": 1,
  "resources": [{
    "key": "garden:mixed-crop",
    "properties": {
      "ecology": "land",
      "habitatMask": 1,
      "growthRate": 196608,
      "spreadRate": 98304,
      "blocksGround": false,
      "persistsWhenEmpty": true
    },
    "yields": {
      "food": {"capacity": 8, "initial": 2, "consumption": "one"},
      "paper": {"capacity": 4, "initial": 1, "consumption": "all"}
    },
    "presentation": {
      "name": "Mixed crop",
      "sprite": "data/gfx/ressource",
      "minimap": [100, 160, 50],
      "levels": [
        {"stock": 0, "variants": [{"frame": 10, "weight": 1}]},
        {"stock": 5, "variants": [{"frame": 14, "weight": 1}, {"frame": 19, "weight": 1}]}
      ]
    }
  }]
}
```

Each entry supplies `key`, `properties`, `yields` and `presentation`, and may name
a `requiredExperiment`. An experiment declaration contains its stable kebab-case
`key`, English `label` and `help`. Catalog labels fall back to that English text
when the optional experiment translation keys are unavailable.

## Simulation properties

| Field | Meaning |
| --- | --- |
| `primaryMaterial` | One of the deposit's yields; defaults to its lowest material slot |
| `habitatMask` | Bit set: land 1, aquatic 2, shore 4, desert 8; defaults to land |
| `requiresGrowthTerrain` | Placement additionally requires terrain that supports resource growth |
| `requiresPermanentDepositsTerrain` | Placement additionally requires terrain that permits permanent deposits |
| `ecology` | Fertility source: `land`, `shore`, `uniform`, or `none` |
| `growthRate` | Growth opportunities before the selected ecology factor; zero disables natural growth |
| `spreadRate` | Probability of a neighbor-spreading opportunity |
| `stockDependentGrowth`, `stockBranchDivisor` | Select the stock-dependent in-place/spread arbitration; divisor defaults to 8 |
| `blocksGround`, `blocksAir`, `blocksBuilding` | Independent ground movement, flight and building-placement obstruction |
| `clearable`, `clearConsumption` | Whether clearing applies, and whether it removes `one` unit or `all` of the deposit |
| `visibleToHarvest` | Harvesting requires the relevant visibility permission |
| `persistsWhenEmpty` | An exhausted deposit stays present and can regenerate |
| `farmable` | Allows the sustainable farm-area harvesting behavior |
| `smoothPlacement` | Allows generator smoothing to mature and extend author-placed patches |

Rates use exact integer units: **196608 means one opportunity/probability one**.
`growthRate` may be 0–786432; `spreadRate` and yield growth probabilities are
0–196608. This represents wheat's one-third opportunity rate exactly. Runtime
queries use compiled tables and terrain ecology fields, not JSON or floating-point
parsing. Synchronized randomness affects simulation only; artwork choices use a
separate deterministic coordinate hash.

Each material in `yields` has a positive `capacity` up to 65535, `initial` stock
(default 1), `seedReserve` (default 1), `growthRate` (default 196608), and a
`consumption` of `one`, `all` or `infinite`. `all` removes the entire deposit,
including every other yield, while delivering one unit of the requested material.
`destroysDeposit: true` also makes a `one` harvest remove the whole deposit.
Infinite yields cannot destroy their deposit. `placementMaximum` optionally
selects the upper bound for randomized single-yield map placement; zero uses
`initial`. It must not exceed capacity or be lower than initial stock.

Clearing statistics count successful clearing operations, not removed stock units.
Each operation is attributed once to the deposit's configured primary material,
including removal of an empty persistent deposit.

Terrain definitions can specify `allowedResourceKeys` as an explicit stable-key
allowlist, including an empty list to prohibit all resources; `null` selects
capability-based habitats. Old numeric terrain resource masks are converted to
key lists by the compatibility loader. Habitat predicates still apply to explicit
allowlists. Unused material types do not create natural-material gradient work.
Periodic propagation may reuse an exact prior prepared result within a bounded,
unsaved cache. Reuse does not schedule additional material fields or change fixed
publication deadlines; see [delayed periodic gradients](../development/performance-telemetry.md#delayed-periodic-gradients).

AI source fields use a compiled material mutability mask covering every registered
definition, including unplaced ones. Permanent sources avoid periodic queued
refreshes only when harvesting, clearing and configured ecology cannot change
their availability. Explicit source edits and catalog replacement invalidate these
fields; save/load preserves whether a cached field was current or stale.

Shared generator supply checks count positive material stocks, including secondary
yields. Starting guarantees accept equivalent custom sources; bounded crop repairs
replace only clearable surplus deposits. Material frontage measures renewable
supply under harvesting, so full stocks and infinite yields remain sustainable.
Named planting recipes remain choices of the individual generator.

AI seed reservations require a finite material yield with a positive configured
spreading rate under the source's current ecology. Expansion checks use that
specific donor's habitat, including terrain key allowlists. Infinite supplies and
resources that only regrow in place remain harvestable; whole-tile forbidden
areas cannot provide partial-stock reserves. Farm areas handle those reserves
through the configured material `seedReserve`. Non-land ecology does not require
proximity to the land fertility source. Collection pauses for recovery require
actual local regrowth or a compatible neighboring donor, rather than assuming
that every Food deposit behaves like wheat.

Some stable diagnostic interfaces retain historical names: statistics metric IDs
and Cortex CSV/debug columns containing `wheat` describe Food sources or configured
recipe supply, rather than requiring a wheat deposit. Their canonical engine fields
use material and supply names; the old diagnostic labels remain compatibility aliases.

## Artwork and saved content

`presentation.levels` starts at stock zero and has strictly increasing stock
thresholds. The selected level uses the **total stock across all materials**.
Each level has one or more weighted sprite variants. `sprite` is a canonical
`data/`-relative sprite prefix; traversal and absolute paths are rejected.
Optional `animationFrames`, `animationStride` and `animationTicks` default to one.
All effective frame indices must fit 0–65535. Installed artwork is loaded through
the normal asset system; transferring a map does not download its sprite files.
Missing custom artwork uses the visible magenta fallback. The generated foundation
artwork and provenance are recorded in `datasrc/gfx/resources/manifest.json`.

Save format 140 embeds resolved resource definitions, material stocks and required
experiment metadata. Startup permits an unavailable or malformed default resource catalog so embedded
maps can still load; creating a new map without a valid default catalog reports
an error. Loading a current save uses its snapshot even when installed
catalog files have changed. Saved enabled experiments and map requirements travel
through LAN, online setups and verifier replays; local experiment preferences
cannot change an existing deposit's behavior. The supported old-save floor remains
58. Replay and network acceptance use the new simulation boundary independently.

Authoring properties compile into small indexed rows; yield arrays, names and
artwork remain separate. Single-yield cells retain inline stock. Multi-yield maps
use additional storage only for deposits that need it. Tests exercise canonical
round trips, malformed inputs, more than 255 identities, mixed material yields,
experiment transport and deterministic presentation choices.

## Regression testing

`ResourceRegistry`, `RuntimeResources`, `TerrainResources`, `TerrainEcology` and
`GradientPreparation` cover catalog validation, mixed stocks, harvesting, habitat,
movement and save continuation. `JavaScriptIntegration` covers material queries and
remembered stock with limited visibility. The resource fixtures include identities
above 255 and bounded combinations of growth, consumption, clearing and obstruction.

For a performance comparison, first freeze saves with
`test/prepare_parallel_compute.py`, retaining the baseline executable and its data
checkout. Run `test/benchmark_resource_refactor.py BEFORE AFTER WINDOWS.json
--before-root BASELINE_CHECKOUT --output OUTPUT` with compilers and other game runs
idle. The default eight measured pairs balance which executable runs first. One
additional pair per scenario is discarded to warm OS caches; every measured run
still starts a fresh process from its frozen save with zero simulation warmup.
Both executables must complete the same fixed simulation window. Shorten both
windows explicitly when a match ends early; do not compare unequal tick counts.

Before and after the campaign, `input-verification.json` checks executable,
manifest, fixture, runner/helper and Python executable hashes, resolved Linux
runtime-library paths/content, affinity, an inherited-environment digest, and the
runtime catalog inventory (`.json`, `.txt`, `.js`, `.sgsl` under each data root).
Added or removed catalog files count as changes. This is not an artwork/archive
hash or a compiler/build-provenance check; retain the build manifest separately.
Any mismatch or failed final audit prevents publication of an acceptance summary.
Python exceptions and keyboard interruption retain partial measurements and the
final audit; abrupt process termination can leave a `starting` or `running` audit,
which is incomplete evidence and must not be treated as acceptance. Before/after
hashes do not detect inputs changed and restored between those checkpoints.

The metric descriptions in `metadata.json` distinguish measured engine CPU from
whole-process CPU, wall time and peak RSS. Measured engine CPU includes every
worker thread, session summary/teardown and the final gradient drain; it excludes
session startup and requested final saving. Engine `run_ns` wall time also includes
session startup, so it is not the identical interval. Setup CPU stops before session
startup; setup/run/save CPU do not partition whole-process CPU. The structured
headless command clears a parent `GLOB2_PERF_DISABLE`, leaving production scope
collection enabled; the parent value in metadata is not an effective override.
Scope output requires `--telemetry team-timeline` and should remain outside accepted
timing runs.

Retain separate profiling runs for ecology, gradient and cache costs, and paired
completion runs for gameplay outcomes. `ResourceRuntimeBenchmark` reports
candidate-only component timings and allocations, including absent-material fields,
seed-cache and mixed-stock storage. Its growth samples advance successive map
states, not identical repeated workloads; consumer routing caches are not exercised.
These diagnostics do not establish legacy performance equivalence. Whole-process
peak RSS likewise does not isolate resource allocations.

The performance gate uses paired confidence intervals: aggregate CPU regression
above 2%, or any legacy scenario above 5%, blocks acceptance. Results whose intervals
cross these limits remain inconclusive and require more repetitions or investigation.
These measurements do not replace same-candidate per-tick determinism across worker
counts and supported platforms, or exact save/load continuation.

Older maps and saves (before format 140) resolve their numeric resource IDs against
an immutable eight-resource import catalog compiled into the compatibility adapter.
They never consult installed resource JSON. Installed defaults pin those historical
keys to slots 0–7, regardless of authoring order; additional keys sort deterministically.
Custom resources and all resources in embedded snapshots retain their registry IDs.
