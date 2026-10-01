# JavaScript scripting

Glob2's optional JavaScript AI and map-script backend uses vendored QuickJS-NG
and OpenLibm. It exposes copied game observations and accepts validated existing
orders or scenario effects. Existing AIs and USL/SGSL maps retain their execution
paths. Selecting JavaScript suppresses the map's retained legacy SGSL script,
including its presentation and win/loss results. Scripts are **trusted developer
code**. Loading a map, save or replay
with embedded JavaScript executes it automatically, without an enablement setting
or permission prompt. Review embedded sources before using files from an untrusted
origin. Capability restrictions and resource limits support predictable execution
and reliability; they do not protect the process against malicious scripts.

The map scenario editor and developer commands can select JavaScript scripts.
JavaScript AI source is currently configured through the developer commands.

## Documentation and examples

- [API reference](javascript-api.md): every exposed method and field, visibility,
  numeric values, sentinels, argument ranges, orders and scenario effects.
- [TypeScript declarations](../../examples/javascript/glob2.d.ts): editor/type
  information for the same boundary. Scripts themselves must be JavaScript.
- [AI example](../../examples/javascript/ai.js) and
  [scenario example](../../examples/javascript/scenario.js): standalone modules.
  The [map-reading example](../../examples/javascript/map-read.js) records visible
  wheat near an owned unit in automatically saved variables.
- [Compatibility fixture](../../test/fixtures/javascript/README.md): reproducible
  numeric, per-tick and save-continuation verification.

## Write a script

Declare a synchronous `step(ctx)` or `main(ctx)` function. An AI returns one
order or nothing; a map script returns an array of effects or nothing. Both use
the same runtime and read API, with different host-enforced visibility. Ordinary
top-level variables persist automatically; there is no required state object or
initialization callback. `export` is optional. Imports are unavailable.

