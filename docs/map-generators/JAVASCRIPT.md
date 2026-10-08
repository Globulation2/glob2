# JavaScript map generators

A custom generator is a local package whose synchronous JavaScript module composes
Globulation 2's existing native map toolkit. Generated worlds are ordinary maps;
playing, saving or transferring one does not require the generator package.
Native generators retain their IDs and remain the default catalog.

## Author and run

An authoring directory contains `manifest.json`, `generator.js`, and optional `.js`
modules imported with relative paths. Imports cannot leave the package. Each launch
freezes those files into an immutable package; relaunch after editing to reload it.

```sh
build/linux/client/release/src/glob2 --generator-package data/generators/examples/swamp \
  --generate-map examples:swamp --seed 91 --width 256 --height 256 --teams 4 \
  --output artifacts/swamp.map --preview artifacts/swamp.png --json artifacts/swamp.json \
  --export-generator-package artifacts/swamp-generator.json
python3 tools/map-generators/package.py data/generators/examples/swamp artifacts/swamp-generator.json
```

The repeatable `--generator-package PATH` launch option accepts a directory or a
portable JSON package. It works with the map CLI and normal headless modes. Installed
packages load when starting the interactive game. Explicit launch packages provide
that launch's catalog. The standalone native study harness has no package loader;
use the game's headless catalog and generation modes for scripted studies.
`tools/map_generator_study.py --generator-package PATH` freezes packages and attaches
them to each study job; namespaced IDs and package inputs also work in tournament
`generate_map` and inline `game` jobs.

In **Settings → Map generators**, import a portable package, explicitly replace an
installed package of the same ID, export its frozen bytes, or remove it. This uses
the normal desktop/mobile file picker and browser upload/download interfaces.
Library changes become active after storage persistence succeeds. In-progress
requests retain their original package snapshot. Packages appear in the local game
landscape picker and the map editor; online server-controlled generation uses the
native catalog. To share a scripted landscape online, generate and share its map.

## Manifest and portable format

```json
{
  "id": "author:landscape",
  "name": "My landscape",
  "apiVersion": 1,
  "revision": 1,
  "entry": "generator.js",
  "tags": ["terrain:natural", "feature:lakes", "style:wide-open"],
  "controls": [{
    "id": "water", "label": "Water", "group": "terrain",
    "kind": "range", "minimum": 0, "maximum": 40, "step": 5,
    "default": 10, "searchRange": [5, 25]
  }]
}
```

IDs must be namespaced lowercase ASCII strings. Native numeric IDs are reserved;
custom numeric handles exist only within a running session. Preferences store the
string ID. Missing packages fall back to a native landscape. Optional manifest
fields are `description`, `author`, `editorOnly` and `hasStartingColonies`. Editor-only
blank terrain generators may omit colony placement. The engine still validates the
finished world.

Controls use the native range/toggle/choice contracts, capped at 4096 legal values
and 256 named choices. Declare exactly one bounded
`searchRange` or explicit `searchValues` per control. Choice controls use `choices`
and a numeric default index; toggles use a zero/one default. `terrainWeight` marks
weights that must not all be zero. `powerOfTwo` displays a range's values as powers
of two, as with repeat controls. Shared size, colony and worker controls are supplied
by the engine and cannot be overridden by a manifest.

Optional `translations` maps language codes to dictionaries of original label →
translated label. Names, control labels and choice labels use these dictionaries;
missing translations display the original text.

A portable package wraps the manifest as
`{"formatVersion":1,"manifest":{...},"modules":{"generator.js":"source",...}}`.
The runtime accepts at most 4 MiB and 128 modules. Directory symlinks are rejected.
The installed library accepts at most 128 packages and 64 MiB.

## Callbacks and toolkit

```js
export function generate(c) {
  const terrain = c.mask(c.torus.size(), 2); // grass vertices
  c.stage("terrain");
  c.toolkit.Sketch.writeVertices(terrain);
  c.addTeams();
  if (!c.toolkit.Pipeline.settleColonies("homes", () => terrain,
      team => ({x: 32 + team * 32, y: 32}))) return "Cannot settle";
  c.toolkit.Pipeline.secureStartingCrops(c.torus);
}
export function validateWorld(c) {
  return c.toolkit.Pipeline.startingFloorFailure(c.request.teams);
}
```

