# Maps verification

Focused regression scenarios and commands. Start with the [native test guide](../../../test/README.md) for building, isolation and runner selection.

## Terrain resource regression

The `TerrainResources` suite (`python3 test/run_tests.py --filter 'TerrainResources/*'`)
links the actual client objects and exercises terrain edits and resource clearing for all eight
resource types, all three base terrains, overlapping strokes, and all four
wrapped map corners. A whole-map oracle checks both removal and preservation.
These are headless map-operation tests; they do not drive editor mouse events.

## Map generator golden maps and colony sweep

From the repository root, `scons --build=build/native-tests -j8 release=1 map-generator-golden-test` builds
`build/native-tests/src/MapGeneratorGoldenTest`. Run it as `./build/native-tests/src/MapGeneratorGoldenTest <profile>`
to compare this platform's rows of `test/map-generator-golden.txt` against fresh rolls, with
`--update` after a revision bump, `--print` to bootstrap a platform's rows from a log, and
`--sweep` to roll every playable landscape at the lobby's colony counts and sizes. A platform
with no rows reports and passes, so a new machine can run the check before its rows exist;
`--require-rows` makes that a failure instead, which is what CI runs, so the table must carry
rows for every platform running that check in CI for the selected platform. The current
table records vertex terrain (save format 146): every generated map changed when terrain
moved to map vertices, without individual generator recipe revisions. The complete
pre-resource-epoch table, including historical `macos-arm64` rows, is retained in
`test/fixtures/map-generators/pre-resource-epoch-golden.txt`. The current table includes
native macOS arm64 rows; Linux hashes must not be used to bootstrap macOS coverage.
Golden hashes establish behavior for their retained inputs. Compare topology separately when a serialized-format or resource-state change moves a complete map hash. See the [generator framework](../../map-generators/map-generator-framework.md).

`MapGeneratorGoldenTest <profile> --telemetry` compares telemetry enabled/disabled and repeated
attempts for all registered generators at three seeds, including complete serialized worlds and
RNG restoration. It prints generation-only timings and record counts; timing is diagnostic, not a
flaky performance threshold. `python3 test/test_map_telemetry.py` tests the bulk collector's failure
retention and aggregation. The existing JSON-report test covers typed telemetry, malformed reports,
service failures and raw invalid requests. See [telemetry](../../map-generators/telemetry.md).

## Orchard Commons fruit conversion

Runs as the `OrchardCommons` suite of `glob2-engine-tests`:

```sh
python3 test/run_tests.py --filter 'OrchardCommons/*'
```

Saved fixtures land under `artifacts/tests/OrchardCommons/`.

The harness places competing inns on unmodified generated terrain and checks superior
advertised fruit variety, equal friendly diet, advertising off, lost fruit and lost
wheat using real game ticks. It also checks full-save generation repeatability,
telemetry neutrality, terrain/resource save-load preservation and invalid requests.
Saved fixtures retain each scenario. Loading rebuilds the existing conversion cooldown;
the test explicitly matures eligibility after loading, not immediate continuation.
CI runs this suite on Linux and Windows and retains the fixtures.

For generation-only timing at 512×512/eight teams, run the opt-in benchmark case with
`python3 test/run_tests.py --tag benchmark --filter 'OrchardCommons/*'`. It skips the
scenarios and reports separate telemetry-off/on means and failure counts; it is a local
profiling tool, not a timing threshold in CI.

## Map generator profiling fixture

`scons --build=build/native-tests -j8 release=1 map-generator-profile-fixture` builds
`build/native-tests/src/MapGeneratorProfileFixture <profile-dir> <seed> <rounds>`, which round-robins every
registered generator for `rounds` passes with parameters (shared and generator-specific) drawn
at random the same way `GenerationRequest::randomizeControls` does, and prints a per-generator
attempt/success/timing table. It defaults to registered search domains; pass
`--domain=legal` to sample full experimental domains, or `--domain=search` explicitly.
For durable stress evidence, `--jsonl=/absolute/path.jsonl` records every attempted request,
seed, outcome and generation timing. Add `--report-every=25 --telemetry` to retain periodic
full per-colony map reports and every failure's structured diagnostic. Report analysis is
outside the recorded generation timing. Unsupported shared settings that cannot yield an
accepted parameter roll contribute no attempt; check each generator's actual attempt count.

