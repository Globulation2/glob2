# JavaScript scripting

The optional JavaScript AI and map-script backend uses vendored QuickJS-NG and
OpenLibm. Existing AIs and USL/SGSL maps retain their existing execution paths.
This first version provides developer commands; the game menus do not expose a
JavaScript selection control.

## Loading sources

Compile-check a synchronous module with `glob2 --check-script source.js`.
To replace a map's USL script, use
`glob2 --attach-map-script input.map source.js output.map`; the tool writes a new
gzip map and refuses to overwrite an existing output. SGSL is a separate legacy
script payload and is preserved. For a new structured headless game, pass
`--map-script source.js`, or select `--player javascript --ai-script 0:source.js`
(with equivalent options for each other player). These options accompany the
usual `--run-game` map, seed, player and output options documented in
[distributed tournaments](../tools/tournaments.md).

Use absolute source paths with structured headless commands on macOS, whose
normal startup changes the working directory to the app resource directory.

AI source is embedded in each player's existing GameHeader AI configuration,
prefixed by `glob2-js/1\n`. Map source and runtime state live in the existing
MapScript payload, with a new JavaScript mode. Saves/replays therefore carry
source rather than machine-dependent bytecode or external filenames. Source
options are unavailable when resuming with `--load-game`.

Modules export `step(ctx, state)` and optionally `init(ctx, state)`. Initialization
runs before the first successful step. `init` returns null/undefined; an AI step
returns one order descriptor or null, and a map step returns an array of effects
or null. See [AI example](../../examples/javascript/ai.js) and
[scenario example](../../examples/javascript/scenario.js).

Each callback receives a fresh runtime: module variables, closures and modified
built-ins disappear between callbacks. Only mutations to `state` persist. State
supports null, booleans, finite numbers, strings, dense arrays and plain records;
cycles, accessors, symbols, functions and other objects are rejected. Record key
order and signed zero survive saves. Unsupported state or effects reject the
whole callback, including its RNG consumption.

## Read API and visibility

`ctx.tick` is the simulation tick and `ctx.myTeam` is the AI's team, or -1 for a
map script. `ctx.random()` and `Math.random()` use the same private deterministic
controller RNG. They do not consume the simulation RNG. AI controllers use the
existing per-player AI stream; map scripts use a separately seeded, persisted
stream. Terrain history uses lazily allocated indexed chunks, and disabled or
eliminated controllers stop recording observations.

| Query on `ctx.game` | Result |
| --- | --- |
| `teams()` | Team IDs/alive status; own-team economy/alliance data, or all teams for maps |
| `units({team,offset,limit}?)`, `buildings({team,offset,limit}?)` | Stable team/slot order, filtered by capability |
| `unit({id,generation})`, `building({id,generation})` | Entity or null for hidden, missing or stale references |
| `buildingTypes()` | Static building variants with ID, name, level, size, site/virtual flags, capacities |
| `map.width`, `map.height` | Toroidal dimensions |
| `map.tile(x,y)` | Terrain/resource observation with visible/explored/observedTick |
| `map.region(x,y,width,height)` | Row-major tile array; maximum dimensions 256, subject to work budget |
| `objectives()`, `hints()`, `interface()` | Scenario-only objective, hint and presentation records |

Owned entities expose operational information (unit activity/targets and building
workers, resources, production and flag settings). Opponents expose only the
visible entity subset, never hidden targets or production/economy internals.
AI unit visibility follows fog of war and excludes units inside buildings;
cloaked enemy buildings are excluded. Hidden entity lookups return null even if
the script knows their IDs. Entity references include a generation to prevent a recycled ID silently
referring to a new unit or building. References use the existing engine slot generations.

Entity-list offsets count only visible results in stable team/slot order; limits
bound the returned page. Use pages for large rosters. Native query construction
charges each returned record before allocating it.

AI terrain queries retain the last observed terrain/resources with their tick;
an unseen tile contains no terrain/resource data. Current occupants are returned
only for currently visible tiles. Coordinates wrap around the map. Observations
are copied data: modifying returned records cannot change the game. Map scripts
see current terrain and all entities. This is an explicit capability selected by
the host, never a flag the script can grant itself.

## Gameplay orders

All building-targeted orders take a `building` reference from the read API and
require ownership and matching generation. Worker requests are 0..20; production
ratios are 0..16, matching the GUI controls. Integer fields are range checked.
Orders pass through the game's existing order execution path.

