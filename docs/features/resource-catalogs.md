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
ore, iron ore, silica (a glass-yielding deposit, distinct from sand terrain) and
cotton are supplied as an optional editor experiment; stock generators, building
costs and service recipes do not use them. A second editor experiment,
`landscape-resources`, adds twelve map-making variants built only from existing
materials and rules; see [Landscape resources](#landscape-resources). New material inventory rows appear
only when a material is present in map sources, carried packets or inventory and
the building uses that material.

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
Growth opportunities are calculated from a completed-tick snapshot and published
eight ticks later by default. Replenishment accepts a matching resource type;
a positive increment can recreate an empty destination with configured initial stocks.
Spread can survive removal of its source. Destinations are revalidated at publication,
and accepted increments preserve intervening harvesting. See
[delayed growth](../development/reference.md#delayed-resource-growth).

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
   key; follow [Simulation version](../multiplayer/turn-protocol.md#simulation-version).

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

## Themed terrain and resource sets

The online **Terrain & resources** library at `/sets` holds flexible sets of custom
terrain, resource deposits, or both. A release includes definitions, PNG
spritesheets, frame grids and mappings, and attribution under CC0-1.0 or
CC-BY-4.0. Sets reuse the engine's existing material identities; they do not add
new inventory materials. Presets supply initial properties, and the web workspace
exposes property overrides, material yields, stock levels, variants and animation.
Advanced terrain colors, seams and decor use the engine's material declaration.

### Authoring and sharing a set

1. Open **Terrain & resources → Create a set**, enter its title, tags, license and
   creator credit, then add terrains or resources from presets. Each entry keeps a
   stable key within a release; changing its display name does not break updates.
2. Set the PNG frame width/height before **Upload PNG**. Terrain frames must be
   32×32; resource/decor frames can be up to 64×64. Select the uploaded image in
   the entry's **Spritesheet** control. Reupload identical image bytes with new
   dimensions to repair a mistaken frame grid, then check every frame mapping.
   **Remove sheet** removes unused images; change or remove entries that still
   reference an image first. Identical PNG bytes must use the same frame grid
   when combining sets in one map.
3. Click a tile in **Inspect sheet**, or type **Selected frame**, to choose a frame.
   This only selects a candidate: **Add selected frame** assigns it as a variant.
   Variant weights choose relative frequency; animation frame count, stride and
   ticks control the frame sequence. Resource stock levels select variants by the
   total stock across all materials.
4. Edit supported gameplay properties and material yields. Terrain Q8 fields use
   fixed-point units: 256 = 1; health fields express signed HP per exposed tick
   divided by 256. Resource rates use 196608 for one opportunity/probability one,
   with bounds described [in the field reference](#simulation-properties). Advanced JSON
   edits remain attached to their entry when switching entries; correct every
   invalid edit before saving.
5. **Save draft** keeps a private working copy. **Preview current changes** renders
   unsaved content locally in the browser. **Run checks & preview** saves and
   validates the exact revision for publication; a later edit requires new checks.
   Checks need an available set-validation engine agent on the instance.
6. Enter a release label, notes and visibility, verify credits/reuse rights, then
   **Publish this release**. Use **Create new release** for later revisions.
   **My sets → Load more drafts** reveals older working drafts. On a public set,
   **Inspect latest release** shows read-only entry properties before importing.
7. In the map editor, open **Set Library**, inspect an exact release, select its
   entries and import them, or download its package and import it from disk.
   Painting and local edits use the map's copied content. To update placed entries,
   explicitly choose **Replace placed entries** for another release; review the
   warning about replacing local edits and capping existing stocks.

The native CLI can check a downloaded or authored package without an online
instance:

```sh
glob2 --validate-set package.json --json report.json --preview preview.png
```

The report binds the exact package hash to its validation result; the optional
preview renders a contact sheet of up to 64 entries. An invalid package produces
`valid: false` with a reason and a successful command exit; invocation, input-file
and report-output errors produce a nonzero exit. Inspect the report's `valid`
field before treating a package as usable.

Drafts are private and saved with revision checks. Checks bind the exact serialized
package hash to an engine validation job. Saving again invalidates those checks.
Publishing creates an immutable release; further editing creates a new release.
Public sets appear in the library, unlisted sets are accessible by link, and private
sets are visible to their owner. Withdrawal or moderation blocks future downloads,
without changing copies already incorporated in maps.

The map editor's **Set Library** searches the current online instance and imports a
chosen release or selected entries. Required custom resources are included with
selected terrain. The **Import Terrain & Resource Set** menu action accepts a
bounded `.json` package from disk, including in the browser editor. Entries use
stable keys derived from their set and release UUIDs, so independently authored
sets and releases can coexist without colliding.

Each map retains its own editable copy. The library dialog's **Map content, credits
& local edits** panel edits the copied definitions and terrain material declaration,
with the same validation applied before publication to the map. Original credits
remain. Reimporting existing entries cannot silently overwrite those edits. To
update placed content, choose another release of the same set and explicitly select
**Replace placed entries**. Matching entry keys within the set move to the new release;
removed entries remain on the old release. Local changes to replaced entries are
superseded, and material stocks are retained up to the new capacities.

Format 144 stores a map-owned artwork bundle alongside the existing terrain and
resource snapshots. Only custom artwork and set attribution are bundled. Built-in
terrain and resource graphics continue to come from the installed game. Loading,
playing, sharing and replaying a map require no library lookup, creator account or
previously installed set. Scene snapshots retain the immutable bundle; graphics
objects are created and destroyed on the render thread.

Packages are limited to 16 MiB, 256 sheets, 2048×2048 pixels per sheet and 64 MiB of
decoded pixels in the combined map bundle. Terrain frames are 32×32; resources and
decor can use frames up to 64×64. Hashes, PNG decoding, duplicate JSON keys, depth,
paths, property bounds and effective animation/decor frame indices are checked.
Invalid imports leave the map's catalogs and bundle unchanged. Existing saves remain
readable at the durable compatibility floor; maps with new bundles require format
144. Format 149 also stores pending resource-growth work alongside vertex terrain
and scheduled building gradients; the durable save compatibility floor is unchanged.

### AI Terrain Studio

**Create with AI** opens `/terrain-studio`. Describe the terrain, resource deposits,
visual theme, and gameplay you want. A clear creation or revision request starts
one build affecting up to twelve entries; questions and brainstorming remain
conversational. An available terrain credit is required for discussion. Each
validated delivery costs one credit, including property-only revisions; confirmed
failures return the reserved credit. Publishing is a separate action.

Start fresh, choose **Edit with AI** on an owned unpublished draft, or choose
**Remix with AI** on a released set. Remixes preserve source credits and licenses.
Upload up to four selected PNG, JPEG, or WebP references to guide appearance;
references and generation artifacts stay private to the project owner. Use
artwork you have permission to reference.

The designer uses existing terrain capabilities and inventory materials. It can
change movement, construction, hazards, ecology, growth, clearing, and material
yields, but cannot invent engine mechanics or inventory materials. Ground gets
four texture variants; generated resources get three stock stages and two variants
per stage. Requested animation uses a gentle glow pulse, not articulated movement.
The default style follows the game's painterly artwork; explicit alternate styles
are supported subject to readability and technical asset limits.

The scene gallery uses the game compositor for isolated cells, narrow paths,
mixed boundaries, raised decor, resource stock stages, and selected animation
phases. Terrain variation selects another deterministic visual seed; resource
variation selects a frame within the stock level. Review the properties alongside
the artwork: passing import checks does not establish balance for every map.
The equivalent native preview is:

```sh
glob2 --validate-set package.json --json report.json --preview gallery.png \
  --gallery 1 --phase 0 --variation 0
```

Deliveries update the ordinary private set draft. Manual controls remain available
in the studio inspector and in the set workspace. Save manual edits before sending
a new AI request. If another client changes or publishes the draft during a build,
the validated result becomes a saved candidate; adopting it explicitly replaces
the current draft content. Candidates can also be downloaded. Complete normal set
publication to share a release or import the downloaded package into the map editor.