It exists to give an external sampling profiler (macOS `sample`,
Linux `perf record`) a sustained, representative mix of real generation work to attach to; run it
with a large round count in the background and sample its PID. It is not a regression test (see
`--sweep` and `--performance` above for that) and is not wired into CI, but a fixed seed and round
count also make it a quick way to compare a shared primitive's before/after cost: attempt/success
counts per generator repeat exactly across a behavior-preserving change, so a diff there is a
signal something changed, not just an optimization's timing.

## Map subclass test pattern

Pattern used by `MapQueryTest.cpp`. Lets you write tests against `Map`'s predicates with a minimal link surface — no `globalContainer`, no real `Sector` array, no transitive pull of `Bullet` / `Team` / `Building` / `Unit` into the test binary.

### The fixture: subclass `Map`, bypass `setSize`

`Map::setSize()` does `new Sector[sizeSector]`, which forces the linker to resolve every `Sector` method (vtable + transitively `Bullet`, `Team::pushGameEvent`, `Building::kill`, `Unit::getRealArmor`, `globalContainer`, ...). Bypass it:

```cpp
struct GrassMap : Map {
    GrassMap() {
        wDec = 3; hDec = 3; w = 8; h = 8;  // 8x8 map
        wMask = 7; hMask = 7;
        size = 64;
        resourceCells.assign(size, {});    // no resource, no building, no unit
        occupancyCells.assign(size, {});
        areaCells.assign(size, {});
        scriptAreaCells.assign(size, 0);
        vertexTerrain.assign(size, GRASS); // one terrain per vertex
        bindBootstrappedArrays();
        rebuildTerrainCounts();            // compile the cell rules
        // No Sector or auxiliary arrays are allocated.
    }
    ~GrassMap() {
        // Reset fixture dimensions before base cleanup.
        w = h = wMask = hMask = wDec = hDec = 0;
        size = 0;
    }
};
```

`w` / `h` / `wMask` / `hMask` / `wDec` / `hDec` and `arraysBuilt` are public on `Map`; the cell arrays are private, so `MapQueryTest.cpp` (the complete fixture, which also loads a resource registry) builds with test-only private access.

### Stubs for `Sector`

`test/unit/stubs/MapSectorStubs.cpp` (linked into every unit binary through `UNIT_STUBS` in `test/tests.py`) provides empty bodies for `Sector::Sector(Game*)`, `Sector::~Sector`, `Sector::setGame`, `Sector::step`, `Sector::save`, `Sector::load`, `Sector::free`, and `UnitDeathAnimation::UnitDeathAnimation`. `Map.o`'s compiled `setSize` / `setGame` reference `Sector` symbols even though the test never calls them. ~30 lines of stubs avoid pulling all of `Sector.cpp`'s real deps.

### Build wiring

Add the translation unit to `UNIT_TESTS` in `test/tests.py`. The production sources the unit binary links (`Map.cpp`, `MapQuery.cpp`, `MapTerrain.cpp`, `BitArray.cpp`, `Utilities.cpp`, `building/BuildingUtils.cpp`, `unit/UnitUtils.cpp`, ...) are listed once in `UNIT_PRODUCTION_SOURCES`; the include paths come from the client build environment, so nothing per-test is needed.

### Terrain encoding for tests

Terrain is stored per vertex: vertex (x,y) is the top-left corner of cell (x,y), and a
cell is wholly one terrain only when all four of its corners are. Use
`Map::setVertexTerrain(x, y, type)` for one vertex, `paintVertices(vertices, type, false)`
for a set without beaches, and `assignVertexTerrain` or `fillTerrain` for a whole map.
A lone vertex makes the four cells around it mixed. Batch larger edits with
`auto batch = map.editTerrain()` to invalidate derived fields once. Test gameplay with
`terrainPropertiesAt`, not with terrain IDs.

### When to use this pattern

- Testing other Map behaviors (`doesUnitTouch*`, `doesPosTouch*`, `setClearingArea*`, `markImmobileUnit`, etc.).
- Adding regression tests around any Map state mutator before refactoring it.
- **Don't use** for behaviors that genuinely need real `Game` / `Team` / `Unit` / `Building` wiring (e.g. `doesUnitTouchEnemy` reaches into `game->teams[]->myBuildings[]`) — those need either a different stub set or a refactor to decouple first.

## Map repetition regression

```sh
scons -j8 release=1 tests
python3 test/run_tests.py --filter 'MapTiling/*'
```

