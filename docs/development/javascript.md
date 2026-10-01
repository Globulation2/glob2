# JavaScript scripting

Glob2's optional JavaScript AI and map-script backend uses vendored QuickJS-NG
and OpenLibm. It exposes copied game observations and accepts validated existing
orders or scenario effects. Existing AIs and USL/SGSL maps retain their execution
paths. This version provides developer commands; menus have no JavaScript
selection control.

## Documentation and examples

- [API reference](javascript-api.md): every exposed method and field, visibility,
  numeric values, sentinels, argument ranges, orders and scenario effects.
- [TypeScript declarations](../../examples/javascript/glob2.d.ts): editor/type
  information for the same boundary. Scripts themselves must be JavaScript.
- [AI example](../../examples/javascript/ai.js) and
  [scenario example](../../examples/javascript/scenario.js): standalone modules.
  The [map-reading example](../../examples/javascript/map-read.js) records visible
  wheat near an owned unit without copying a whole map into state.
- [Compatibility fixture](../../test/fixtures/javascript/README.md): reproducible
  numeric, per-tick and save-continuation verification.

## Write a script

A single synchronous module exports `step(ctx, state)` and optionally
`init(ctx, state)`. An AI returns one order or nothing; a map script returns an
array of effects or nothing. Both receive the same read API, with different
host-enforced visibility. There are no imports or external module dependencies.

```javascript
export function init(ctx, state) {
  state.decisions = 0;
}

export function step(ctx, state) {
  state.decisions++;
  const buildings = ctx.game.buildings({team: ctx.myTeam, limit: 50});
  const building = buildings.find(b => !b.virtual && b.workers !== 2);
  return building ? {type: 'workers', building, workers: 2} : null;
}
```

Map scripts use `ctx.myTeam === -1`, can inspect the whole map and return scenario
effects, for example `[{type: 'message', text: 'Welcome!'}]`. AI scripts read only
permitted observations and can issue orders only for their own team. Changing
context properties cannot grant additional capabilities.

## Load and check sources

Compile-check syntax and profile restrictions:

```sh
glob2 --check-script /absolute/path/to/source.js
```

This only compiles/resolves the module: it does not evaluate module code, verify
that `step` exists, execute callbacks, or validate returned orders/effects. Those
checks occur during a game. A successful compile check is not a gameplay test.

To replace a map's USL script, write a new map:

```sh
glob2 --attach-map-script input.map /absolute/path/to/source.js output.map
```

The tool writes gzip data and refuses to overwrite an existing output. SGSL is a
separate legacy payload and is preserved. For a new structured headless game,
use `--run-game` with `--map-script /absolute/path/to/source.js`, or
`--player javascript --ai-script 0:/absolute/path/to/ai.js` (and equivalent
source options for other JavaScript players). See
[distributed tournaments](../tools/tournaments.md) for complete map, seed,
player and output commands.

Use absolute source paths for structured commands on macOS, whose normal startup
changes the working directory to the app resource directory. Source options are
unavailable when resuming with `--load-game`: the save supplies its embedded code.

AI source lives in each player's existing GameHeader AI configuration, prefixed
by `glob2-js/1\n`. Map source and runtime state live in the existing MapScript
payload in JavaScript mode. Saves/replays carry source, not external filenames
or machine-specific bytecode.

## Callback lifecycle and state

Each invocation creates a fresh runtime, evaluates the module, optionally calls
`init`, then calls `step` with the same context and state object. Initialization
occurs on the first invocation and is marked complete only after a successful
commit. Module variables, closures and changes to built-ins disappear after the
invocation. Use module-level constants freely, but keep ongoing memory in `state`.

`state` starts as `{}`. Mutate its properties; replacing your local `state`
parameter does not replace the host's root object. `init` must return
`null`/`undefined`. An AI `step` returns one order record or `null`/`undefined`;
a map `step` returns an effect array or `null`/`undefined`. Returning state is
not how to persist it.

AI callbacks follow the existing AI order polling schedule; paused or eliminated
controllers are not polled. Map callbacks run at the existing world-logic cadence,
currently when `ctx.tick % 32 === 0` in normal simulation. Neither should assume
one callback per tick or use the callback count as elapsed time. Map callbacks
require a game with a mission/GUI context; normal headless Engine sessions supply
one. AI observation history is recorded during simulation, even between decisions,
and disabled/eliminated controllers stop recording it.

State, query arguments and returned results use a restricted data format:

| Supported | Rejected at the boundary |
| --- | --- |
| `null`, booleans, finite numbers, strings | `undefined` within records/arrays, NaN, infinities, BigInt, symbols, functions |
| Dense ordinary arrays | Holes, named array properties, accessors |
| Plain records, including null-prototype records | Class instances, custom prototypes, getters/setters, cycles, Map/Set instances |