`generate` is required. Optional `validateRequest` and `validateWorld` return a
specific diagnostic string to refuse the request/world. Undefined, null or an empty
string mean success. Every callback has its own disposable runtime; module state
is not shared between callbacks or candidate seeds. Validators have read-only world
access. Request validation has no world. Package inspection runs module initialization
without world or RNG access. Promises and asynchronous initialization are rejected.

`c.request` is a frozen object with `width`, `height`, `teams`, `workers`, `seed` and
frozen `options`. Native parameters `Game`, `Map`, `GenerationRequest`,
`GenerationContext` and `GenerationTelemetry` are injected; omit them from JavaScript
calls. Other arguments retain their C++ order and defaults. Mutable scalar references and scalar output pointers
return a record containing their updated values and `result` for non-void native
returns; pass their initial values in the native argument order. Optional pointers
accept null; omitted output pointers return null in the corresponding result field. Mutable vector
references require buffer handles. Toolkit families use
header names, for example `Grid`, `Drawing`, `Growth`, `Planting`, `Homes`, `Solve`
and `Pipeline`; legacy helpers use `Legacy` prefixes.

The generated [TypeScript declarations](../../data/generators/toolkit.d.ts) describe
factories, record fields, overloads and callback signatures. The generated
[coverage inventory](../../src/map/generator/javascript/ToolkitCoverage.json) records
native symbols and the explicit script equivalents of C++-only templates.
Records with constructors use family factories. Simple records also accept object
literals. Native vectors are checked buffer handles: `length`, `get`, `set`, `fill`,
`clone` and `toArray`. APIs accepting vectors also accept JavaScript arrays. Native
buffers passed into callbacks expire when that callback returns; clone data that
must be retained. Call operators are exposed as `at`; objective iteration is
`terms()`. Pure toolkit operations are available during request checks, while world
mutation is restricted to generation.

Context helpers supply named RNG streams (`stream(name).next()`, `bounded(name,n)`),
array shuffling, invocation-local design caching, seeded feasible-choice selection,
team creation, resource placement and buildability queries. `resourceType(key)`
looks up a resource in the world's frozen catalog, including landscape additions;
`setResource(tile,type,brushSize)` uses the native placement brush. A zero brush
places one tile; its size must be smaller than both map dimensions. `buildingType()` supplies
the starting building type handle for legacy placement helpers; its optional name,
zero-based level and construction flag select another catalog type. `stage`, `measure`,
`choice` and `fallback` record the normal bounded generation diagnostics/telemetry.
`Math.random()` uses its own named seeded stream. Stream names are part of the
reproducibility contract; do not rename them without a generator revision.

The optional examples are separate packages. Swamp scripts its height-field pipeline.
Forts and Even Ground retain native layout designers under `Blueprints`, while their
scripts own the terrain, colonies and furnishing stages. They are examples of
orchestration ports, not standalone reimplementations of the layout solvers.

## Execution and compatibility

Generation uses the vendored QuickJS interpreter and pinned numeric library.
Script-driven shared helpers select pinned math; native generators keep their native
math branch. No filesystem, network, clock, timers or game-order API is exposed.
Each invocation has deterministic budgets of 1 billion interpreter operations and
2 billion native toolkit loop checkpoints, a 512 KiB stack limit and bounded JS/native-handle memory.
Resource failures remain failures even if a script catches the exception. Treat
packages as local authoring code and observe the native toolkit's parameter contracts.

Reports record generator ID, revision, package SHA-256, API/toolkit version, seed,
settings, stage and errors. Keep the exported package with the report to reproduce
an old revision. Package hashes cover canonical manifest and module bytes; a hash
alone cannot recover missing source. Map files store the generated world rather
than generator code or a new serialized request format.

Shipping builds use committed generated C++ adapters and need no libclang. Developers
regenerate them with `tools/map-generators/generate_js_bindings.py` using libclang 18,
its Python bindings and the configured SDL prefix. Pass `--check` to verify that
committed outputs match the current toolkit. Changes to the toolkit require
regenerating adapters, declarations and coverage, updating the API contract where
needed, and verifying native/script generation and platform determinism.