The registered engine cases repeat a compressed source map, verify its buildings,
units, seam-wrapped forbidden/guard/clearing areas and clearing flag settings, and
write and reload an ordinary map in the profile's `generated/` directory. They
also cover invalid repeat factors, scripted-map refusal, automatic player counts,
equal shares and the editor-only 32-tile size. `UIPresentation` includes the
advanced repetition dialog across desktop/touch viewports, safe insets and text sizes.

## Map CLI

Build the normal client with `scons --build=build/native-tests release=1`, then run
`python3 test/test_map_cli.py build/native-tests/src/glob2` (use `.exe` on Windows).
The test uses a disposable profile and the shared `MapPreview` software renderer
with an invalid video driver, proving PNG export requires no display.
It compares explicit CLI settings against a config with CLI overrides,
generated versus loaded map pixels, default/explicit 2×, 4× and 8× scales and preview sizes, loads a premade map and
three checked-in saves, checks invalid arguments and output failures, and verifies
inputs/preferences are unchanged. PNGs, command logs and hashes are retained in
`artifacts/map-cli/` and uploaded by Linux CI. Windows CI also runs the full suite without a display. The optional
`--generation-only` subset compares serialized maps from config/CLI settings
and checks invalid settings and preferences.
See [map CLI documentation](../../map-generators/cli.md).

### Whole-game diagnostics

`python3 test/run_tests.py --filter 'GameDiagnostics/*'` covers release-active
field parsing, full unsigned food values, capture cadence, shared-team controllers, bounded Scene export,
repeated graphics lifetimes, checked output failures, and software/portable/GL
state restoration. Display cases run through the registry's isolated processes.
`python3 test/test_game_diagnostics.py [client-binary]` exercises the production CLI,
retaining commands, PNGs, saves and per-tick checksums under `artifacts/map-cli/diagnostics/`.
It compares disabled/fields/PNG/threaded runs, save continuation, malformed arguments,
and output failures. The map CLI suite invokes it on
native platforms; `--generation-only` continues to skip graphics exports.

### Flat map images

`python3 test/test_map_image.py [client-binary]` tests the optional image importer
and exporter without display or network access. It uses only the Python standard
library and retains command logs and fixtures in `artifacts/map-image/`.
Checks cover every resource type, bounded mature resource amounts, implicit terrain,
offset wrap contours, legal resource-budget preservation and seam stitching,
post-shore seam/corner agreement, protected seam-crossing start markers,
resizing in both directions, deterministic initialization, four starting workers,
save/load, dropped shoreline resources, invalid images and colony counts, and
resolved gzip input/output collisions.

Linux and Windows CI run the native conversion suite.

### Map JSON reports

Build `scons --build=build/native-tests release=1 map-report-test`, then run
`python3 test/test_map_report.py build/native-tests/src/glob2 build/native-tests/test/MapReportHarness`
(add `.exe` to both binaries on Windows). The suite runs without graphics, checks
the [published report contract](../../map-generators/report-format.md), recomputes fairness
formulas, and uses analytic maps to check wraparound, disconnected islands, algae
blocking swimming, resource amounts and construction space. It verifies unchanged
serialized state and simulation RNG, deterministic reports, config provenance,
older saves, and output errors. Reports and commands are retained in
`artifacts/map-report/` and uploaded by CI. The PNG CLI suite also exercises all
three outputs together.


### Distributed map telemetry

`python3 test/test_distributed_map_telemetry.py` checks map-weighted aggregation,
repeated subjects, typed values, missing/truncated traces and cohort separation.
The main CLI integration suite also compares complete native and structured map
reports, including generation-failure telemetry. The opt-in
`test/distributed_map_telemetry_integration.py --hosts HOSTS.json --output NEW_DIR`
checks complete result/artifact roundtrips and offline record counts on every host;
add an absolute registered `bundle` path to each host entry. It stops its workers
after collection. Retained validation is linked in the tournament validation guide.
The team-statistics harness also checks the AI telemetry schema interface for every
built-in implementation, shared-team player identities, controller generations,
reassignment, exact numeric persistence, replay availability, and truncated fields.
See [AI telemetry](../../ai/telemetry.md) for the capture/extension contract.