Map/Set and other permitted objects may be used temporarily during a callback;
convert them to ordinary arrays/records before saving or returning them. Shared
acyclic values are copied as trees, so identity/aliasing does not survive a
callback. Property order, string content and signed zero do survive save/load.
Read records also have a null prototype; consult the reference for safe property
checks and missing-field behavior.

`ctx.random()` and `Math.random()` share a private deterministic stream. They do
not consume simulation randomness. AI controllers use the existing per-player
AI stream; map scripts have a separately seeded, persisted stream. A successful
callback commits its state and RNG alongside the accepted order/effects.
Rejected results roll back state and RNG consumption; map effect batches are
validated before application. Observation history is recorded world data, not
callback state, and is not rolled back with a rejected decision.

## Failures and debugging

Invalid read arguments throw a catchable JavaScript `TypeError`. Catch ordinary
query errors inside the script if recovery is useful. Returned state and
orders/effects are validated **after** `step` returns, so script code cannot
catch those host validation errors. A rejected AI decision disables that
controller, logs a diagnostic and produces no order; later polls also produce
no order. The disabled status and diagnostic survive saves. Map-script failures
stop the session rather than silently skipping scenario logic.

Work exhaustion cannot be bypassed by catching the exception. Native allocation,
QuickJS heap exhaustion and physical-stack failure are fatal host failures,
even if JavaScript catches the initial error; they propagate as session errors
instead of choosing a machine-dependent gameplay outcome. Use small entity pages
and map regions, avoid storing whole observation snapshots, and compare `ctx.tick`
for scheduling. Exception messages are diagnostics, not stable API identifiers.
There is no injected `console` or logging API in profile 1.

## Runtime profile and determinism

| Limit | Profile 1 |
| --- | --- |
| Source | 128 KiB; embedded NUL rejected |
| Encoded persistent state / result | 1 MiB per value; conversion accounting also applies across callback data |
| Data nesting | 256 levels |
| Work | 1,000,000 deterministic units per invocation, shared by compilation, evaluation, init, step and conversion |
| QuickJS heap | 32 MiB |
| Native data accounting | 32 MiB, cumulative conservative fixed weights across ABIs |
| Interpreter call depth | 256; physical stack budget is a fatal fallback |
| Native recursive parsing/traversal | 64 guarded calls |
| Map effects | At most 256 per invocation |

Work meters interpreter dispatch, lexing, queries, conversions and native
operations. Container/string entry charges are linear; string searches charge
actual coerced lengths, and sorting/output charge comparisons/emitted characters.
Large requests can exceed budgets below their individual dimension limits.
Budgets are not wall-clock timeouts and are not dynamically sized from hidden
world contents. A fresh runtime resets invocation budgets; persistent state has
its own save/load limits.

Ordinary synchronous JavaScript syntax and full Math are supported within the
profile. Transcendental operations and exponentiation use the vendored math subset
with strict floating-point compilation and round-to-nearest. There is no filesystem,
network, clock, OS binding, module loader or host pointer exposure. Dynamic code
compilation, regular expressions, BigInt, async/await and promises are unavailable.
Date, Proxy, weak references, typed arrays and shared memory are omitted.

This is an in-process interpreter sandbox, not OS process isolation. Maintaining
the pinned interpreter is part of maintaining the boundary. Rerun hostile-script
regressions when updating dependencies or extending the host API.

Map source/state, RNG, presentation/objectives/hints and entity generation counters
participate in checksums when JavaScript map scripts are active. AI sources/state
use existing player configuration/save mechanisms, and orders use the existing
multiplayer/replay path. No additional synchronization protocol is introduced.
Save format 124 uses version-gated loading with minimum save version 58 retained;
network/YOG protocol 47 rejects older clients; replay minimum remains 123.
Profile versioning and API-maintenance obligations are in the reference.

## Verify a change

Build `unit-tests engine-tests` with SCons, then run:

```sh
python3 test/run_tests.py --filter 'JavaScript*/*'
python3 test/check_javascript.py /absolute/path/to/glob2 --output artifacts/js-check
```

Use a fresh output directory for the frozen check. Runtime tests compare exact
IEEE double bits and exercise sandbox/resource failures. Integration tests cover
fog of war, stale terrain/references, ownership, orders, scenario rollback and
continuation. The frozen fixture compares complete tick traces, worker-count
equivalence, replays, saves and resumed execution. Changes affecting simulation
still require matching per-tick checksums across supported platforms; successful
builds alone do not establish cross-platform determinism.
