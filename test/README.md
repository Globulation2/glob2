# Native tests

Native tests are doctest cases compiled into two binaries by the main build, from
the same objects as the game:

| Binary | Links | How it runs |
| --- | --- | --- |
| `glob2-unit-tests` | libgag, libusl, a few production sources and the stubs in `test/unit/stubs/` | headless cases share a process; display cases run in separate processes |
| `glob2-engine-tests` | every client object except the entry point | one process per test case, each in a disposable profile |

Both are listed in `test/tests.py`, built by `test/SConscript` and land in
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
python3 tools/dev_build.py --build=build/local-tests tests # or unit-tests / engine-tests
export GLOB2_BUILD_DIR="$PWD/build/local-tests"
python3 test/run_tests.py                   # everything this platform can run
python3 test/run_tests.py --list --tag display
python3 test/run_tests.py --fullscreen --tag display      # opt in to fullscreen transitions
python3 test/run_tests.py --binary unit
python3 test/run_tests.py --filter 'HungryDefeat/*' --verbose
python3 test/run_tests.py --binary engine --shard 2/4 --junit artifacts/tests/junit.xml
python3 test/run_tests.py --binary engine --in-process     # fast local loop, no isolation
python3 test/run_tests.py --update-fixtures --filter 'WinningConditions/*'
```

`ColonySkinPreview` checks shared image preparation with independent appearance
authorization, refresh, expiry and cancellation across preview owners.

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
`SpriteSheets` also checks renderer readiness and atomic HD reload publication. Build the
`asset-loading-benchmark` target to compare worker configurations against the same
runtime assets. See [asset loading](../docs/development/package-size.md#release-asset-and-bundle-sizes)
for worker controls, scratch accounting and measurement limits.

`test/run_tests.py` lists the cases with doctest's `-ltc`, applies `--filter`
(suite/name globs), `--tag`, `--exclude-tag`, `--quick` and `--shard K/N`
(deterministic by sorted name), then runs each engine case in its own process with
a fresh `GLOB2_USER_DATA_DIR`, `HOME`, temp directory and SDL's dummy drivers, a
timeout by tag, output captured and shown only on failure, and a check that the
profile's preferences were not rewritten. `[display]` cases get a real video driver,
under `xvfb-run` on Linux without `DISPLAY`, with an isolated Openbox window
manager to apply SDL3 fullscreen requests. Install `xvfb`, `xauth`, `openbox` and
`x11-utils`. The session waits for Openbox's startup callback so client-event
initialization is complete before the test creates a window. Server resets are disabled so SDL can recreate contexts without racing
X server reinitialization; they are skipped on Windows and with
`--no-display`. Results merge into one JUnit file (`--junit`) and, under GitHub
Actions, into the step summary with a `::error file=,line=` annotation per failure.
`test/test_run_tests.py` covers the runner itself.
The runner escapes commas and backslashes in selected names and checks that JUnit
records every selected case; a successful exit with missing tests is an error.

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

Include `Glob2Test.h` (unit tests) or `EngineFixtures.h` (engine tests) and use
doctest's `TEST_SUITE`, `TEST_CASE`, `SUBCASE`, `CHECK`, `REQUIRE`, `CHECK_EQ` and
`REQUIRE_MESSAGE`. Suites are named after the area (`HungryDefeat`, `MapQuery`,
`Maxima.Combat`); case names are descriptive sentences. Conditions that doctest
cannot decompose (`a && b`) use `GLOB2_REQUIRE(cond, message)` or `GLOB2_CHECK`.
Tags go at the end of the name, or through `GLOB2_TEST_CASE(name, "[display][slow]")`:

| Tag | Meaning |
| --- | --- |
| `[display]`, `[display:WxH]` | needs a real window (300 s timeout, xvfb on Linux) |
| `[slow]` | over a minute (600 s timeout; skipped by `--quick`) |
| `[network]` | binds loopback sockets; never runs alongside another `[network]` case |
| `[artifacts]` | writes review evidence under `glob2test::artifactDir()` |
| `[golden]` | compares against a checked-in text; `--update-fixtures` rewrites it |
| `[writes-preferences]` | legitimately saves settings |
| `[benchmark]` | timing evidence, not pass/fail; runs only with `--tag benchmark` |
| `[map-generators]` | the full generator contract; CI runs it in the map-generators job, the shards use `--exclude-tag map-generators` |
| `[maxima]`, `[save-format]` | selection only |

Where a test file lives: beside the code it tests, in that code's directory under
`src/`, `libgag/src/`, `libusl/test/`, `natsort/` or `mobile/`, named `*Test.cpp`,
`*Harness.cpp`, `*Benchmark.cpp` or `*Fixture.cpp` so CI and coverage tell it from
production code (`is_test_source` in `.github/scripts/ci_policy.py`). Tests that span
domains, shared support, stubs and fixtures stay in `test/`. Registry entries outside
`test/` are written from the repository root, as `'#src/unit/UnitTimingTest.cpp'`.

Which binary a test joins: `glob2-unit-tests` if it needs neither `GlobalContainer` nor any
client object, otherwise `glob2-engine-tests`. Add the translation unit to
`UNIT_TESTS` or `ENGINE_TESTS` in `test/tests.py` (with `cxxflags`, `defines` or
`require={'wss','not-mingw','opengl'}` when needed); CI picks the new cases up on
the next run. Keep helpers and fixtures inside an anonymous namespace: every test
file in a binary is one link, so two global `struct World`s collide.

`test/support/Glob2Test.h` provides, in `namespace glob2test`:

- `sourceRoot()`, `fixture("dir/file")`, `inflated("dir/file.gz")` for repository data;
- `profileDir()`, `TempDir`, `artifactDir()` for scratch and review output;
- `CapturedStdout` / `CapturedStderr` (no `freopen`), `expectGolden(relative, text)`,
  `readFile`, `writeFile`, `updatingFixtures()`;
- `ToolkitScope` for tests that need a `FileManager` without an engine.

`test/support/EngineFixtures.h` adds `HeadlessGlobals` (the RAII `GlobalContainer`
every headless harness used to bootstrap by hand: `runNoX`, building types, races,
key actions, the repository on the data path, a seeded simulation RNG; `Options`
select a real display, string loading, screen size and seed) and `HeadlessGame`
(a one-colony game on a small grass torus with `addBuilding`, `addUnit`, `step`
and `checksum`). Unit tests that exercise `Map` without the game reuse the
`GrassMap` subclass in `src/map/MapQueryTest.cpp`, which sizes the tile array
directly instead of calling `Map::setSize`; the `Sector`, `MapHeader`, `Order`,
`GameGUI`, `Race` and SHA-1 symbols such tests reach are supplied once by
`test/unit/stubs/`, so every unit test shares one link surface. A test that needs a
stub replacing a symbol another unit test links for real belongs in the engine
binary instead.

Time in input tests is injected, never read from `SDL_GetTicks`. Event
timestamps (`event.tfinger.timestamp`, `event.common.timestamp`) and the frame
clock a test passes to `Host::update(tick)`, `GameGUI::step(events, now)`,
`GameGUITouch::advanceScroll(now)` or `PhoneEditor::advance(tick)` are the only
sources the touch scroll physics sees, so a fling, bounce or stopped-finger
rule is exact and repeatable (`libgag/src/ScrollPhysicsTest.cpp`,
`libgag/src/ui/UILayoutHarness.cpp`, `src/hud/touch/GameGUITouchHarness.cpp`). Events without a
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