`AITelemetryUI` exercises the real telemetry dialog with Maxima's full schema and
the maximum presentation field count, using counted measurements and recorded
painting to check that layout work stays bounded. It also covers selection,
search, refresh state, and access revocation. `TextMetrics` verifies that font
measurement creates no bitmap-cache misses, retains rendered text, matches styled
UTF-8 raster dimensions, bounds its separate cache, and clears it on font reload.
The display case `AITelemetryUI/telemetry full-game*` reports opening, sampled
refresh, scrolling, and search timings for a saved Maxima game and a presentation
stress fixture, and retains desktop/phone captures. Run it alone on a quiet machine
for timing evidence. `GLOB2_TELEMETRY_BENCH_SAVE` selects the exact initial save for
paired revisions; `GLOB2_TELEMETRY_BENCH_TRACE=1` emits per-tick checksums outside
the measured UI work. Its eight-sample p95 is the largest observed sample;
timing targets are review criteria, not assertions in CI.

## Runtime terrain

`TerrainRegistry/*` validates JSON, immutable imports, deterministic IDs, bounded
cost profiles, maximum registry size, canonical serialization, snapshot string
lifetimes and visual-profile deduplication. Scalar/SIMD queue results are compared
against a heap oracle; invalid or undersized queue requests must fail before
changing the field, while unused slow definitions must remain harmless. `TerrainRuntime/*` covers map isolation, match immutability,
capability summaries, custom movement, resumed/worker gradients and embedded save
continuation. The production pipeline case dispatches through `Map::syncStep` with
serial and worker execution, checks both binary water and weighted profile capture,
and reimports before publication to reject a pending field from the old registry. Run these with `python3 test/run_tests.py --filter 'TerrainRegistry/*'`
and `python3 test/run_tests.py --filter 'TerrainRuntime/*'`. Also run existing
terrain, gradient, save, replay, scene and editor suites when changing this boundary.
The scalar kernel can be compiled explicitly with `GLOB2_GRADIENT_SCALAR`; NEON
requires an ARM build. Native success alone does not establish cross-platform
checksum equivalence or performance qualification.

The custom-map cases in `TurnEngineHarness` and `LanMatchHarness` exercise shared-map
loading, content-hash transfer, per-tick agreement and match verification without
local authoring files. `EditorActionCoverage` exercises file selection, failed import and retry, palette
scrolling/selection, cancellation and save/load without author JSON on both layouts,
and captures screenshots. `TerrainPresentation` covers software and GPU
registry/asset invalidation, plus explicit edge-mask expectations for custom aliases
at wrapped map boundaries. Cache-versus-direct pixel equality alone is insufficient:
both paths can share the same wrong layer description.

### Building catalog composition and performance

`BuildingCatalogFixtures` loads retained manifests under
`test/fixtures/building-catalog/composition/`; it never regenerates definitions at
runtime. The seeded combinations exercise mixed services, split recipes, shared
stock, rectangular overlays and missing capabilities. Per-tick save continuation,
resource conservation and retained custom-rule traces complement the focused
`BuildingCatalog`, `BuildingServices` and `BuildingProductionCombat` suites.
`BuildingAreaEffects` compares cached coverage with an independent evaluator and
covers pulse services, combat, lifecycle transitions, growth snapshots and save
continuation. `BuildingAreaEffectsBenchmark` is opt-in (`--tag benchmark --filter
'BuildingAreaEffectsBenchmark/*'`); it separates stationary coverage, funding
pulses, dirty rebuilds and memory across map/team sizes. Its populated-match
fixture measures one team with healthy, nonhungry workers and walls, rather than
combat or active resource growth.
`AICustomCatalog` checks actual replacement-provider selection and split-production
orders across the native controllers. These custom traces do not establish stock
behavior parity.

`BuildingGradientBenchmark` is opt-in (`--tag benchmark --filter
'BuildingGradientBenchmark/*'`). It measures actual building/resource field
preparation and a fixed stock simulation without AI decisions. The source compiles
unchanged against the pre-catalog engine for paired measurements. Kernel rows
include input dimensions, repetitions, iteration counts and output digests;
simulation rows retain endpoint unit/building counts, health and inventory.
Run matched release toolchains one process at a time on an otherwise idle host.
Timing thresholds are evaluated from retained interleaved runs, not asserted in CI.

`BuildingCatalogBenchmark` separates catalog setup from steady simulation with
55, 256 and 1,024 definitions, keeping live entities fixed. Its private supply
routing workload reports cold, warm and depletion passes below, at and above the
cache budget, including retained cell bytes. These custom-catalog measurements
complement the unchanged-source stock comparison; they have no historical
baseline equivalent.

