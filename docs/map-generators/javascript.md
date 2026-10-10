# JavaScript map generators

## On this page

- [Generator Studio](#generator-studio)
- [Author and run](#author-and-run)
- [Publish and discover online](#publish-and-discover-online)
- [Manifest and portable format](#manifest-and-portable-format)
- [Callbacks and toolkit](#callbacks-and-toolkit)
- [Execution and compatibility](#execution-and-compatibility)
- [Maintaining the bridge](#maintaining-the-bridge)

## Generator Studio

The opt-in web workspace at `/generator-studio` combines conversation, JavaScript
and manifest editing, immutable private revisions, and engine previews. Start with
a playable Swamp example with a fresh identity, or import a portable package. V1
supports exactly one JavaScript module; imports preserve the manifest and entry
filename and reject packages with additional modules. The native package format
still supports multiple modules.

Both files form one revision and one assistant replacement. Invalid manifest JSON
remains editable and recoverable; syntax hints do not establish engine validity.
Code and Changes select either file, and restoring or undoing an assistant edit
creates another immutable project revision. Project revision numbers are separate
from the manifest's increasing release revision.

Generate freezes the saved revision and settings in the browser engine. Defaults
are seed 19, 128 × 128 tiles, four colonies and four starting workers. Manifest
controls augment the shared settings; dimensions display tiles and travel as the
existing width/height exponents. Changing controls requires another Generate.
Reports include package identity, engine/simulation version, duration, refusals
and bounded telemetry. Watch AI play starts Nicowar colonies from that exact world
with spectator camera, pause and speed controls. Editor-only terrain can be
previewed but cannot be playtested. Closing or replacing the preview ends its
temporary-profile host.

Run checks explicitly submits the saved package to the isolated Generator Library
validator. Reports stay bound to their revision and can be supplied to chat for a
requested repair; the assistant cannot run tests or claim unsupplied results.
Download exports the portable package. An invalid manifest instead downloads a
Studio recovery draft containing both editable files; import that draft to resume
repairing its JSON. Publish uses the existing library rules:
a matching, valid, unexpired upload receipt, identity ownership, increasing
manifest revision, licensing and immutable releases. Browser results are local
development diagnostics and never authorize publication.

## Author and run

An authoring directory contains `manifest.json`, `generator.js`, and optional `.js`
modules imported with relative paths. Imports cannot leave the package. Each launch
freezes those files into an immutable package; relaunch after editing to reload it.

```sh
build/linux/client/release/src/glob2 --generator-package data/generators/examples/swamp \
  map generate examples:swamp --seed 91 --width 256 --height 256 --teams 4 \
  --output artifacts/swamp.map --preview artifacts/swamp.png --report-file artifacts/swamp.json \
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
Library changes become active after storage persistence succeeds. Publication affects
future selections; existing requests, previews, resets and controls retain their
selected catalog revision until a new landscape is selected. Packages appear in the local game
landscape picker and the map editor. The online library also publishes exact releases
for local installation and server-controlled generation in custom rooms.

## Publish and discover online

The online map library has **Maps** and **Generators** tabs. On the website, choose
**Publish generator**, upload a portable JSON package, and supply one example seed
and settings. Publication starts unlisted. Inspect the validation report before
publishing: uploading alone never publishes a release. Public releases appear in
search; unlisted releases are accessible by link; private releases are available
only to their owner. Registered-account restrictions, likes, reporting and moderation
follow the map library. Editor tools are labelled and cannot be selected in rooms.

The first successful publication reserves the namespaced manifest ID permanently.
New releases of that identity must increase the manifest revision; forks use a new
ID. Published releases are immutable. Deleting a catalogue identity does not free
its manifest ID or remove bytes needed by recorded matches.

Workers inspect packages with the engine's authoritative parser. The downloadable
file is the engine's canonical export; reports retain both its hash and the
original upload hash. All uploaded-module inspection, generation and map reload
run inside the existing Linux namespace sandbox. Workers advertise these jobs only
after an isolated engine startup probe succeeds. Namespace/filesystem failures are
retryable infrastructure errors; rejected packages, interpreter/toolkit budget
failures, crashes and invalid worlds fail technical validation.

The example must generate twice with identical decompressed world fingerprints
and load as an ordinary saved map. Supplementary requests sample two other seeds,
smaller/larger and rectangular maps, different colony counts, and each individual
control extreme. Legitimate request refusals are shown explicitly. Editor-only
packages receive terrain validation without starting-colony requirements. Reports
bind package hash, engine simulation version, API/toolkit version and suite revision.
Passing this bounded sample does not establish balance or support for every request.

Processes are limited to 120 seconds, 2 GiB memory, 64 MiB files and 64 KiB captured
output. A validation job has a 15-minute total deadline; samples are removed after
inspection within the worker's dedicated scratch tmpfs (at most 4 GiB). The existing
deterministic interpreter and toolkit limits remain unchanged. Upload/publication
and custom-room generation have per-account rate limits. Releases receive new,
separate evidence as new engine versions are served; old evidence remains visible.

In the native **Generators** tab (also linked from **Settings → Map generators**),
select an exact compatible release and install it. File and package hashes are
checked. Failed durable writes trigger rollback; restoration failures are reported
and clear the pending operation. An installed
manifest ID requires explicit replacement confirmation. Release provenance is
stored separately from package bytes. New publications do not automatically update
an installed package; select and install the desired release explicitly.

For a custom online room, select **Use in a room**, choose controls and reroll the
seed. The room pins that exact release; publishing another revision does not change
it. Settings changes clear readiness and replace pending generation. The server
checks release visibility, moderation, playable status and engine validation before
generation and again before starting. It never substitutes another release or a
native generator. Joining players download the resulting ordinary map and do not
need the generator installed. Match history retains the exact package, requested
and chosen seed and settings. Sharing those generated map bytes links verified
server provenance to the finished map version. A version upload may supply a
`generator` query parameter containing a scripted descriptor; it remains an author
claim unless the server independently recognizes the generated world bytes. Ranked
matchmaking keeps native map generation. Older clients must update before joining scripted rooms.

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
blank terrain generators may omit colony placement; a package with
`hasStartingColonies: false` must also declare `editorOnly: true`. The engine still validates the
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
`clone` and `toArray`. Value and read-only vector arguments also accept JavaScript
arrays; mutable vector arguments require handles. Numeric buffer declarations carry
a storage type so byte masks, integer indices and floating-point fields cannot be
interchanged accidentally. Native buffers passed into callbacks expire when that callback returns; clone data that
must be retained. Solver briefs and height maps constructed with a callback's
temporary context or RNG also expire with that callback. Call operators are exposed as `at`; objective iteration is
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

`FertilityField.Field()` creates an empty field. Before sampling it, call `rebuild`
or `rebuildWeighted` with dimensions in `1..1024` and input buffers of exactly
`width * height` elements. `gate` and `multiplyLocal` require masks of the field's
size. `FertilityField.Path` selects the native convolution path. Rebuilds reserve
their worst-case native work and scratch memory before entering the native method.
Shared helpers accepting a fertility field also require it to be initialized.
Mutable generator controls are checked before native operations: ranges need a
positive step and a bounded legal domain, and power-of-two display values must be
in `0..30`.

The optional examples are separate packages. Swamp scripts its height-field pipeline.
Forts and Even Ground retain native layout designers under `Blueprints`, while their
scripts own the terrain, colonies and furnishing stages. They are examples of
orchestration ports, not standalone reimplementations of the layout solvers.

## Execution and compatibility

Generation uses the vendored QuickJS interpreter and pinned numeric library.
Script-driven shared helpers select pinned math; native generators keep their native
math branch. No filesystem, network, clock, timers or game-order API is exposed.
Each invocation has deterministic budgets of 1 billion interpreter operations and
2 billion native toolkit loop checkpoints, a 512 KiB stack limit and a shared
128 MiB budget for JavaScript and native handles. Native reservations are cumulative
for the invocation; releasing a handle does not restore its allocation budget.
Resource failures remain failures even if a script catches the exception. Treat
packages as local authoring code and observe the native toolkit's parameter contracts.

Reports record generator ID, revision, package SHA-256, API/toolkit version, seed,
settings, stage and errors. Keep the exported package with the report to reproduce
an old revision. Package hashes cover canonical manifest and module bytes; a hash
alone cannot recover missing source. Map files store the generated world rather
than generator code or a new serialized request format.

## Maintaining the bridge

Shipping builds use committed generated C++ adapters and need no libclang. Developers
install the pinned Python bindings from `requirements-dev.txt`, supply system
libclang 18, and regenerate with `tools/map-generators/generate_js_bindings.py`
and the configured SDL prefix. Pass `--check` to verify that
committed outputs match the current toolkit. Changes to the toolkit require
regenerating adapters, declarations and coverage, updating the API contract where
needed, and verifying native/script generation and platform determinism.

Type-check the positive and negative authoring examples with
`tsc --strict --noEmit test/map_generator_toolkit_contract.ts`; they cover buffer
mutability, callbacks, output references and the manually exposed methods/constants.

The generator owns API classification and emits constructor ownership explicitly.
`ToolkitBinding` owns conversion, callback leases and runtime limits; native
helpers outside the instrumented toolkit need explicit bridge contracts before
they are exposed. `ToolkitFertilityContracts.h` demonstrates shape validation,
work reservation and allocation reservation before a native call. A successful
native assertion in the game is not a substitute for validating script arguments.

`tools/map-generators/instrument_toolkit_budget.py` reports missing loop checkpoints
and unchecked vector indexing without modifying source. Use `--write` to apply its
proposed edits, then review the diff. It does not infer scratch allocations or
reference ownership; those contracts still require explicit review and regressions.

The `ScriptGenerator` suite's shared-world golden case compares two seeds on a
128 × 64 world, then compares all simulation checksum components through 256 ticks
after save/load. It retains the full trace as test evidence and compares its SHA-256
with `test/fixtures/generators/shared-generator-trace.sha256`. The browser determinism
suite checks the same reference in serial and threaded Chromium, Firefox and WebKit;
it does not require a locally prepared native artifact. Regenerate this reference
with the native test runner's `--update-fixtures` when an intentional simulation
change updates these worlds, alongside the simulation-version compatibility work.

Related: [map generators](README.md).