| `type` | Additional fields |
| --- | --- |
| `create` | `buildingType`, `x`, `y`, `workers`, `futureWorkers`; flags also `range` |
| `workers` | `workers` |
| `delete`, `cancelDelete` | None |
| `construction` | `workers`, `futureWorkers` |
| `cancelConstruction` | `workers` |
| `priority` | `priority` (-1, 0, 1) |
| `production` | Three `ratios` (swarm only) |
| `exchange` | `receiveMask`, `sendMask` (market only) |
| `range`, `minimumLevel` | `range` or `level` (flag only) |
| `moveFlag` | `x`, `y` |
| `clearingResources` | Five resource booleans; stone (index 3) must be false (clearing flag only) |
| `forbidden`, `guardArea`, `clearArea` | `x`, `y`, `width`, `height`, row-major boolean `mask`, `mode` (1 add, 2 remove) |

Creation accepts level-zero construction-site variants or virtual flags from
`buildingTypes()`. Scenario building restrictions are enforced when the engine
executes creation, including orders from other controllers.

## Scenario effects

Map callbacks return up to 256 effect records. The complete batch is validated
before applying any effect. Presentation state is authoritative independently of
local GUI settings and is restored through saves.

| `type` | Fields |
| --- | --- |
| `message` | `text` |
| `messageTranslated` | `language`, `text` |
| `hideMessage` | None |
| `objective` | Zero-based `id`, `action`: complete/incomplete/failed/hidden/visible |
| `hint` | Zero-based `id`, `visible` |
| `buildingChoice`, `flagChoice` | `name`, `enabled` |
| `guiElement` | `id` (0..4), `enabled` |

Building names are swarm, inn, hospital, racetrack, swimmingpool, barracks,
school, defencetower, stonewall and market; flags are explorationflag, warflag
and clearingflag. Spawning entities and changing terrain are outside this profile.

## Determinism and sandbox boundaries

Profile 1 limits source to 128 KiB, persistent state to 1 MiB, data nesting to 256,
native recursive parsing/traversal to 64 guarded calls, each invocation to one
million deterministic work units and the QuickJS heap to 32 MiB. The engine
meters bytecode dispatch, lexing, native container operations,
queries and data conversion. Containers and strings receive linear entry charges;
native
string searches charge actual coerced lengths, and sorting/output charge their
comparisons and emitted characters. Conservative operation charges mean large
arrays or region reads may exhaust the budget before their dimensions
reach the maximum. Native data construction/conversion and saved-state decoding
also use a 32 MiB accounting budget with fixed conservative weights across ABIs.
Sparse/accessor arrays are validated before allocating native output storage.
Limits do not depend on elapsed time or hidden world contents.

There is no filesystem, network, clock, OS binding, module loader or host pointer
exposure. Dynamic compilation, regular expressions, BigInt, async/await and
promises are unavailable to scripts. Date, Proxy, weak references, typed arrays
and shared memory are omitted. Ordinary synchronous JavaScript syntax and full
Math are supported; transcendental operations and exponentiation use the vendored
math subset with strict floating-point compilation and round-to-nearest.

This is an in-process interpreter sandbox. A native interpreter vulnerability
can compromise the process; the JavaScript boundary is not OS process isolation.
Keep vendored dependencies patched and rerun the hostile-script regression suite
when changing the runtime or host API. Deterministic failures disable the AI with
a diagnostic; map failures stop the session. Host allocation/physical stack
failures propagate as fatal session errors rather than silently choosing a
machine-dependent gameplay outcome.

Map script state, RNG, effects and entity generation counters participate in
simulation checksums when JavaScript map scripts are active. AI sources/state
use existing player configuration/save mechanisms and orders use the existing
multiplayer/replay path; no additional synchronization protocol is introduced.
The save format is version 124 with version-gated reading of older files and the
previous minimum save version retained. Network protocol 47 rejects old clients.

Build `unit-tests engine-tests` with SCons, then run the `JavaScriptRuntime` and
`JavaScriptIntegration` suites with `test/run_tests.py --filter 'JavaScript*/*'`.
Numeric tests compare exact IEEE double bits. The frozen
[test fixture](../../test/fixtures/javascript/README.md) checks complete per-tick
traces and worker/save continuation; integration tests cover fog of
war, stale terrain, ownership, scenario rollback and continuation. Changes that
can affect simulation still require matching per-tick checksums across supported
platforms; successful builds alone do not establish cross-platform determinism.
