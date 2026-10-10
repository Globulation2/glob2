# Resource catalogs and materials

## On this page

- [Importing resources](#importing-resources)
- [Artwork and saved content](#artwork-and-saved-content)
- [Regression testing](#regression-testing)
- [Related guides](#related-guides)

## Importing resources

Use **Map editor → Menu → Import Resource Definitions** to import a JSON file;
its deposits appear in the **Resources** tab of the editor dock, which opens at
the imported section. Each resource card names the terrains it may be placed on.
Resources gated by an experiment the map does not carry are shown locked, and
their section offers **Enable for this map**, which adds the experiment to the
map's header. Installed catalog experiments are
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

## Artwork and saved content

`presentation.levels` starts at stock zero and has strictly increasing stock
thresholds. The selected level uses the **total stock across all materials**.
Each level has one or more weighted sprite variants. `sprite` is a canonical
`data/`-relative sprite prefix; traversal and absolute paths are rejected.
Optional `animationFrames`, `animationStride` and `animationTicks` default to one.
All effective frame indices must fit 0–65535. Installed artwork is loaded through
the normal asset system. Themed set sheets use map-owned artwork bundles instead,
so transferring those maps includes their custom sprite files.
Missing installed artwork referenced by manually imported legacy definitions uses
the visible magenta fallback. A missing spritesheet belonging to a credited set
bundle is rejected during import or load. The material icons are generated
artwork recorded in `datasrc/gfx/resources/manifest.json`.

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

### Foundation deposit artwork

The gold ore, iron ore, silica and cotton deposit sprites
(`data/gfx/resource-<name>0.png`) and their 4x HD frames are painted
procedurally by `tools/artwork/paint_resources.py`. Deposit art should fill its
32px cell the way the stock rocks do, with an identifying silhouette as well as
colour: boulders with gold veins and nuggets, darker boulders with rust bands
and metallic chunks, upright quartz crystals (so silica stays visible on sand),
and a leafy bush whose white bolls sit in brown husks. To change one, edit its
layout and palette in the tool, preview it on real terrain, then write the
frames and check the HD pack:

```sh
python3 tools/artwork/paint_resources.py --sheet artifacts/resources.png [--ground terrain-dirt0]
python3 tools/artwork/paint_resources.py
python3 tools/artwork/package_runtime.py --check
```

The tool needs NumPy, SciPy and Pillow, and it is deterministic, so an
unchanged rerun reproduces the committed pixels. Only artwork changes; the
registry, saves and simulation are unaffected.

### Landscape resources

The `landscape-resources` experiment gives map makers more variety without new
mechanics: every entry is ordinary registry data over existing materials, and
stock generators do not place them. Behaviour comes only from the fields above.

| Key | Yields | Behaviour |
| --- | --- | --- |
| `jungle-trees` | wood | Like trees, faster growth; two looks per stage |
| `pine-trees` | wood | Like trees, slower growth; two looks per stage |
| `dead-trees` | wood 3 | No growth or spread; finite |
| `ruins` | wood 4, metal 2 | Finite scavenge site |
| `camp-site` | food 2, wood 2, fabric 1 | Finite scavenge site |
| `ancient-debris` | metal 3, gold 2 | Finite scavenge site; same placement as the ore deposits |
| `scrub` | wood 2 | Does not block walking; blocks building until cleared; slow spread |
| `tall-grass` | wood 1 | Does not block walking; blocks building until cleared; spreads |
| `maize` | food 4 | Farmable crop; faster growth, smaller stock |
| `potatoes` | food 5 | Farmable crop; `uniform` ecology, so it grows steadily away from water |
| `rice` | food 5 | Farmable crop; `shore` ecology, so it grows only near sandy coasts |
| `fish` | food 5 | Aquatic; grows and spreads in shallow water (deep water never grows resources) |

Scavenge sites start full, never grow and vanish once every yield is gone; their
sprite shrinks with the total stock. Because no building consumes metal, gold or
fabric yet, a site keeps its last pieces until a clearing area removes it.
Two catalog-wide properties follow from the definitions rather than from placed
deposits, so the set keeps them unchanged except where noted. Scavenge sites do
not yield stone: a finite stone yield would make stone a mutable material source in
every new map and give up its static-gradient cache. A material's habitat is the
union of the habitats of every resource yielding it, and AI fertility, crop maps and
generator checks read it; dead trees, camp-sites and ruins therefore need growth
land like trees and wheat, and debris matches the ore deposits. Fish are the one
deliberate exception: as an aquatic food they make water food habitat in every new
map, which changes how the AIs read fertility.
Farm areas replant the farmable crop next to each cell, so a maize or potato field
keeps its crop; an empty field and a boundary with wheat use the lowest-ID crop,
wheat. Fish are the first resource to use `animationFrames`: each stock level is
twelve frames advanced every three ticks, so the animation pauses with the game.

The sprites (`data/gfx/resource-<key><frame>.png`, with 4x HD frames) are painted
by `tools/artwork/paint_landscape.py` in the classic pastel style: one frame per
stock level, a second tree look five frames later, and twelve fish frames per
level. Fish are tinted toward the water and translucent so they read as
submerged. Preview, write and check as for the foundation deposits:

```sh
python3 tools/artwork/paint_landscape.py --sheet artifacts/landscape.png [names…]
python3 tools/artwork/paint_landscape.py --sheet artifacts/water.png --ground terrain-water0 fish
python3 tools/artwork/paint_landscape.py
python3 tools/artwork/package_runtime.py --check
```

### Adding a built-in resource

1. Add the entry to `data/resources/registry.json` with a `requiredExperiment`
   (declare a new experiment in `experiments` if none fits). Only the eight legacy
   resources are ungated. New keys sort after the legacy slots, so they can renumber
   later gated resources in newly created maps; code must look resources up by key.
   Keep stone a static source: a finite or growing stone yield anywhere in the
   catalog makes stone mutable for every map (the `ResourceRegistry` suite checks this).
   A new yield on a terrain where no existing resource yields that material widens
   the material's habitat for every map, which is a simulation change.
2. Paint its frames and HD frames with a deterministic tool under
   `tools/artwork/`, run `package_runtime.py --check`, and credit the artwork in
   `docs/assets/source-attribution.md`.
3. Add `[<presentation name>]` to `data/texts.keys.txt` and every catalog listed in
   `data/texts.list.txt` (blank when untranslated), and list it in
   `data/texts.pending.txt` until every catalog translates it.
4. Run the `ResourceRegistry`, `BrushCatalog`, `TerrainResources` and
   `RuntimeResources` suites, `test/build_system/test_web_assets.py` and
   `data/check_translations.py --strict`.
5. Editing `registry.json` changes the simulation data hash and so the sim version
   key; follow [Simulation version](../multiplayer/turn-engine.md#simulation-version).

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

Setting `GLOB2_RESOURCE_STRESS_OUTPUT` to an output directory while running
`ResourceRuntimeBenchmark` with `test/run_tests.py --tag benchmark` also exports
custom-resource saves for CLI continuation. Exports include complete GUI save
state and idle AI controllers so every player supplies orders in headless runs.
They exercise resource growth and storage with sparse colonies, not an active AI
economy. Check full-window completion and worker-count checksum parity before
using them for end-to-end timing.

By default the historical performance gate uses paired confidence intervals:
aggregate CPU regression above 2%, or any scenario above 5%, exits with status 1;
intervals wholly within these limits exit 0, and inconclusive results exit 2.
Use `--report-only` for measured optimization work without enforcing these historical
limits. It preserves `performance_gate` diagnostics and confidence intervals in
`summary.json`, records `report_only: true`, and returns 0 for a completed, verified
campaign regardless of the threshold classification. Execution failures, changed
inputs and incomplete windows still fail. An interrupted runner kills and reaps
its current engine process before completing the input audit.
The former percentage gates are not completion requirements for optimization;
retain changes based on repeatable measured benefit and representative coverage.
These measurements do not replace same-candidate per-tick determinism across worker
counts and supported platforms, or exact save/load continuation.

Older maps and saves (before format 140) resolve their numeric resource IDs against
an immutable eight-resource import catalog compiled into the compatibility adapter.
They never consult installed resource JSON. Installed defaults pin those historical
keys to slots 0–7, regardless of authoring order; additional keys sort deterministically.
Custom resources and all resources in embedded snapshots retain their registry IDs.

## Related guides

- [Resource properties](resource-properties.md).
- [Terrain resource sets](terrain-resource-sets.md).

Related: [features and content](README.md).
