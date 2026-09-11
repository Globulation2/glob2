# Adding a map generator

A generator is a registered module, not a new branch in a central dispatch switch. Its controls drive the editor, custom-game lobby, default initialization, range validation, mode history, stress runner and study catalog.

## Module boundaries

- **`core/`** owns requests, control metadata, registration, seed streams, validation and results. It does not implement terrain recipes.
- **`generators/`** contains one header/source pair per generator. The header defines its named options; the source owns its controls and ordered generation steps. Generator-specific terrain and resource routines remain local to that source.
- **`shared/`** contains height fields, instance-owned noise, terrain classification, region geometry, distance fields, resource filling and starting-position helpers. These are ordinary functions with explicit map/context dependencies, not methods of a generator superclass.
- **`compatibility/`** owns the historical descriptor codec and overloaded-field conversion. New generators do not add fields to that descriptor. `MapGenerator.h` is only a small compatibility facade for older callers.

`Map` retains general terrain-editing operations, including `rebuildTerrain()` for bulk undermap edits. `Game` retains team, unit and building mutation APIs. Neither dispatches generation algorithms.

## Register a module

1. Add `ExampleGenerator.h/.cpp` under `src/map/generator/generators/`. Use a named options struct that reads its scalar values from the request by stable option ID.
2. Return a `GeneratorDefinition` from `exampleDefinition()`. Provide a stable string ID, unique nonnegative numeric ID, translation key, revision, editor-only availability, control list and callback. Numeric IDs are identifiers, never list positions; gaps are supported. `hasStartingColonies` defaults to true. Uniform alone disables normal starting-colony validation in the shipped catalog.
3. Add that definition to the single list in `GeneratorRegistry::builtins()` and its source to `src/SConscript`. Add the display name and control labels to the translation tables using the existing translation workflow.
4. Write the callback as `bool generate(Game&, GenerationContext&)`. Read the immutable request, build into the supplied fresh game, set `context.stage` before operations that may fail, and return false with a useful `context.detail` if the layout cannot be placed.

Each `GeneratorControl` defines `id`, translation label, minimum, maximum, step, default, group, power-of-two display formatting, whether the value participates in terrain weighting, and its `kind`. The all-zero-weight check reads this metadata, not generator-specific option names. IDs must be unique within a generator and cannot shadow shared controls. For irregular integer domains, supply a sorted, unique `allowedValues` list such as `{4, 8, 16}` with matching minimum and maximum. These are literal stored values; `powerOfTwo` instead formats stored exponents. Use `values()`, `indexOf()`, `valueAt()` and `displayValue()` in consumers, never selection-index arithmetic. Use `GeneratorControl::set()` for UI normalization. The service rejects malformed requests instead of silently changing their settings.

A control's `kind` is `ControlKind::Range` unless it is an on/off switch. Declare a switch with `GeneratorControl::toggle(id, label, on, group)`: it stores 0 or 1, the lobby draws it as a checkbox row (click it, or press Space or Return while it has focus) and the editor as a check button, and the registry rejects a toggle with any other domain, step, allowed values, power-of-two formatting or terrain weight. Every 0/1 option is a toggle. A switch should change how a map feels without breaking its invariants: the map must still generate and pass the generator's own `validateWorld` in both states.

Declare resource amounts with `GeneratorControl::percentage(id, label, maximum)`: 0 to `maximum` (300 unless given) in steps of 25, defaulting to 100, in the Resources group. The percentage scales the numbers that already decide that resource's amount — densities, clump sizes and counts, noise or fertility thresholds — and 100 must reproduce the unscaled map exactly: use `MapGeneration::scaledCount()` and `scaledShare()` from `shared/Resources.h`, which return their input unchanged at 100, and draw nothing from a random stream at 100 that the unscaled code didn't. Decide deliberately which placements are fairness guarantees (starter kits, reachability backstops, 1:1 guaranteed wheat and wood) and leave those unscaled, so every setting stays playable. Keep existing control ids and meanings; a generator that already exposes explicit amounts (Maze) keeps them.

Lobby preferences save a control through its legacy descriptor field when it has one (`wheat`, `lake-size`, ...) and in the preferences' `options` section otherwise, so new controls persist with no further work.

Shared settings are width and height exponents, colony count, starting workers and background terrain. The request's `options` map contains only controls for its selected generator. Generator-specific structs give these values meaningful names such as `lake_size`, `channel_width` and `bridge_width`; there is no reuse of unrelated descriptor slots.

Switch modes with `GenerationHistory::select()`. It remembers options per stable numeric ID and carries the shared controls between modes. Do not add UI-specific defaults or range tables.

## Generation, randomness and failure

Choose the generation sequence appropriate to the map's character. Modern height-field modes classify terrain, choose starting locations, add resources, then place colonies. Legacy modes place colonies before their final resource pass. Concrete Islands and Isles deliberately interleave region division, resources and starts. Shared helpers do not impose a universal pipeline.

