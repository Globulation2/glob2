# Native tests

Native tests are doctest cases compiled into four binaries by the main build, from
the same objects as the game:

| Binary | Links | How it runs |
| --- | --- | --- |
| `glob2-unit-tests` | libgag, libusl, a few production sources and the stubs in `test/unit/stubs/` | headless cases share a process; display cases run in separate processes |
| `glob2-map-tests` | `libglob2_map.a`, libgag, libusl, the production objects in `MAP_PRODUCTION_SOURCES` and the stubs in `test/map/stubs/` | as `glob2-unit-tests` |
| `glob2-sim-tests` | `libglob2_sim.a`, `libglob2_map.a`, libgag, libusl, a headless `GlobalContainer`, `SIM_PRODUCTION_SOURCES` and `test/sim/stubs/`; no `GameGUI` | as `glob2-engine-tests` |
| `glob2-engine-tests` | every client object except the entry point | one process per test case, each in a disposable profile |

The module libraries are archives of the client objects in `MAP_SOURCES` and
`SIM_SOURCES` (`buildsys/sources.py`), so the linker takes only the members a test
reaches: a map change relinks a few hundred objects instead of the whole client.
`engine-tests` also builds `glob2-sim-tests` and `unit-tests` also builds
`glob2-map-tests` (`map-tests` and `sim-tests` build one alone), because the module
binaries hold cases moved out of those two.

All are listed in `test/tests.py`, built by `test/SConscript` and land in
`build/<toolchain>/client/release/test/` (`darwin`, `linux` or `mingw`; `--build=DIR`
and `GLOB2_BUILD_DIR` override the directory as for the game). The standalone
programs that remain come from the `PROGRAMS` table in the same file
(`MapReportHarness`, `MenuColonyHarness`, and the `glob2-tools` group) or from
`src/SConscript` (`MapGeneratorGoldenTest`, `LANSessionHarness`, the transport
programs and the map generator study tools). The per-harness
aliases documented in the domain verification guides are listed in `LEGACY_ALIASES`; they build
the binary that now contains the test.

The `RenderFramePacer` unit suite checks drawing deadlines, frame-cost accounting,
live changes, resume and Unlimited with explicit timestamps. `SettingsGraphics`
checks FPS preference migration and validation; `Settings` covers the native and
compact dropdown, persistence and screenshots. `ScreenExecution` checks that
update-only callbacks and capped drawing retain input and screen lifecycle behavior.

## Build and run

```sh
python3 tools/dev_build.py engine-tests unit-tests
python3 test/run_tests.py                   # headless tier, newest build: no [display], no [slow]
python3 test/run_tests.py --tier display    # adds [display] cases
python3 test/run_tests.py --tier full       # everything this platform can run
python3 test/run_tests.py --list --tag display
python3 test/run_tests.py --fullscreen --tag display      # opt in to fullscreen transitions
python3 test/run_tests.py --binary unit          # glob2-unit-tests and glob2-map-tests
python3 test/run_tests.py --binary map           # one module binary
python3 tools/dev_build.py map-tests && python3 test/run_tests.py --binary map   # map edit loop
python3 test/run_tests.py --filter 'HungryDefeat/*' --verbose
python3 test/run_tests.py --binary engine --shard 2/4 --junit artifacts/tests/junit.xml
python3 test/run_tests.py --binary engine --in-process     # fast local loop, no isolation
python3 test/run_tests.py --update-fixtures --tag golden   # rewrite committed goldens
```

The `Maxima.Farming` fertility-cache case checks correctness in every build; its
CPU-time budget is a separate `[benchmark]` case, enforced only in optimized builds
without coverage instrumentation.

`ColonySkinPreview` checks shared image preparation with independent appearance
authorization, refresh, expiry and cancellation across preview owners.