### Runtime resource stress components

`ResourceRuntimeBenchmark` is opt-in (`--tag benchmark --filter
'ResourceRuntimeBenchmark/*'`). It holds map geometry fixed at 256² and 512² with
eight teams, comparing one resource definition against 512 equivalent definitions
and a 512-definition variant whose deposits yield three materials. Sparse material
coverage must allocate fields only for materials present; equivalent definitions
must preserve the query/field digest. No building recipes are changed.

The `GLOB2_RESOURCE_STRESS` JSON rows separate definition installation, placement,
field requests, source scans, mutations, seed refresh and growth. Growth uses a
configured nonzero uniform rate in three batches of 16 passes, after the matched
query phases, with stock digests verifying equivalent-definition behavior. They include allocated
material fields, shared absent fields, preparation and consumer cache bytes, and
stock index/sidecar/free-list capacities. Set `GLOB2_RESOURCE_STRESS_OUTPUT` to an
ignored artifact directory to save `report.json` and six binary games with eight
small colonies for additional CLI continuation. Colony creation and serialization
occur after the measured phases. This is a bounded component benchmark, not a
statistical gameplay comparison or proof of no regression against the old engine.
Run it on an otherwise idle host, separately from builds and tournaments.

The five explicit-design generator regressions use
`test/map-generator-resource-epoch.json` to separate topology from initial stock.
Resource epoch 1 removes resource-sprite draws from the simulation RNG; historical
full hashes are retained, while new full hashes include the resulting stock
quantities. Topology hashes still cover every underlying/render terrain tile,
resource identity/location, and colony start. A stock-only change cannot silently
approve a changed route or deposit layout.

To record an epoch row, first generate the five historical explicit designs with
the archived engine (256², four teams, seed 1: maze `cell-shape=0`, fingerprint
`pattern=0,barrier=0`, canals `block-shape=0`, caravanserai `desert=1`, honeycomb-isle
`block-shape=1`). Keep their `.map.gz` files under their generator names in an
ignored artifact directory. Set `GLOB2_RECORD_RESOURCE_DESIGN_GOLDENS` to that
absolute directory and run the `MapGeneratorDefaults/Explicit designs*` test.
Recording verifies the historical full hashes and compares topology before writing
an `epoch1-<platform>.json` artifact. Review every comparison before copying rows
into the fixture. Record other platform/compiler variants on those actual builds;
unavailable full hashes produce an explicit unverified warning. The four designs
whose historical full hashes matched across platforms retain portable topology
checks; Fingerprint's known platform variant requires its own recorded topology.

### Generator fingerprint verification and observations

`python3 test/collect_generator_evidence.py collect BINARY OUTPUT --platform macos-arm64`
reads `MapGeneratorGoldenTest --inventory` and runs `--print` twice in fresh profiles.
The request enumeration is shared with generation, including extended team counts
without accepted fingerprints. Collection requires every committed Linux reference
key to remain in that inventory and every requested row to be observed exactly once;
it rejects missing or extra rows and changed revisions. It retains
raw logs, normalized rows, checkout provenance, host compiler, stable binary hash, and unchanged
current/historical fixture hashes. Collection does not update expected output or
replace the strict `--require-rows` gate. The macOS validation job runs that gate
against its accepted platform rows before collecting observations; both checks
and the five explicit-design checks are attempted even if another fails. Missing
rows and full fingerprint changes fail verification. It uploads these
observations as `generator-observations-macos`. The collector cannot infer the
binary's build inputs from the checkout; its manifest marks that association
unverified. Retain the producing build job and compiler/flags/dependencies when
reviewing the observation.

Two processes on one host establish repeatability only. Compare artifacts from
independent jobs at the same source with
`python3 test/collect_generator_evidence.py compare FIRST SECOND`; differences
require investigation before acceptance. Agreement still does not establish
historical topology preservation: independently verify against archived pre-epoch
maps before adding new expected rows or the resource-epoch design hashes.

The macOS job also runs the five explicit-design resource-epoch checks separately
and retains `resource-design-observations.json` with every actual full and topology
hash. Existing accepted-platform and portable-topology assertions still run;
unverified full hashes are marked as observations, and the record distinguishes
which references exist. The existing `GLOB2_RECORD_RESOURCE_DESIGN_GOLDENS`
archived-map comparison remains available for independent historical validation.