Use `context.stream("name")` for a per-attempt `std::mt19937` stream, or `context.bounded("name", count)` for a rejection-sampled bounded choice. Seed derivation is defined in `GenerationContext.cpp`: unsigned FNV-1a mixed with the root seed, followed by a fixed integer avalanche. Do not use `std::hash`, wall-clock reseeding, `rand()`, `srand()` or the gameplay RNG in generator algorithms. Noise tables and stamp caches belong to each height-map instance.

General engine mutation functions still consume the synchronized gameplay RNG. `GenerationService` seeds and restores it with an RAII scope. This bridge is synchronous; this refactor does not make `Game` generation thread-safe. Independent processes can generate concurrently.

The same seed, generator revision and settings reproduce a world within this implementation on the same platform. Historical seeds intentionally do not promise the same maps, and cross-platform floating-point identity is not a supported contract. Bump a generator's revision when its output changes deliberately after this framework lands.

`GenerationService::generate()` returns generator ID, revision, seed, stage, error code and detail. It validates requests before world mutation and verifies dimensions, colony counts, starting swarms and worker counts afterward. The quantitative start proxy is an offline diagnostic, not an automatic terrain repair rule.

A failed candidate may contain partial terrain or colonies. Discard it; never reuse it as another attempt or publish it. The lobby creates a fresh game for each of its five attempts, records derived seeds and launches the successful serialized preview. The editor similarly only opens a successfully generated fresh game.

## Shapes, regions and constrained starts

Use `shared/Geometry` for transformed radial shapes and label-grid stamping. Supply a named context stream; query `maximumRadius()` when budgeting space around the shape and account for transform stretch. Shape rasterization has explicit clipped/wrapped behavior and does not guarantee connectivity after other edits.

Use `shared/Topology` for graph distances, sparse-label adjacency and passable-grid components. Choose cardinal or eight-neighbor adjacency and wrapping explicitly. These are geometry tools, not a substitute for the engine's unit pathfinding rules. Construct the appropriate passability mask for the invariant being checked.

`Regions::splitUpPoints` keeps its existing local search by default. Whole-region weighted search is optional, bounded, and does not promise a global optimum. Weights travel with points during the final shuffle. For a single site, its spacing budget is half the shorter map dimension. Weights are positive integers up to 10,000.

`placeSettlement` takes an explicit home mask and preferred building anchor. It checks the entire swarm footprint, places every requested worker, records the start and returns a diagnostic on failure. Create teams and bake the terrain first. Keep resource policy in the generator, before or after settlement as appropriate. Discard failed candidates even if a swarm has already been placed.

Add optional pure `validateRequest` and `validateWorld` callbacks to the definition for relationships between settings and generator-specific invariants. Return an empty string on success or useful diagnostic detail on failure. Request validation is shared by the editor, lobby and service. Final validation runs after the common structural checks. Validate the finished terrain when later carving, resources or buildings can alter connectivity; never silently change the request to satisfy an invariant.

[Map generator framework](MAP_GENERATOR_FRAMEWORK.md) describes the shared building blocks, the generator catalog, and how resource placement and colony fairness currently work.

## Compatibility and verification

Old numeric IDs and descriptor bytes remain stable. The adapter resolves historical lake/channel/bridge sentinels and resource amounts; save files and replay files continue to contain full worlds. New options have no implicit legacy encoding.

Build and run the contract checks:

```sh
scons release=1 -j12 map-generator-study map-generator-defaults-test custom-setup-test
build/src/MapGeneratorDefaultsTest glob2-generator-contracts
build/src/CustomGameSetupHarness
```

The defaults test injects a test-only generator at ID 101, constructs its actual editor controls, edits its custom option, and generates a map. It also checks defaults, ranges, mode memory, codec roundtrips, legacy sentinels, interleaved repeatability, gameplay RNG restoration and structured failures.

Run all registered playable defaults without maintaining another generator list:

```sh
python3 tools/map_generator_study.py --count 1000 --start 20001 \
  --output artifacts/map-generator-refactor --label validation
python3 tools/plot_map_generator_refactor.py \
  artifacts/map-generator-refactor/validation.csv --baseline artifacts/baseline/summary.json
```

For targeted settings, pass a JSON configuration list with `id`, `method` and a `params` object whose keys are stable control IDs. A sample uses registered defaults plus those overrides. Out-of-range values fail explicitly. `--binary` selects a separately built historical executable for comparison.

Before accepting a generator or structural refactor, inspect fixed-seed previews and poor-performing examples, exercise range endpoints and crowded/rectangular maps, and investigate changes beyond the documented statistical thresholds. Keep generated evidence under ignored `artifacts/` and summarize findings in the pull request. The comparison plotter requires an explicitly supplied baseline study summary.