`UnitCatalog` validates developer-authored definitions and migration boundaries.
`UnitCustomization` exercises combined capabilities, cargo accounting and save
continuation. Its locked ablation case exports `unit-ablations.trace`, and the
`RuntimeResources` composition case exports `seeded-compositions.trace`; both are
`[sim-trace]` cases with no committed fixture. `test/run-browser-determinism.py
--engine-binary` retains the native traces alongside the stock replay, and
`.github/scripts/ci_compare_evidence.py` requires every native and serial/threaded
browser trace to match the first native one row for row. Native and browser
resource/unit evidence must identify the same clean source revision.

`SkinShapeModel` and `SkinModel` check the GSB1 blend-shape and GSR1 bone-rig
contracts against the analytic fixtures shared with the Studio decoders
(`test/fixtures/skins/`). `SkinModelRender` checks native GPU/CPU agreement for
the rig shader, mixed baked/rig rendering, GL state restoration and CPU fallback,
and the browser conformance suite checks the same shader in Chromium, Firefox and
WebKit; `tools/skins/test_fit_shapes.py` and `test_explorer_rig.py` regenerate
the installed assets and check their fit to the baked clips. See
[unit rigs](../tools/unit-animation/README.md#contract-and-conformance-tests).

Asset pipeline checks live in `AssetLoader` and `SpriteLoad`, including independent
continuation cancellation, cache metadata cleanup and variable atlas admission.
`SpriteSheets` also checks renderer readiness, atomic HD reload publication and
retention of unchanged artwork requests. `HighResolutionIntegration` checks that
shared artwork survives screen creation and teardown. Build the
`asset-loading-benchmark` target to compare worker configurations against the same
runtime assets. See [asset loading](../docs/development/package-size.md#release-asset-and-bundle-sizes)
for worker controls, scratch accounting and measurement limits.

Without `--build-dir` or `GLOB2_BUILD_DIR` the runner uses the newest native build:
a `tools/dev_build.py` dev build (`build/<platform>/client/debug/dev-*`) or
`build/<platform>/client/release`. The default
tier is `headless`, which leaves out `[display]` and `[slow]` cases and runs in
about a quarter of the full inventory's time; `--tier display` adds display cases
(`--quick` is the same), and `--tier full` runs everything. Timeouts catch hangs, not
slowness: a case recorded in the CI timing profiles (`test/ci-timings/*.json`, the slowest
profile unless `--timing-profile` names one) gets ten times its recorded duration, at least
60 s, and an unrecorded case gets 600 s. They are three times longer for unoptimized
(`-O0`) builds, `GLOB2_TEST_TIMEOUT_SCALE` overrides the multiplier and `--timeout` replaces
every timeout. `--filter` or `--tag`
without `--tier` selects from the full inventory, so naming a case always runs it.

`--affected` narrows the selection to the cases a change can reach, using the
per-case coverage map that master's Clang coverage job records
(`python3 test/select_affected.py --fetch` downloads it to
`artifacts/coverage-map.json.gz`). A case runs when the files it executed
intersect the change, when it has no record yet, or when it is on the always-run
list (golden simulation, save formats, replays); a change to a header, the build
system, `test/`, `content/` or CI configuration, or a map older than seven days or
not from an ancestor, runs the whole tier. `test/select_affected.py --list` shows
the choice without running anything.

`test/run_tests.py` lists the cases with doctest's `-ltc`, applies `--tier`, `--filter`
(suite/name globs), `--tag`, `--exclude-tag` and `--shard K/N`
(deterministic by sorted name), then runs each engine case in its own process with
a fresh `GLOB2_USER_DATA_DIR`, `HOME`, temp directory and SDL's dummy drivers, a
hang-guard timeout from its recorded duration, output captured and shown only on failure, and a check that the
profile's preferences were not rewritten. `[display]` cases get a real video driver,
under `xvfb-run` on Linux without `DISPLAY`, with an isolated Openbox window
manager to apply SDL3 fullscreen requests. Install `xvfb`, `xauth`, `openbox` and
`x11-utils`. The session waits for Openbox's startup callback so client-event
initialization is complete before the test creates a window. Server resets are disabled so SDL can recreate contexts without racing
X server reinitialization; they are skipped on Windows and with
`--no-display`. Results merge into one JUnit file (`--junit`) and, under GitHub
Actions, into the step summary with a `::error file=,line=` annotation per failure.
`test/test_run_tests.py` covers the runner itself.
Each test executable prints a `GLOB2_TEST_PROVENANCE` line naming the source tree,
compiler and flags that produced it. Native builds write that identity to
`<binary>.provenance.json` after linking (`buildsys/test_provenance.py`), so a commit or
stray untracked file that changes no compiled input recompiles and relinks nothing;
browser and mobile test executables embed it instead.
The runner escapes commas and backslashes in selected names and checks that JUnit
records every selected case; a successful exit with missing tests is an error.
Sanitizer diagnostics fail the run even when the process exits successfully.
Failures after a passing doctest report, including leak checks at process exit,
are also recorded in the merged JUnit results.

Standard runs keep display tests windowed. The HD artwork integration test's
fullscreen camera-continuity checks and the text raster test's fullscreen
downscaling check run only with `--fullscreen`; all their windowed checks still
run by default, including with `--in-process`. Linux CI enables `--fullscreen`
under its virtual display, with `--display-jobs 1` to avoid concurrent software
renderer startup stalls. Headless cases remain parallel. Linux timeout reports
include the owned process group, thread wait locations and Xvfb window mapping state; GitHub Actions also
collects a bounded GDB backtrace before cleanup when available. To opt in when invoking a test binary directly, set
`GLOB2_TEST_FULLSCREEN=1`; the Python runner overrides that variable according to
its flag, so an inherited setting cannot enable fullscreen in a standard run.

On macOS, display cases keep their windows off the desktop so a run doesn't take
over the screen or keyboard focus: the runner and the test entry point set
`GLOB2_TEST_HIDDEN_WINDOWS=1`, which makes `GraphicContext` create its window
hidden and sets SDL's background-app and no-activation hints, so windows that
tests create with `SDL_CreateWindow` directly don't activate the process either.
Readback and captures read the rendering context, not the screen, so results
are the same. `--fullscreen` turns this off because fullscreen checks need the
screen; set `GLOB2_TEST_HIDDEN_WINDOWS=0` to watch a run, or `=1` to opt in on
other desktops.

Windows CI replays native engine access violations and CRT aborts under GDB with a fresh profile
and separate artifacts. Harnesses retain function names for backtraces; shipped
programs keep their normal release stripping. Logs appear in
`artifacts/tests/crash-diagnostics/`. Diagnostic replays are bounded to 180 seconds
per case and never replace the original failed result. CRT assertion/abort entry
points have pending breakpoints so the stack is captured before process exit.
For generator teardown leaks, the Windows x64 replay also enables QuickJS leak
reporting at the exact runtime-destruction entry point and prints the resulting
dump flags to verify activation. It flushes the named Windows CRT used by the
assertion so a different loaded CRT cannot hide buffered output. This affects only
the replay under GDB, not
the original test or the shipped program.
The manual `windows-generator-diagnostics.yml` workflow takes an exact `revision`,
builds the engine harness with the normal MinGW release flags/dependencies, and
runs the ScriptGenerator suite, including optional examples and prototype ownership,
before retaining a GDB replay of any failed case.
It enables `GLOB2_GENERATOR_PROTOTYPE_DIAGNOSTICS=1` to retain prototype cache
insertion/release types and addresses; ordinary runs leave this trace disabled.
It preserves that case's failure and is focused diagnostic evidence, not a full
Windows or development checkpoint.

Running a binary by hand is safe too: `TestMain.cpp` creates a temporary profile
and selects the dummy drivers when the environment does not, so
`build/darwin/client/release/test/glob2-unit-tests -ts=MapQuery` never touches
`~/.glob2`. Doctest's own options apply: `-ltc`, `-tc=`, `-ts=`, `-s`, `-r=junit`.

## Writing a test

Include `Glob2Test.h` (unit tests) or the engine fixtures a test uses (below) and use
doctest's `TEST_SUITE`, `TEST_CASE`, `SUBCASE`, `CHECK`, `REQUIRE`, `CHECK_EQ` and
`REQUIRE_MESSAGE`. Suites are named after the area (`HungryDefeat`, `MapQuery`,
`Maxima.Combat`); case names are descriptive sentences. Conditions that doctest
cannot decompose (`a && b`) use `GLOB2_REQUIRE(cond, message)` or `GLOB2_CHECK`.
Tags go at the end of the name, or through `GLOB2_TEST_CASE(name, "[display][slow]")`:

| Tag | Meaning |
| --- | --- |
| `[display]`, `[display:WxH]` | needs a real window (xvfb on Linux) |
| `[slow]` | over a minute (skipped by `--quick` and the headless tier) |
| `[network]` | binds loopback sockets; never runs alongside another `[network]` case |
| `[artifacts]` | writes review evidence under `glob2test::artifactDir()` |
| `[golden]` | compares against a checked-in text; `--update-fixtures` rewrites it, and `GLOB2_TEST_SIM_TRACE_DIR=<dir>` also writes the actual text there for `tools/sim_fingerprint.py` |
| `[sim-trace]` | records a trace with `recordSimTrace` and no checked-in text: `GLOB2_TEST_SIM_TRACE_DIR=<dir>` writes it for `tools/sim_fingerprint.py`, which compares it between the merge base and the pull request; the case asserts its behaviour with direct checks |
| `[writes-preferences]` | legitimately saves settings |
| `[benchmark]` | timing evidence, not pass/fail; runs only with `--tag benchmark` |
| `[map-generators]` | the full generator contract; CI runs it in the map-generators job with `--tier full` (the cases are also `[slow]`), the shards use `--exclude-tag map-generators` |
| `[maxima]`, `[save-format]` | selection only |

Assert the property a count stands for, not the count. Check content against the
catalog, manifest or file it comes from (every ruleset in `data/rulesets.json` loads,
every script the renderer switches to has a shipped font), so adding an entry keeps
the test green and dropping one fails it. Keep a literal only when it is a format
contract, such as a wire or save layout, a frozen legacy table or a `static_assert`
on struct layout, and say why next to it.

Simulation tests detect bugs, not change. Assert the direct invariant the behaviour
promises (a conserved material ledger, every AI order accepted, a winner declared at
the decision tick), prefer a property across seeds (`Property.h`) to one hand-picked
case, and check determinism relationally: the same input twice, save/reload against
an uninterrupted run (`Continuation.h`), threaded against serial. Never commit a
stored checksum or trace just to notice that output moved: tag the scenario
`[sim-trace]` and call `recordSimTrace`, and sim-diff compares it between the merge
base and the pull request, where a difference without a `SIM_REVISION` bump fails. A
`[golden]` fixture is for compatibility only: bytes another program or an old file
must keep matching.

Correctness cases never assert on elapsed wall-clock or CPU time: CI runners and the
`-O0` coverage build are slow and noisy. Assert a behavioural bound with an operation
count (ticks, frames, wake-ups, messages, cache hits) or an injected clock, as
`RenderFramePacerTest`, the turn harness's simulated network clock and the GameSpeed
pacing check (`Engine::sessionDelay` on given timestamps) do. A loop waiting for
something to happen may keep a generous deadline purely as a hang guard that fails
with a clear message. A timing or latency bound belongs in a separate `[benchmark]`
case.

Where a test file lives: beside the code it tests, in that code's directory under
`src/`, `libs/gag/src/`, `libs/usl/test/`, `third_party/natsort/` or `platforms/mobile/`, named `*Test.cpp`,
`*Harness.cpp`, `*Benchmark.cpp` or `*Fixture.cpp` so CI and coverage tell it from
production code (`is_test_source` in `.github/scripts/ci_policy.py`). Tests that span
domains, shared support, stubs and fixtures stay in `test/`. Registry entries outside
`test/` are written from the repository root, as `'#src/unit/UnitTimingTest.cpp'`.

Which binary a test joins, smallest first:

- `glob2-unit-tests` (`UNIT_TESTS`) if it needs neither `GlobalContainer` nor any
  client object beyond `UNIT_PRODUCTION_SOURCES`;
- `glob2-map-tests` (`MAP_TESTS`) if it needs only the map layer: terrain, cells,
  resources, fields, gradients, routing, pathfinding, claims and map I/O, with no
  `Game`, team, building or unit attached to the map;
- `glob2-sim-tests` (`SIM_TESTS`) if it builds a `Game(nullptr)` (or teams, units,
  buildings, AIs, orders, replays) under `HeadlessGlobals` and steps it itself, with
  no `GameGUI`, `HeadlessGame`, window, menu, renderer or `GlobalContainer::load()`
  (`loadStrings` or `display` options);
- otherwise `glob2-engine-tests` (`ENGINE_TESTS`).

Add the translation unit to that list in `test/tests.py` (with `cxxflags`, `defines`
or `require={'wss','not-mingw','opengl'}` when needed); CI picks the new cases up on
the next run. A module entry also needs a `MODULE_CASE_FLOORS` count: `run_tests.py`
refuses to run when a module test file lists fewer cases than its floor, or when a
binary lists cases from a file registered to another one, so a case moved out of
engine or unit cannot silently disappear. Raise the floor with new cases. If a
module binary fails to link, the missing symbol names the layer the test really
needs: `python3 tools/link_graph.py --within map closure map/Map.o --external`
(or `--within map+sim`) lists what each module still takes from outside it, and
`path A.o B.o` shows the reference chain. Move such a test to the next binary
rather than adding a stub, unless the stub only replaces an unreachable weld; the
stubs in `test/map/stubs/` and `test/sim/stubs/` abort, so a test that reaches one
fails loudly. Moved cases keep their runner policy and coverage labels:
`run_tests.py --binary engine` includes `glob2-sim-tests` and `--binary unit`
includes `glob2-map-tests`, and the mobile and browser builds fold them back into
their engine and unit binaries (`portable_engine_tests()`, `portable_unit_tests()`). Keep helpers and fixtures inside an anonymous namespace: every test
file in a binary is one link, so two global `struct World`s collide.

`test/support/Glob2Test.h` provides, in `namespace glob2test`:

- `sourceRoot()`, `fixture("dir/file")`, `inflated("dir/file.gz")` for repository data;
- `profileDir()`, `TempDir`, `artifactDir()` for scratch and review output;
- `CapturedStdout` / `CapturedStderr` (no `freopen`), `expectGolden(relative, text)`,
  `readFile`, `writeFile`, `updatingFixtures()`;
- `ToolkitScope` for tests that need a `FileManager` without an engine.

Engine tests include only the fixture headers they use, so a test that needs no
game or GUI does not compile against them:

- `HeadlessGlobals.h`: `HeadlessGlobals`, the RAII `GlobalContainer` every headless
  harness used to bootstrap by hand (`runNoX`, building types, races, key actions,
  the repository on the data path, a seeded simulation RNG; `Options` select a real
  display, string loading, screen size and seed);
- `HeadlessGame.h`: `HeadlessGame`, a one-colony game on a small grass torus owned
  by its `GameGUI`, with `addBuilding`, `addUnit`, `step` and `checksum`;
- `GameFixtures.h`: helpers that drive any `Game` and need no GUI: `BoundGameRandom`,
  `stepAI`, `stepAIValidated`, `publishCurrentHiringSuggestions`,
  `discardPrematureHiringSuggestions` and `prepareTurretTargets`. `stepAI` polls the
  AIs synchronously; `stepAIValidated` follows the engine's production AI path
  (observation, admission, the AI order delay, the pipeline's stale-target
  withdrawal) and returns each executed order with the `OrderValidation::validate`
  verdict taken just before it executed. `AIOrderValidity/*`
  (`src/ai/AIOrderValidityTest.cpp`) uses it to require that every registered AI
  issues only Accepted or Stale orders across seeds. Its allowlist of known
  rejections is empty; an entry names one AI, order type and reason, and the run
  fails once the entry is no longer hit;
- `DrawFixtures.h`: `sceneOf`, `drawMap`, `drawGUI` and `observeMap` for draw passes;
- `Continuation.h` (engine binary only): save/load continuation checks, below;
- `SimulationInvariants.h` (sim and engine binaries): `expectBuildingInvariants` and
  the `MaterialLedger`, below.

### Seeded properties and save continuations

Randomized tests use `test/support/Property.h` (header-only, any binary).
`glob2test::forEachSeed(n, [&](std::uint32_t seed) { ... }, firstSeed)` calls the body
for `n` consecutive seeds starting at `firstSeed` (default 1); the body seeds its own
generator from `seed`. Every failing assertion inside it names the seed and the
setting that replays it. Two environment variables steer a run:

- `GLOB2_SEED=<n>` runs only seed `n`, any value, for every `forEachSeed` in the
  process, so pair it with a case filter:
  `GLOB2_SEED=1235 python3 test/run_tests.py --filter 'OrderValidation/*'`;
- `GLOB2_PROPERTY_ITERS=<k>` multiplies every seed count by `k` for a longer soak.

When converting a fixed-seed loop, pass its historical seed as `firstSeed` so the
default run still checks the sequence the test always checked.

`test/support/Continuation.h` checks that a saved game continues exactly like the
game it was saved from. `simulationComponents(game)` is `Game::checkSum`'s full
component vector (game fields including the map-header word, every building,
every unit). With `legacy152Representation` it is the frozen historical layout,
whose map-header word mixes in the format version saving rewrites, so that word is
dropped. `saveGameBytes`,
`saveGUIBytes`, `loadGameBytes` and `loadGameGUI` move a game through memory.
`expectContinuation(game, {.ticks = n, ...})` saves the game, loads it into a fresh
`GameGUI` and steps both in lockstep, each with its own synchronized stream bound,
comparing components, `syncRandom` and the orders the step returns after loading
and after every tick. The first divergence fails the test with its tick and first
differing component, and the result returns the loaded game for further checks.
Options cover the rest: `step` (for example `glob2test::stepAI`, which also compares
AI decisions, or `stepEngine`), `afterLoad` for what a real loader applies after
`Game::load` or for checks of the loaded state, `each` for test-specific comparisons
at every tick, `gui` to save and load through `GameGUI`, and `twoLoads` to compare
two loads of one save when loading deliberately rebuilds unsaved state.

### Invariants and the material ledger

`test/support/SimulationInvariants.h` holds oracles that are true of any live game,
so behaviour tests can state what should hold rather than compare a checksum.
`expectBuildingInvariants(game)` checks `Game::integrity` and, for every live
building, occupancy within `maxUnitInside`, non-negative stock and reservations no
larger than the stock.

`MaterialLedger ledger(game)` then `ledger.check("tick n")` after every step checks
material conservation in two compartments, per material:

- map: the map's stock equals its stock at the start plus measured growth
  (`growthGlobal`), minus decay, harvests and clearings;
- colony: building and team stock plus carried packets (in stock units: one packet
  is the catalog's `materialMultiplier`, 1 for basic materials and 10 for fruit)
  equals the start plus harvests, minus everything recorded in `consumed` (meals,
  births, ammunition, construction, upgrades, services and area upkeep).

The first imbalance fails with every term of the identity and later checks stay
quiet. Flows the measurements count but do not size (`materialSpillageEvents`,
repair consumption, cargo lost with a dead carrier, stock lost with a removed
building, a clearing that removes a whole deposit) make that check one-sided
(nothing may appear from nowhere) and restart an exact segment from the actual
census; a material whose accepting buildings disagree on the multiplier (colony) or
whose deposits are not harvested one unit per event (map; wood in the default
catalog) is not checked. `inexactReasons()` lists every restart and exclusion, and
`colonyExact()`/`mapExact()` say whether there were none. Create the ledger after
the game's first engine step, which starts statistics collection (the constructor
requires it), and declare material a test inserts with `credit()`/`creditMap()`.
`takeMaterialCensus(game)` counts the same quantities without a ledger.

Map and unit tests that exercise `Map` without the game use the `GrassMap` pattern
of `src/map/MapQueryTest.cpp`, which sizes the tile array directly instead of
calling `Map::setSize`. In `glob2-map-tests` the whole map layer is real and only
the simulation symbols it references are stubbed (`test/map/stubs/`). In
`glob2-unit-tests` the `Sector`, `MapHeader`, `Order`, `GameGUI`, `Race` and SHA-1
symbols such tests reach are supplied once by `test/unit/stubs/`, so every unit
test shares one link surface; a unit test that needs a stub replacing a symbol
another unit test links for real belongs in the map, sim or engine binary instead.

Time in input tests is injected, never read from `SDL_GetTicks`. Event
timestamps (`event.tfinger.timestamp`, `event.common.timestamp`) and the frame
clock a test passes to `Host::update(tick)`, `GameGUI::step(events, now)`,
`GameGUITouch::advanceScroll(now)` or `PhoneEditor::advance(tick)` are the only
sources the touch scroll physics sees, so a fling, bounce or stopped-finger
rule is exact and repeatable (`libs/gag/src/ScrollPhysicsTest.cpp`,
`libs/gag/src/ui/UILayoutHarness.cpp`, `src/hud/touch/GameGUITouchHarness.cpp`). Events without a
timestamp carry no velocity, which is why older synthetic gestures never coast.

Mac gesture scrolling uses the same injected timestamps and frame clocks. Run
`GestureScroll/*`, `ScrollPhysics/*`, `UILayout/*` and `EventQueue/*` with the unit runner. Use the engine runner for
`ScreenExecution/*`, which exercises screen-stack lifecycle and queued dispatch. The Mac-only engine suite
`MacScrollMonitor/*` uses synthetic Cocoa samples to check phase conversion,
zero-delta termination, native momentum sequence continuity, conventional-wheel
pass-through and teardown. The `GameGUITouch` scroll-physics display case also
checks native HUD capture, editor tray axis selection and cancelled momentum;
its map zoom case checks gesture wheel fallback. Synthetic checks establish event
contracts, not physical trackpad feel: verify that separately in windowed and
fullscreen modes, at Retina scaling, and with conventional and gesture mice.


## Domain verification guides

Use the [ordered verification hub](../docs/development/testing/README.md) for simulation, compatibility, AI, maps, rendering, audio, scripting, platforms and tooling scenarios.

## CLI compatibility

`CommandLine/*` tests registry definitions, strict parsing, repeats, and conflicts.
The real executable must also pass static help, documented workflows, artifact
contracts, and generated-reference drift checks:

```sh
python3 test/test_cli_smoke.py --binary /path/to/glob2 --artifacts artifacts/cli
python3 tools/cli_reference.py --binary /path/to/glob2 --check
```

See the [CLI guide](../docs/tools/cli.md) for command and migration contracts.

CPU rendering changes use `ComputeExecutor`, `SurfaceRaster`, `SoftwareRenderer`,
`TerrainMaterials`, `TerrainPresentation` and `RenderDevice` alongside window,
backend-switch and recording integration suites. Executor tests control callback
claims, cancellation and release order, check that graphics barriers do not pump
simulation or scene work, and retain tickets across lifecycle changes. Regional
raster checks compare complete regions and disjoint tiles exactly; software
recorder cases also check pending source destruction, shared backing views,
capacity flushes, fallback ordering and failure/discard recovery. Run rendering
integration with both `GLOB2_RENDER_EXECUTION=serial` and `parallel`; the default
`immediate` retains direct serial drawing without batching. Preserve independent base-revision captures
because shared-kernel comparisons alone cannot prove unchanged pixel arithmetic.