```javascript
let decisions = 0;

function step(ctx) {
  decisions++;
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
that `step` or `main` exists, execute callbacks, or validate returned orders/effects. Those
checks occur during a game. A successful compile check is not a gameplay test.

In the map editor, open the scenario editor, choose the **Map script** tab,
and select **JavaScript** in the language dropdown. The editor supplies a simple
`step(ctx)` starter for a new script. **Compile** (or F9) checks syntax without
executing the script. **OK** compiles and commits the selected source and mode;
saving the map embeds them. JavaScript maps reopen with JavaScript selected,
regardless of the legacy default-language setting. **Load** and **Save** use
`.js` files while JavaScript is selected. Switching languages keeps separate
drafts until the dialog closes; compilation and file loading leave the map
unchanged until OK. Replacements are compiled separately before committing;
failed preparation preserves the active sources, language and saved globals,
including when changing from JavaScript to SGSL. That transition prepares both
the SGSL program and an empty USL backend before replacing either live runtime.
SGSL remains a separate legacy payload: its source is retained
while JavaScript is selected, but it does not execute or present text/timers then.
Choosing **SGSL** and pressing OK clears an active JavaScript map runtime and
reactivates the legacy script. Released maps can pair USL and SGSL; editing either
legacy payload preserves the other and retains their existing execution behavior.

To replace a map's USL script from the command line, write a new map:

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

## Callback lifecycle and globals

Each AI and map script owns a persistent runtime. Source evaluates once, then
`step(ctx)` is called at the existing callback cadence; `main(ctx)` is used when
no `step` function exists. The engine supplies a fresh context for each call.
Top-level `let`, `const` and `var` bindings, including mutable objects declared
with `const`, retain their values. Each script is isolated from other scripts.

The engine automatically snapshots global data after a successful callback and
stores it with the game. Loading evaluates the original source and restores its
bindings before the next callback. Source initialization must be deterministic
and cannot query the world or draw randomness: use `ctx` inside the callback.
Built-ins and the global object are frozen; declare your script's variables at
top level instead of attaching properties to `globalThis` or built-ins. Names
beginning with `__glob2_` are reserved for the engine.

Global data supports plain objects and arrays, including aliases, cycles, sparse
arrays, property attributes, undefined and non-finite numbers. Null prototypes,
property order, extensibility and signed zero survive save/load.
NaN sign and payload are canonicalized at the snapshot boundary; profile 1
does not expose their binary representation. Both infinities retain their signs.
Symbol keys,
accessors, custom prototypes, class definitions/instances, Map/Set instances and host context
objects cannot be retained globally. References to built-ins or function-owned
objects (such as a function prototype) are also rejected; use independent
top-level data. Functions must remain unchanged definitions
from the source and may close over top-level variables. Functions with private
closure locals, or functions created during a callback, cannot be saved;
move their memory to top-level variables. Source functions are frozen and
recreated on load; interpreter pointers and executable bytecode are not saved.

An AI callback returns one order record or `null`/`undefined`; a map callback
returns an effect array or `null`/`undefined`. Return values describe effects,
not persistent memory. Unsavable global data fails with a diagnostic before any
orders or effects commit.

AI callbacks follow the existing AI order polling schedule; paused or eliminated
controllers are not polled. Map callbacks run at the existing world-logic cadence,
currently when `ctx.tick % 32 === 0` in normal simulation. Neither should assume
one callback per tick or use the callback count as elapsed time. Map callbacks
require a game with a mission/GUI context; normal headless Engine sessions supply
one. AI observation history is recorded during simulation, even between decisions,
and disabled/eliminated controllers stop recording it.

Query arguments and returned results use a restricted data format:

| Supported | Rejected at the boundary |
| --- | --- |
| `null`, booleans, finite numbers, strings | `undefined` within records/arrays, NaN, infinities, BigInt, symbols, functions |
| Dense ordinary arrays | Holes, named array properties, accessors |
| Plain records, including null-prototype records | Class instances, custom prototypes, getters/setters, cycles, Map/Set instances |

Map/Set and other permitted objects may be used temporarily during a callback;
convert them to ordinary arrays/records before returning them. Returned
acyclic values are copied as trees; global snapshots preserve aliases and cycles.
Property order, string content and signed zero do survive save/load.
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
query errors inside the script if recovery is useful. Global snapshots and returned
orders/effects are validated **after** `step` returns, so script code cannot
catch those host validation errors. A rejected AI decision disables that
controller, logs a diagnostic and produces no order; later polls also produce
no order. The disabled status and diagnostic survive saves. Map-script failures
stop the session rather than silently skipping scenario logic.

Work exhaustion cannot be bypassed by catching the exception. Native allocation,
QuickJS heap exhaustion and physical-stack failure are fatal host failures,
even if JavaScript catches the initial error; they propagate as session errors
instead of choosing a machine-dependent gameplay outcome. Fatal failures close
the active session, disconnect multiplayer, produce an actionable GUI diagnostic
and return failure in headless execution. A lockstep session must never resume
after a fatal scripting failure. Use small entity pages
and map regions, avoid storing whole observation snapshots, and compare `ctx.tick`
for scheduling. Exception messages are diagnostics, not stable API identifiers.
There is no injected `console` or logging API in profile 1.

## Runtime profile and determinism

| Limit | Profile 1 |
| --- | --- |
| Source | 128 KiB; embedded NUL rejected |
| Encoded globals snapshot / result | 1 MiB per value; conversion accounting also applies across callback data |
| Data nesting | 256 levels |
| Work | 1,000,000 deterministic units per callback including conversion/snapshot; source startup and save restoration each have separate budgets |
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
world contents. Each callback resets work and conversion budgets; the retained heap stays
within its fixed limit. Global snapshots have their own save/load limits.

Ordinary synchronous JavaScript syntax and full Math are supported within the
profile. Transcendental operations and exponentiation use the vendored math subset
with strict floating-point compilation and round-to-nearest. There is no filesystem,
network, clock, OS binding, module loader or host pointer exposure. Dynamic code
compilation, regular expressions, BigInt, async/await and promises are unavailable.
Date, Proxy, weak references, typed arrays and shared memory are omitted.

The interpreter runs in the game process. Maintaining the pinned interpreter is
part of maintaining this trusted-code integration. Rerun capability and resource
regressions when updating dependencies or extending the host API. This work is
not a comprehensive security audit.

Map source/state, RNG, presentation/objectives/hints and entity generation counters
participate in checksums when JavaScript map scripts are active. AI sources/state
use existing player configuration/save mechanisms, and orders use the existing
multiplayer/replay path. No additional synchronization protocol is introduced.
Save format 125 adds the scripting identity fields and JavaScript map mode to
format 124's released experiment-header layout. The loader preserves that layout
for released format-124 saves and assigns entity identities when loading formats
58–124. Format-125 saves preserve and validate stored identities and generation
counters; truncated counter tables are rejected, including missing entries for
unused entity slots. The minimum save version remains 58.
Network/YOG protocol 49 combines the scripting wire format with mandatory WSS;
older protocols are rejected. The replay minimum remains 123. Scripting profile 1 is a
separate unpublished contract; its number does not negotiate either engine gate.
Profile versioning and API-maintenance obligations are in the reference.

## Verify a change

Build `unit-tests engine-tests` with SCons, then run:

```sh
python3 test/run_tests.py --filter 'JavaScript*/*'
python3 test/check_javascript.py /absolute/path/to/glob2 --output artifacts/js-check
```

Use a fresh output directory for the frozen check. Runtime tests compare exact
IEEE double bits and exercise capability/resource failures. Integration tests cover
fog of war, stale terrain/references, ownership, orders, scenario rollback and
continuation. The shared corpus includes realistic economic planners and a toroidal map survey
that read production observations, calculate scores and return persisted data and
orders. The frozen fixtures compare complete tick traces, worker-count
equivalence, replays, saves and resumed execution. Changes affecting simulation
still require matching per-tick checksums across supported platforms; successful
builds alone do not establish cross-platform determinism.

### Numeric implementation inventory

All profile numbers are IEEE binary64; integer fast paths implement the same
JavaScript semantics. Runtime startup requires round-to-nearest. Interpreter,
pinned math and first-party host conversion objects use `-fno-fast-math` and
`-ffp-contract=off`. The build checks their undefined symbols with
`tools/javascript/check-math-symbols.py`; unexpected platform math or numeric
parsing dependencies fail the build. There are currently no exceptions.

| Script-reachable operation | Implementation |
| --- | --- |
| `Math.abs`, `floor`, `ceil`, `trunc`, `sqrt` | Namespaced OpenLibm 0.8.8 |
| `acos`, `asin`, `atan`, `atan2`, `cos`, `sin`, `tan` | Namespaced OpenLibm, including argument reduction |
| `exp`, `expm1`, `log`, `log1p`, `log2`, `log10`, `pow`, `cbrt` | Namespaced OpenLibm; QuickJS supplies JavaScript `pow` special cases |
| `cosh`, `sinh`, `tanh`, `acosh`, `asinh`, `atanh` | Namespaced OpenLibm |
| `Math.hypot` | QuickJS variadic accumulation with pinned OpenLibm binary `hypot` |
| `Math.round`, `sign`, `min`, `max`, `imul`, `clz32`, `sumPrecise` | Pinned QuickJS interpreter algorithms; `round` uses pinned `floor` |
| `Math.fround`, `f16round` | Interpreter conversions; half conversion uses pinned `frexp`, `scalbn`, `copysign` |
| `Math.random`, `ctx.random` | Persisted host private RNG, exactly representable integer-to-double scaling |
| Math constants, numeric literals | Pinned literal values and QuickJS dtoa parser |
| `+`, `-`, `*`, `/`, unary signs, increment/decrement | Strict IEEE arithmetic with interpreter coercion and integer fast paths |
| `%`, `**` | Pinned OpenLibm `fmod` and JavaScript exponentiation wrapper |
| Bitwise operations, shifts, integer conversion | Interpreter integer operations and explicit truncation/wrapping |
| Comparisons, equality, boolean conversion, Number predicates | Interpreter coercion, comparisons and classification |
| `Number`, unary `+`, `parseFloat`, `parseInt`, JSON numeric parsing | Pinned interpreter and dtoa parsing; no host `strtod` |
| `String(number)`, number `toString`/`toFixed`/`toPrecision`/`toExponential`, JSON numeric output, implicit numeric string conversion | Pinned dtoa formatting and interpreter radix/rounding logic |
| Host query/order integers, finite-data validation, save encoding | Range checks, exact bounded integer casts, classification and binary bit encoding; no host decimal formatting |

The numeric corpus covers every exposed Math method, coercion, parsing and
formatting, signed zero, subnormals, overflow/underflow, large arguments, rounding
boundaries and fixed-seed vectors. It compares finite result bits and observable
non-finite behavior. This is executed evidence for the tested vectors and builds,
not a proof covering all possible scripts or inputs.

Profile 1 remains unpublished while these defects are corrected. The former
platform-dependent `hypot` result is intentionally replaced by the pinned result;
its ARM64/x86-64 reproducer is retained as a regression. Released save, replay and
network acceptance gates remain independent of this draft profile.
