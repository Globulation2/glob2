# glob2/test/

Native tests are doctest cases compiled into two binaries by the main build, from
the same objects as the game:

| Binary | Links | How it runs |
| --- | --- | --- |
| `glob2-unit-tests` | libgag, libusl, a few production sources and the stubs in `test/unit/stubs/` | one process, in-process |
| `glob2-engine-tests` | every client object except the entry point | one process per test case, each in a disposable profile |

Both are listed in `test/tests.py`, built by `test/SConscript` and land in
`build/<toolchain>/client/release/test/` (`darwin`, `linux` or `mingw`; `--build=DIR`
and `GLOB2_BUILD_DIR` override the directory as for the game). The standalone
programs that remain come from the `PROGRAMS` table in the same file
(`MapReportHarness`, `MenuColonyHarness`, and the `glob2-tools` group) or from
`src/SConscript` (`MapGeneratorGoldenTest`, `LANSessionHarness`, the transport
programs and the map generator study tools). The per-harness
aliases documented below are kept as `LEGACY_ALIASES` for one release; they build
the binary that now contains the test.

## Build and run

```sh
scons -j8 release=1 server=0 tests          # or unit-tests / engine-tests
python3 test/run_tests.py                   # everything this platform can run
python3 test/run_tests.py --list --tag display
python3 test/run_tests.py --fullscreen --tag display      # opt in to fullscreen transitions
python3 test/run_tests.py --binary unit
python3 test/run_tests.py --filter 'HungryDefeat/*' --verbose
python3 test/run_tests.py --binary engine --shard 2/4 --junit artifacts/tests/junit.xml
python3 test/run_tests.py --binary engine --in-process     # fast local loop, no isolation
python3 test/run_tests.py --update-fixtures --filter 'WinningConditions/*'
```

`test/run_tests.py` lists the cases with doctest's `-ltc`, applies `--filter`
(suite/name globs), `--tag`, `--exclude-tag`, `--quick` and `--shard K/N`
(deterministic by sorted name), then runs each engine case in its own process with
a fresh `GLOB2_USER_DATA_DIR`, `HOME`, temp directory and SDL's dummy drivers, a
timeout by tag, output captured and shown only on failure, and a check that the
profile's preferences were not rewritten. `[display]` cases get a real video driver,
under `xvfb-run` on Linux without `DISPLAY`, with server resets disabled so SDL
can recreate contexts without racing X server reinitialization; they are skipped on Windows and with
`--no-display`. Results merge into one JUnit file (`--junit`) and, under GitHub
Actions, into the step summary with a `::error file=,line=` annotation per failure.
`test/test_run_tests.py` covers the runner itself.

Standard runs keep display tests windowed. The HD artwork integration test's
fullscreen camera-continuity checks and the text raster test's fullscreen
downscaling check run only with `--fullscreen`; all their windowed checks still
run by default, including with `--in-process`. Linux CI enables `--fullscreen`
under its virtual display. To opt in when invoking a test binary directly, set
`GLOB2_TEST_FULLSCREEN=1`; the Python runner overrides that variable according to
its flag, so an inherited setting cannot enable fullscreen in a standard run.

Running a binary by hand is safe too: `TestMain.cpp` creates a temporary profile
and selects the dummy drivers when the environment does not, so
`build/darwin/client/release/test/glob2-unit-tests -ts=MapQuery` never touches
`~/.glob2`. Doctest's own options apply: `-ltc`, `-tc=`, `-ts=`, `-s`, `-r=junit`.

## Writing a test

Include `Glob2Test.h` (unit tests) or `EngineFixtures.h` (engine tests) and use
doctest's `TEST_SUITE`, `TEST_CASE`, `SUBCASE`, `CHECK`, `REQUIRE`, `CHECK_EQ` and
`REQUIRE_MESSAGE`. Suites are named after the area (`HungryDefeat`, `MapQuery`,
`Maxima.Combat`); case names are sentences without commas. Conditions that doctest
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

Where a test goes: `glob2-unit-tests` if it needs neither `GlobalContainer` nor any
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
`GrassMap` subclass in `test/MapQueryTest.cpp`, which sizes the tile array
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
rule is exact and repeatable (`test/ScrollPhysicsTest.cpp`,
`test/UILayoutHarness.cpp`, `test/GameGUITouchHarness.cpp`). Events without a
timestamp carry no velocity, which is why older synthetic gestures never coast.

## Python tests

`test/test_*.py` are `unittest` files; those that need a build take the binary
path on the command line (`test_map_cli.py`, `test_map_report.py`). The `check_*.py`
scripts compare full-game traces against retained fixtures and are documented with
the harness they accompany below. `tests/` at the repository root tests the build
system and the browser services.

## Maxima

See [Maxima tests](maxima/README.md) for policy, configuration, integration and
saved-game continuation coverage.

## Selection lifetime regression

`GameGUISelectionHarness.cpp` links the real client objects with a test entry
point. It exercises selected building/unit deletion before the next GUI draw,
null selections, and a live unit with no peer. It runs headlessly, using the real
`GameGUI`, entity classes, selection setters, and destruction hooks. A friend
fixture accesses the private selection API without exposing it to game callers.

Runs as the `GameGUISelection` suite of `glob2-engine-tests`:

```sh
python3 test/run_tests.py --filter 'GameGUISelection/*'
```

For AddressSanitizer and UndefinedBehaviorSanitizer on macOS or Linux, build the
engine tests into a separate directory with the sanitizer flags and run the same filter:

```sh
scons -j8 release=0 server=0 --build=build/tests-asan engine-tests \
  CXXFLAGS='-g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  LINKFLAGS='-g -fsanitize=address,undefined'
python3 test/run_tests.py --build-dir build/tests-asan --filter 'GameGUISelection/*'
```

If Homebrew sdl2-compat cannot locate SDL3 under the macOS sanitizer, prefix
the harness command with `DYLD_LIBRARY_PATH=/opt/homebrew/lib`.

SCons caches compiler/linker flags; pass `CXXFLAGS=-g LINKFLAGS=-g` to return to a
normal build. This is a direct method regression, not an interactive replay test.

## Terrain resource regression

The `TerrainResources` suite (`python3 test/run_tests.py --filter 'TerrainResources/*'`)
links the actual client objects and exercises terrain regeneration and resource clearing for all eight
resource types, all three base terrains, overlapping strokes, and all four
wrapped map corners. A whole-map oracle checks both removal and preservation.
These are headless map-operation tests; they do not drive editor mouse events.

## Map generator golden maps and colony sweep

From the repository root, `scons -j8 release=1 server=0 map-generator-golden-test` builds
`build/native-tests/src/MapGeneratorGoldenTest`. Run it as `./build/native-tests/src/MapGeneratorGoldenTest <profile>`
to compare this platform's rows of `test/map-generator-golden.txt` against fresh rolls, with
`--update` after a revision bump, `--print` to bootstrap a platform's rows from a log, and
`--sweep` to roll every playable landscape at the lobby's colony counts and sizes. A platform
with no rows reports and passes, so a new machine can run the check before its rows exist;
`--require-rows` makes that a failure instead, which is what CI runs, so the table must carry
rows for every platform CI builds on (`linux-x86_64` today, next to the maintainers'
`macos-arm64`). Rows for a platform you cannot build on come from the `--print` output in its
CI log, which the workflow prints before the check. The framework reference under
`docs/map-generators/` describes the rules it enforces.

`MapGeneratorGoldenTest <profile> --telemetry` compares telemetry enabled/disabled and repeated
attempts for all registered generators at three seeds, including complete serialized worlds and
RNG restoration. It prints generation-only timings and record counts; timing is diagnostic, not a
flaky performance threshold. `python3 test/test_map_telemetry.py` tests the bulk collector's failure
retention and aggregation. The existing JSON-report test covers typed telemetry, malformed reports,
service failures and raw invalid requests. See [telemetry](../docs/map-generators/TELEMETRY.md).

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

`scons -j8 release=1 server=0 map-generator-profile-fixture` builds
`build/native-tests/src/MapGeneratorProfileFixture <profile-dir> <seed> <rounds>`, which round-robins every
registered generator for `rounds` passes with parameters (shared and generator-specific) drawn
at random the same way `GenerationRequest::randomizeControls` does, and prints a per-generator
attempt/success/timing table. It exists to give an external sampling profiler (macOS `sample`,
Linux `perf record`) a sustained, representative mix of real generation work to attach to; run it
with a large round count in the background and sample its PID. It is not a regression test (see
`--sweep` and `--performance` above for that) and is not wired into CI, but a fixed seed and round
count also make it a quick way to compare a shared primitive's before/after cost: attempt/success
counts per generator repeat exactly across a behavior-preserving change, so a diff there is a
signal something changed, not just an optimization's timing.

## Android device execution

Selected client harnesses can run as native Android executables on a connected
device. See the [device build and runner
commands](../docs/mobile/development.md#native-tests-on-a-connected-android-device).
Shell tests use SDL dummy video and have no audio or Java Activity; installed-APK
interaction and lifecycle tests are separate. `python3 test/test_mobile_asset_bundle.py`
checks the asset-index packaging regression independently of an Android SDK.

## Map subclass test pattern

Pattern used by `MapQueryTest.cpp` (commit `2d42c340`). Lets you write tests against `Map`'s predicates with a minimal link surface — no `globalContainer`, no real `Sector` array, no transitive pull of `Bullet` / `Team` / `Building` / `Unit` into the test binary.

### The fixture: subclass `Map`, bypass `setSize`

`Map::setSize()` does `new Sector[sizeSector]`, which forces the linker to resolve every `Sector` method (vtable + transitively `Bullet`, `Team::pushGameEvent`, `Building::kill`, `Unit::getRealArmor`, `globalContainer`, ...). Bypass it:

```cpp
struct GrassMap : Map {
    GrassMap() {
        wDec = 3; hDec = 3; w = 8; h = 8;  // 8x8 map
        wMask = 7; hMask = 7;
        size = 64;
        cases.assign(64, Case{});           // default: terrain=0 (grass), no bldg/unit
        // No Sector or auxiliary arrays are allocated.
    }
    ~GrassMap() {
        // Reset fixture dimensions before base cleanup.
        w = h = wMask = hMask = wDec = hDec = 0;
        size = 0;
    }
};
```

`cases`, `w` / `h` / `wMask` / `hMask` / `wDec` / `hDec` are all public on `Map`. `arraysBuilt` is also public. Default-constructed `Case` is "grass tile, no occupant, terrain=0, ressource.type=NO_RES_TYPE".

### Stubs for `Sector`

`test/unit/stubs/MapSectorStubs.cpp` (linked into every unit binary through `UNIT_STUBS` in `test/tests.py`) provides empty bodies for `Sector::Sector(Game*)`, `Sector::~Sector`, `Sector::setGame`, `Sector::step`, `Sector::save`, `Sector::load`, `Sector::free`, and `UnitDeathAnimation::UnitDeathAnimation`. `Map.o`'s compiled `setSize` / `setGame` reference `Sector` symbols even though the test never calls them. ~30 lines of stubs avoid pulling all of `Sector.cpp`'s real deps.

### Build wiring

Add the translation unit to `UNIT_TESTS` in `test/tests.py`. The production sources the unit binary links (`Map.cpp`, `MapQuery.cpp`, `MapTerrain.cpp`, `BitArray.cpp`, `Utilities.cpp`, `building/BuildingUtils.cpp`, `unit/UnitUtils.cpp`, ...) are listed once in `UNIT_PRODUCTION_SOURCES`; the include paths come from the client build environment, so nothing per-test is needed.

### Terrain encoding for tests

To poke `cases[i].terrain` directly (`regenerateMap` is protected): grass < 16, sand 128–143, water 256–271. See `Map.h:336-361`.

### When to use this pattern

- Testing other Map behaviors (`doesUnitTouch*`, `doesPosTouch*`, `setClearingArea*`, `markImmobileUnit`, etc.).
- Adding regression tests around any Map state mutator before refactoring it.
- **Don't use** for behaviors that genuinely need real `Game` / `Team` / `Unit` / `Building` wiring (e.g. `doesUnitTouchEnemy` reaches into `game->teams[]->myBuildings[]`) — those need either a different stub set or a refactor to decouple first.

## Real LAN session regression

From the repository root:

```sh
scons -j2 release=1 server=0 lan-test
python3 test/run_lan_session_test.py build/native-tests/src/LANSessionHarness
```

This runs separate host and joining client processes with real SDL lobby widgets,
YOG anonymous LAN server, game router, and TCP connections. The joiner uses the
actual `LANFindScreen` Connect path. It clicks Ready and Leave Game, then rejoins.
Both cycles force a map download and compare the downloaded `.gz` bytes against
the fixture source (`maps/FourSquares1.map.gz`) byte for byte: the host's private
copy is already gzip-compressed, so the transfer exercises sending a locally
compressed map without gzipping it again, and a new receiver stores the download
as `.gz` without unzipping it. The host verifies readiness, roster size, unique
player IDs, slot masks, and both departures. The map's current size is not hardcoded
in the test. Linux CI runs this automatically with SDL's dummy video/audio drivers.

For two physical machines, run these from each machine's repository root, using
absolute capture prefixes whose parent directories already exist:

```sh
SDL_VIDEODRIVER=dummy ./build/native-tests/src/LANSessionHarness host 127.0.0.1 2 /tmp/lan-host
SDL_VIDEODRIVER=dummy ./build/native-tests/src/LANSessionHarness join HOST_IP 2 /tmp/lan-guest
```

Start the joiner after the host prints `HOST roster=1`. TCP ports 7489 and 7491
must be reachable; this does not connect to the public YOG service. Omit
`SDL_VIDEODRIVER=dummy` to show the real window. Normal game profiles are preserved;
the harness uses `.glob2-lan-test-host` and `.glob2-lan-test-join` profiles containing
only test data. Fixed input timers allow map transfer before leaving; the runner
bounds startup, execution, and child cleanup. Logs and captures are written under
`output/lan-session-test` by default (`--output` overrides it).

## Aspect-ratio and screen-capture regression

The `FullscreenAspect` suite (`test/FullscreenAspectHarness.cpp`) opens a real SDL
window and checks presentation pixels, clipping, logical-resolution screen captures,
and translated mouse motion/button events and polling at equal, wide, tall, odd, and
downscaled window sizes. It exercises the same scaling path used by desktop
fullscreen. The software case also checks every pixel in 24 opaque/translucent
rectangle intersections, including rectangles above the clip area and empty
rectangles. It does not load a game profile or change saved display settings.

```sh
python3 test/run_tests.py --filter 'FullscreenAspect/*'
```

Both cases are tagged `[display:1600x1400]`: the runner opens that Xvfb screen on
Linux without a `DISPLAY` and uses Mesa software OpenGL (`LIBGL_ALWAYS_SOFTWARE=1`)
in CI. The OpenGL case is skipped in `opengl=0` builds. The same goes for the
`WindowResize` suite (`test/WindowResizeHarness.cpp`), which resizes the window
through the cache, callbacks, reflow, context recreation and minimum-size paths and
needs a desktop at least 1100x850 when run natively.

## Wrapped building footprint regression

The `BuildingFootprint` suite (`python3 test/run_tests.py --filter 'BuildingFootprint/*'`)
links the real engine and
round-trips generated fixtures through binary saved games. It checks exact map
occupancy, missing/stale-cell repair, repeated integrity checks, and ground exits
for interior, negative-origin and positive wrapped footprints. It protects the
runtime fix in `fafb5e9a`: the old predicate erased valid wrapped cells on load and
could subsequently abort in `Building::findGroundExit`.

It needs no display, AI tournament tooling, or external save files. Linux CI runs
it on both supported Ubuntu versions.

Saved state and step-by-step before/after reproduction: [PR #165 fixture](fixtures/wrapped-building/README.md).

## Entering unit save regression

The `EnteringUnitSave` suite (`python3 test/run_tests.py --filter 'EnteringUnitSave/*'`)
links the real engine and
round-trips generated fixtures through binary saved games. It exercises eight
entry directions at five interior/edge/corner positions, preserves the building
reference and animation destination, and rejects both a misplaced entering
explorer and stale occupancy for an ordinary explorer. It protects the runtime
fix in `4ce1d5bc`; expected negative controls print integrity diagnostics.

It needs no display, AI tournament tooling, or external save files. Linux CI runs
it on both supported Ubuntu versions.

Saved state and step-by-step before/after reproduction: [PR #166 fixture](fixtures/entering-explorer/README.md).

## Entering unit draw regression

Run `python3 test/run_tests.py --filter 'EnteringUnitDraw/*'`; the case is tagged
`[display:1024x768][artifacts]`.

A unit on its final step into a building keeps its map slot on the tile it is
leaving (`Unit::handleActionEnteringBuilding`) while `posX`/`posY` already name
the building tile, so `Game::drawMapGroundUnits` visits it one square behind
itself. Nothing in `Game::drawUnit` reads `displacement`, so at equal `delta`
that state must render pixel-for-pixel like the same step expressed as an
ordinary walk onto the destination tile. The harness renders both and compares
framebuffers over five points of one step; a mismatch is reported in pixels
against the 32 px tile size. It protects the fix in `src/render/UnitDrawGeometry.h`
for issue #230, where the sprite was anchored on the stale map slot and the glob
walked backwards into the square it came from.

Frames land in the case's artifact directory (`artifacts/tests/EnteringUnitDraw/...`
or `GLOB2_TEST_ARTIFACTS`): `entering-delta<N>.png` and, on failure,
`walking-delta<N>.png` for the unit-only comparison, plus `scene-delta<N>.png` with
terrain and the inn for looking at by eye. The comparison itself stays on the
unit-only render, which has no animated water or clouds to make two frames differ
by themselves. The `FailingUnitMarkers` suite works the same way and leaves its
scene captures next to them. Both need a display and a GL context and are left
out of `opengl=0` builds.

## Immobile unit gradient regression

The `ImmobileUnitGradient` suite (`python3 test/run_tests.py --filter 'ImmobileUnitGradient/*'`)
uses a fresh 64x64 map
and real engine orders to check empty immobile-unit bookkeeping, exact blocked
cells, and immediate building-route invalidation after painting and erasing a gap.
It exercises all seven swim classes on weighted full-map gradients. No display or
external save fixture is needed; normal game data must be available.

Each scenario is its own case (`fresh map has no immobile units`, `immobile unit blocks
its own tile`, `painting forbidden area refreshes gradients`); the latter two clear the
initial occupancy explicitly, so failures in painting or occupancy can be reproduced
independently of the fresh-map initialization bug. Linux CI runs all of them.

## Building gradient invalidation regression

The building invalidation harness also checks that public distance and movement
queries resolve their own inputs, that the array API returns a complete field,
and that idle and invalidated field storage and search queues can be reused
without stale routes.

Building propagation is always lazy. The `PathGradient` unit suite
(`python3 test/run_tests.py --binary unit --filter 'PathGradient/*'`) has an independent heap oracle that
covers all seven swim classes, paused water snapshots, equal-cost movement neighbors,
repeated and reordered requests, interleaved maps, disconnected/capped distances,
toroidal geometry and reused frontiers. The existing eager-kernel cases remain.

The building invalidation and immobile-unit harnesses exercise lazy callers without
configuration switches. The invalidation suite also pauses a field, closes a rival
building ring, then checks that the field retains its old obstacle snapshot only
until the normal refresh deadline. The savegame safety harness covers completion
on save in the standard game path.
CI runs the oracle on Linux and Windows, the lazy invalidation/immobile-unit suites
on Linux, and lazy save continuation on both.

The `BuildingGradientInvalidation` suite (`python3 test/run_tests.py --filter
'BuildingGradientInvalidation/*'`) links the real engine and places every building through `OrderCreate` / `OrderDelete`, so it
exercises `Game::addBuilding` and `Team::syncStep` rather than a test double. On a
fresh 64x64 grass map with two teams, it checks that a cached route field notices
the ground moving under it: a ring of inn sites closed around a site whose field is
already cached stops offering that site to a unit outside, a site placed inside a
standing ring is never offered, and clearing the ring restores it.

Each scenario is its own case (`centre placed inside an existing ring`, `ring placed around
an existing field`, `a rival teams ring cuts off a cached field`, `a ring cuts off a virtual
flags field`); Linux CI runs all of them.

The last two cover what the proximity walk this replaced structurally could not
reach. `ring-other-team` builds the ring as team 1 around team 0's site: the old
invalidation only dirtied the buildings of the team that made the change. It also
pins that the owner's field comes back on the next rebuild the interval allows
rather than on the next lookup, because `Team::syncStep` frees only the demolishing
team's fields. `ring-flag` puts an exploration flag, with its goal disc kept inside
the ring, at the centre: a flag is never written into the building tile grid, so
walking the changed footprint could not discover its field at any distance.

Each scenario has to let `GRADIENT_DIRTY_REBUILD_TICKS` (`src/EngineTiming.h`)
elapse before it can judge a field, and takes the constant from that header rather
than copying it — when the interval was raised from 25 to 100, a local copy here
silently stopped covering it and the regression passed stale fields.

To see the harness fail, drop `gradientGeneration[swimClass] != topologyGeneration`
from `Map::buildingGradient`: `ring-after`, `ring-other-team` and `ring-flag` all
fail. `ring-before` passes either way by construction — nothing is cached before
the ring exists — which is why it is not on its own sufficient.

## Forbidden-zone invalidation and escape recovery

The `MapGradientInvalidation` suite (`python3 test/run_tests.py --filter
'MapGradientInvalidation/*'`) checks all seven swim classes against freshly rebuilt fields after forbidden
brushes, including resource-only edits, clearing goals, other teams and previously
stale caches. It also checks resource, terrain, building and immobility transitions,
a depleted escape exit, unreachable pockets and the refresh budget across tick wrap.

Forbidden edits preserve unaffected walking fields and pending searches; own-team
harvest round trips and clearing destinations still invalidate. Escape fields have
an independent slot every eight ticks, cycling across team/swim combinations.
Uniform-cost classes first check input markers to skip unchanged propagation.
The normal visit interval is `8 * teams * SWIM_CLASS_COUNT`; unsigned tick wrap
can extend one interval to less than twice that bound. This schedule uses the saved
game tick and preserves saved fields, with no new scheduling state.

This changes simulation decisions: replay floor 123 and network protocol 46 separate
it from previous clients. The supported save-format floor remains 58.

## Building expulsion regression

The `BuildingExpel` suite (`python3 test/run_tests.py --filter 'BuildingExpel/*'`)
links the real engine and
checks that a destroyed building puts the units inside it, entering it, or
waiting to leave it back on the map alive (footprint first, then the ring around
it; a unit with no free tile dies), and that the expelled units keep the share of
the meal or healing they had already received while a started meal still costs
the building one wheat. Every scenario then runs real simulation steps and
re-checks `Game::integrity`. It needs no display, AI tournament tooling, or
external save files.

### Savegame safety

Runs as the `SavegameSafety` suite of `glob2-engine-tests`
(`python3 test/run_tests.py --filter 'SavegameSafety/*'`). No display is required; the
runner supplies the disposable profile and working directory.

The harness checks the headless autosave-off default, explicit opt-in, autosave
cadence, the production autosave path, byte equivalence with direct
serialization to a file, successful reload, disabled autosave, truncated map data
from file and memory streams, recovery after failed loads, and oversized map-area
strings. Atomic replacement tests cover callback/open/rename failures and
temporary-file cleanup; background writes cover superseded snapshots and their
finish steps, completion on destruction and failed writes. Autosave bytes, SHA1
included, must match an inline-hashed save, including when the header backpatch
changes hashed bytes, and `Engine::haveMap` must trust a local save only when its
SHA1 matches the host's header.
On POSIX, child processes impose file-size limits to exercise short writes and
buffered flush errors while checking that the previous save survives unchanged.

## Hunger and defeat detection

Runs as the `HungryDefeat` suite of `glob2-engine-tests`
(`python3 test/run_tests.py --filter 'HungryDefeat/*'`) in a disposable profile; the
case owns a live headless GameGUI with preference saving disabled.

A hungry worker or warrior reserves the final inn place during Team::syncStep,
then walks, enters, completes its meal, and exits without being declared defeated.
Each unit type is tested with one wheat (the final food) and ten wheat; the test
continues for 300 ticks after eating and checks refreshed medical status. Checks
include the feeding timer's zero boundary, the actual death winning condition,
and controls for an empty colony, healthy worker, no food, explorer-only reservation,
a fed unit needing unavailable healing, and missing controlling players. All four
feeding cases run twice with seed 110 and
compare every team checksum; printed trace digests support platform comparisons.
The fixture initializes map occupancy and race
data before exercising the real unit activity and movement code.

## Cortex placement regression

The `CortexGeometry` suite (`python3 test/run_tests.py --filter 'CortexGeometry/*'`) compares placement geometry against the tile-scan helpers and checks the
building-proximity mask against per-building edge distances. Coverage includes
wrapped corners, upgrade reservations, construction sites, map-only occupants,
dead buildings, empty colonies, and footprints or distance limits spanning the map.

## Clearing flag resource bounds

The `ClearingFlagGradient` suite (`python3 test/run_tests.py --filter
'ClearingFlagGradient/*'`) runs in an isolated profile whose preferences must stay
unchanged. The regression covers
weighted building gradients, basic-resource switches, fruit, empty tiles,
allocation padding and every swimming class. CI executes it on Linux and Windows.

### Trapped colony elimination

The `TrappedUnitLifecycle` suite (`python3 test/run_tests.py --filter
'TrappedUnitLifecycle/*'`) runs in a disposable profile whose preferences must stay
unchanged; Linux and Windows CI run it.

Normal simulation ticks exercise completed feeding/training behind wood or
wheat, elimination without indoor starvation, active service, open exits,
free units, allied rescue, and hatchery recovery. A stocked hatchery protects
the colony even with production sliders at zero, since the player can change
them; both food and an available exit are required. Repeated seeded runs and
save/load continuations compare per-tick unit state and win/loss results with
an explicit RNG checkpoint. This is a focused regression, not whole-game replay
compatibility. Version 93 rejects older replays because elimination timing changed;
older saves remain loadable.

## Team statistics save compatibility

```sh
python3 test/run_tests.py --filter 'TeamStatsSave/*'
```

The two legacy cases inflate the gzip-compressed fixtures (`test/fixtures/team-stats/version88.game.gz`,
`games/gd-small-2ai.game.gz`) so they exercise loading a genuinely raw legacy save, and
compare the printed trace with `version88.expected.txt` / `version84.expected.txt`
(`--update-fixtures` rewrites them).

This headless test verifies live statistics and smoothing across all 32 sampling
positions and repeated binary reloads, with history-ring wrap, named text fields,
invalid-index and truncated-field controls. It compares version-84 and version-88
save traces against outputs from the original loader. Linux and Windows CI run
it in disposable profiles and check that preferences remain unchanged.
See [fixtures and reproduction steps](fixtures/team-stats/README.md).

The harness also covers [gameplay measurements](../docs/ai/gameplay-statistics.md):
real production, resource, damage, death, treatment, construction and training
paths; 64-bit totals; timestamped coverage; pending projectile/death attribution;
and malformed new fields. `SavegameSafetyHarness` compares measurement totals
through 700 engine ticks after reload, crossing a history sample.

Optional UI artifacts (a `[display]` case, so xvfb or a real display):

```sh
python3 test/run_tests.py --filter 'TeamStatsSave/measurement screenshots*'
```


## AI helper gradient regression

`Map::updateGlobalGradient(Uint8*)` supplies the Castor/Warrush helper maps.
Run its independent byte-for-byte oracle from the repository root:

```sh
python3 test/run_tests.py --binary unit --filter 'GlobalGradient/*'
```

The harness covers 3,000 random fields, mixed seed strengths, inert inputs,
toroidal seams, thin dimensions, obstacles, distance cutoff and idempotence.
It runs in the Linux CI jobs; the weighted pathfinder has separate `Gradient`
coverage in `glob2-unit-tests`.

## Resource-fetch target regression

From the repository root:

```sh
python3 test/run_tests.py --filter 'ResourceFetchTarget/*'
```

The real-engine movement-method fixture checks every swim class: a valid resource
target remains unchanged, and a depleted target is refreshed after its resource
gradient is rebuilt. It invokes the movement method directly, rather than running
an entire match. The runner isolates the profile and working directory and checks that preferences
remain unchanged. Linux CI runs this regression.

## Hiring bucket iteration

`HiringBucketHarness` uses the real engine to check that two competing inns
receive one worker each before either retries. Hiring the first worker reorders
the live bucket; iteration must keep following building identity. The old loop
fails this fixture with two workers at the first inn and zero at the second. Run with:

```sh
python3 test/run_tests.py --filter 'HiringBucket/*'
```

It runs headlessly in disposable profile directories in Linux and Windows CI.

## AI save portability

`AISavePortabilityHarness` checks the shared AI runtime's serialized fields before its first tick,
round-trips asymmetric AddArea/RemoveArea coordinates through the binary stream,
and executes the restored orders on their intended tiles. It also loads legacy
Maxima telemetry with values in the nonexistent seventh-policy columns and checks
that capture marks those columns unavailable while still sampling the six real
policies. The legacy columns and historical samples remain readable.

```sh
python3 test/run_tests.py --filter 'AISavePortability/*'
```

The Linux CI regression job runs this harness. For portability changes, also run
it with Clang and GCC and compare full-game per-tick traces using the same saved
input. Correct coordinate loading preserves the x/y order written in existing
saves; a save containing pending area orders can resume differently from older
GCC builds that transposed those coordinates. The saved layout is unchanged.

## Shared AI runtime/Nicowar save continuation

```sh
python3 test/run_tests.py --filter 'RuntimeContinuation/*'
```

The harness preserves stale fields, uncomputed gradients, ages and duplicate
refresh-queue entries through binary and text manager round trips. It also checks
that shared-runtime controllers own independent managers and that a legacy shared
manager can be copied without retaining mutable gradient or entity references.
Linux and Windows CI run it.
`check_parallel_compute.py` also resumes the version 121 four-controller fixture in
`test/fixtures/echo/` at 1, 2, 4 and 8 workers and compares checksums and saves.
That fixture was saved at tick 256 from `maps/FourSquares1.map.gz`, game seed
123, with Econo, Nicowar, Econo and Nicowar in player order.
The same check starts a fresh four-controller game to cover concurrent cache creation.
It also starts four Castor controllers to exercise concurrent lazy map-gradient
requests.

`python3 test/check_shared_runtime_save_continuation.py PATH/TO/glob2` also checks full
Maxima/Nicowar and Nicowar/Nicowar games through two reloads using the retained
[arena fixture](fixtures/shared-runtime-continuation/README.md).

Save format 119 stores these fields plus the previous construction id, fruit
observation and local initialization timer. Recomputing the cache on load can
otherwise change the tick a pending building becomes ready. Earlier saves remain
readable through their historical reconstruction path; missing historical cache
state cannot be recovered from them. A subsequent format-119 save preserves the
reconstructed state. Network protocol 42 gates transfers containing the new fields;
the replay floor for that version remained unchanged. Version 122 gives each
controller its own manager, loads and copies legacy shared-manager state, and
raises the replay floor to 123 and network protocol to 46.

## Shared AI runtime building-order id save compatibility

`RuntimeBuildingOrderSaveLoadTest` covers `AISharedRuntime::Construction::BuildingOrder::id`,
the `BuildingRegister` key handed out at runtime by `Runtime::add_building_order`.
`save()` and `load()` never moved the field and the member had no initialiser, so
every pending building order restored from a save carried an uninitialised heap
value into `BuildingRegister::issue_order` and `AssignWorkers`: an AI game resumed
from a save was not reproducible run to run, and a resumed multiplayer game could
desync without packet loss or a version mismatch. Version 96 serialises the field.
Older saves do not carry it and load leaves the member at `-1`, the sentinel
`Runtime::load` replaces with a fresh `register_building()` key.

The fixture checks the version-96 round trip, that an unregistered order's `-1`
survives the `Uint32` on the wire rather than returning as a huge positive key,
and that a pre-96 stream leaves the sentinel with every following field still
decoding from the right offset. `test/unit/stubs/RuntimeStubs.cpp` satisfies the
`find_location` / `passes_conditions` link surface (`BuildingsTypes`, `FlagMap`,
`GradientManager`, and the `Constraint` / `Condition` factories) that a
constraint-free order never reaches at runtime. It is the `RuntimeBuildingOrderSaveLoad`
suite of `glob2-unit-tests`.

### Native main Settings redesign

Run `python3 test/run_tests.py --filter 'Settings/*'`. The `Settings` suite
(`test/SettingsScreenTest.cpp`) runs one case per configuration of the old matrix:
640×480, 800×600, 1000×700 and 1280×900 in OpenGL, 1000×700 in software rendering,
and 640×480 with expanded English strings written into the case's disposable
profile. Each case uses its own profile and writes its native captures to its
artifact directory. It covers all six categories, building stages, automatic
saving and retry, software display confirmation/rollback, pending OpenGL changes,
language refresh, and keyboard sequence/conflict handling. The shared dropdown
checks cover anchoring, mouse and keyboard selection, dismissal, wrapping, and
scrolling without committing a value. Persistence failures use a directory at the
destination path so both settings and shortcut retries exercise the atomic writer.
Window and drawable dimensions are logged so 1× runs are not mistaken for physical
HiDPI validation.

The redesigned screen exposes semantic row IDs (for example `gameplay.speed`)
for tests; do not locate settings controls by pixel coordinates. Slider updates
preview immediately and commit on release/idle, whereas discrete changes save
immediately. Bindings commit only after a complete edit. Legacy preference and
keyboard file formats remain unchanged.

The `GameSpeed` suite (`test/GameSpeedTest.cpp`) has a headless case for the speed
presets, bounds, legacy settings and persistence, a display case for the main and
in-game settings, language refresh, keyboard shortcuts, multiplayer eligibility and
camera cadence, and a display case that runs the live engine at normal and maximum
speed, through pause and hard pause, and plays the recorded replay back at 1x,
maximum and fast-forward. The last case captures the engine's per-run checksums
and requires the first four (speed and pause) and the last three (playback) to
agree. `python3 test/run_tests.py --filter 'GameSpeed/settings*'` runs the settings
case alone.

## Pre-game map preview regression

Run `python3 test/run_tests.py --filter 'MapPreview/*'`: three headless cases (geometry,
codec, network) and one `[display][artifacts]` case that writes the native software
captures into its artifact directory. Fixtures exercise rectangular
placement, toroidal dragging, legacy/new codecs, malformed input, network frame
bounds, thumbnail request deduplication, timeout/retry and bounded cache reuse.
See [pre-game preview behavior and compatibility](../docs/features/pre-game-map-preview.md).

## Tournament execution and configuration

`python3 test/test_tournaments.py` exercises leases, duplicates, resumable transfers,
worker queues, immutable builds and offline statistical policies using stdlib fixtures.
`python3 test/test_map_fairness_tournament.py` retains the fairness estimator and repeat-selection regressions.
`python3 test/test_map_generation_study.py` checks structured map-study result classification,
timeouts, temporary-profile cleanup, catalog lookup and per-subject telemetry preservation.
`python3 test/test_fairness_model.py` checks the fitted [fairness model](../docs/map-generators/FAIRNESS_MODEL.md):
that the fit recovers coefficients from a tournament simulated out of the model itself, that a
measurement deciding nothing is fitted near zero, that the fairness definition reads the same at
every colony count and ignores the offset softmax leaves unidentified, and that every measurement
the model may select has a C++ expression waiting for it. The fitting checks need numpy and scipy
and skip without them; the rest is stdlib.
The `TournamentCompatibility` engine suite (`python3 test/run_tests.py --filter
'TournamentCompatibility/*'`) covers real per-player Cortex/Maxima and partial
network-header checks. `python3 test/tournament_cli_integration.py --output DIR`
runs production CLI cases and retains saves, traces and logs. Use a fresh output
directory. `--initial FILE --ticks N` runs a retained initial state on another platform.

`test/tournament_reliability_pilot.py` is an opt-in localhost/SSH integration pilot.
It requires immutable macOS/Linux bundles and explicitly configured disposable
worker directories, and kills only processes belonging to that pilot. See
[the tournament guide](../docs/tools/tournaments.md) for commands and validation policy.
## Map CLI

Build the normal client with `scons release=1 server=0`, then run
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
See [map CLI documentation](../docs/map-generators/CLI.md).

### Map JSON reports

Build `scons release=1 server=0 map-report-test`, then run
`python3 test/test_map_report.py build/native-tests/src/glob2 build/native-tests/test/MapReportHarness`
(add `.exe` to both binaries on Windows). The suite runs without graphics, checks
the [published report contract](../docs/map-generators/REPORT.md), recomputes fairness
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
See [AI telemetry](../docs/ai/telemetry.md) for the capture/extension contract.

## Performance telemetry

```sh
scons -j8 release=1 server=0 unit-tests
python3 test/run_tests.py --binary unit --filter 'PerformanceTelemetry/*'
```

The injected-clock harness checks online variance, nested timings, exclusion of sleep and
presentation from work, budgets, jitter, sampling rotation, actor generations, capture
boundaries, and the disabled control. SavegameSafetyHarness additionally checks background
write timing counts and completed/failed/superseded accounting. See
[metric definitions and export records](../docs/development/performance-telemetry.md).

`MapGeneratorGoldenTest PROFILE --performance` compares all built-in generators with timing
off/on (serialized worlds, outcomes, RNG, and generation telemetry) and emits generation
timing records, including site assignment. Use a disposable HOME and run from the repository.

### Distributed gameplay, AI and performance telemetry

`python3 test/test_distributed_game_telemetry.py` tests typed streaming extraction,
64-bit values, escaped text, dynamic fields, unavailable data, malformed records,
compressed artifacts, checksum enforcement and JSONL/CSV roundtrips.
`python3 test/distributed_game_telemetry_integration.py --hosts HOSTS.json --output NEW_DIR`
runs all eight AIs on supplied registered bundles through real workers and the
coordinator. It checks log transfer, complete final telemetry, offline record
counts, export-on/off per-tick checksums, and repeated save/load telemetry
continuation. Host entries need absolute `bundle` paths; workers are stopped after
collection. See [tournament telemetry](../docs/tools/tournaments.md#gameplay-ai-and-performance-telemetry).


## Nicowar farming wood clearance

`NicowarFarmingHarness` executes the farming scan and its real area orders. It
checks the wood/wheat zone boundaries, eight-way wheat adjacency (including inside
the wood zone), both wrap seams and diagonal corner wrapping,
removal of conflicting farming protection, cleanup after wood and neighboring
wheat disappear, and preservation of building clearing strips.

```sh
python3 test/run_tests.py --filter 'NicowarFarming/*'
```



## Engine save continuation

The `UnitContinuation` suite (`python3 test/run_tests.py --filter 'UnitContinuation/*'`):
five checkpoints compare 256 subsequent
simulation ticks and the RNG state, including idle timers, clearing reservations,
service-list ordering, building worker membership and a nonzero construction
cooldown. Format 114 preserves that cooldown; older formats remain readable.
Linux and Windows CI run
this harness. New saved games preserve live state without running building updates
during load; legacy formats keep their historical reconstruction path.

For full games, compare an uninterrupted sidecar with one or more resumed traces:

```sh
python3 test/compare_save_continuation.py uninterrupted/game.replay.checksums \
  resumed/game.replay.checksums
```

The comparator checks every consecutive team/entity record, reports the first
mismatch, rejects missing/truncated records, and excludes the aggregate checksum
because it includes the save header/version. Run the retained late-game regression
with `python3 test/maxima/check_save_continuation_fixture.py build/native-tests/src/glob2`.

The retained Maxima format-115 checkpoint compares all 512 ticks from 30000
through 30511 against uninterrupted execution. Its compressed save, expected
per-tick hashes, and reproduction commands are in
[maxima/fixtures/save-continuation](maxima/fixtures/save-continuation/README.md).

### AI strategy profile captures

The `CustomGameSetup` suite (`test/CustomGameSetupHarness.cpp`) has one case per
mode of the old command line:

```sh
python3 test/run_tests.py --filter 'CustomGameSetup/*'
python3 test/run_tests.py --filter 'CustomGameSetup/generated map snapshot*'
python3 test/run_tests.py --filter 'CustomGameSetup/AI strategy profiles*'
```

The snapshot case checks that a freshly serialized map launches through the lobby's in-memory load path. The strategy profile cases capture the Players & Teams strategy button and every AI profile at its top and bottom, at 640×480 and 1000×700. They check that profile/summary keys resolve, including Maxima. The player control widgets case exercises opening the strategy screen from the lobby and choosing an AI before launching each controller mode. Every case uses its own disposable `glob2-custom-setup-tests` profile and leaves captures in its artifact directory.

The strategy profile cases run in English; to capture another catalog with its
localized section headings, pass that language code to `setupOptions` in a local
copy of the case. Text wrapping for unspaced CJK text and long words is covered by
the `UILayout` unit suite.

### Landscape preview scheduling and scrolling

The same suite provides focused checks:

```sh
python3 test/run_tests.py --filter 'CustomGameSetup/preview queue*'
python3 test/run_tests.py --filter 'CustomGameSetup/landscape preview*'
```

The preview queue case checks deferred startup, viewport-center
priority for grid and single-column layouts, scrolling/filtering, unchanged seeds,
cooperative viewport-only polling, retries, and restart/reroll behavior. The
normal headless harness also runs these checks. `landscape-responsive` verifies
that completed off-screen images do not allocate rendering surfaces and that
scrolling never starts fades; it captures top/middle/bottom screenshots. CI runs
both the headless harness and this graphical regression.

`landscape-performance` runs the same fixed-image fixture without enforcing the new
behavior, for comparison with a baseline build. It reports delivery and scrolling
times for 67 completed 256×256 thumbnails, plus terrain hashes and colony positions
for three generators with root seed 71. Timings are evidence, not pass/fail limits.

## Phone interface verification

The native portable-renderer checks exercise real SDL touch dispatch in portrait
and landscape, including placement/confirmation, camera gestures, building
controls, replay actions, setup/settings navigation and modal viewport changes.
Build and run commands are in [Mobile development](../docs/mobile/development.md#verification).
The `GameGUITouch` and `UIPresentation` cases need a windowing display (Xvfb on
Linux), run in isolated profiles and copy their screenshots into their artifact
directories. `UIPresentation` has one case per viewport, so CI shards distribute
the full screen/presentation/inset sweep and each viewport gets its own timeout
and failure report. Its offline lobby fixture renders the production screen
without starting the public IRC connection, so layout checks do not wait for
external network timeouts during teardown. They complement
Android/iOS device playtesting; they do not establish device lifecycle,
performance, keyboard or cross-platform simulation compatibility.


## Zone boundary rendering

Run `python3 test/run_tests.py --filter 'PortableRenderer/*'` on a windowing
display. The boundary regression checks every pixel along joined
outlines at 25%, 33%, 50%, 75%, 100% and 150% zoom, with fractional camera offsets
and toroidal copies. It covers software, SDL portable and (when compiled) OpenGL
renderers and verifies that later map artwork retains its transform.
Set `GLOB2_ZONE_EVIDENCE_DIR` to an existing ignored artifact directory to capture
the 33% outline fixtures.

Zone boundaries use `GraphicContext::drawMapBoundary`: positions snap to the
active target's pixel grid and strokes remain at least one target pixel wide.
Ordinary UI lines retain their existing sizing behavior.

## Parallel compute prototype

Build `scons release=1 server=0 unit-tests path-gradient-test
building-gradient-invalidation-test`. The `ComputeExecutor` unit suite checks exclusive
slots, barriers, nested batches, exception propagation, reuse and reconfiguration.
The path oracle also exercises independent eager/lazy searches at 1/2/4/8 threads;
the building invalidation harness compares real area/building seed fields and
frozen hiring advancement. Linux/Windows CI run the executor and path oracle.

`python3 test/check_parallel_compute.py BUILD/GLOB2 --baseline BASELINE` compares
per-tick traces, replay bytes, final saves and uninterrupted save continuation.
Omit `--baseline` to compare the candidate's default execution; `--output DIR`
retains all evidence. This subprocess runner uses Unix `wait4`; native Windows
uses the C++ harnesses. See the existing performance guide for corpus preparation
and paired CPU/wall-time benchmarking.


### Delayed gradient pipeline

The `GradientPipeline` unit suite (`python3 test/run_tests.py --binary unit --filter
'GradientPipeline/*'`) varies completion order across
0/1/2/4/8 workers and publication delays, checks synchronous supersession, bounded
buffers, partial thread-creation failure, exception delivery and teardown. It can
also be compiled directly with ThreadSanitizer without SDL.

`BuildingGradientInvalidationHarness` exercises real resource, guard and clear
fields, including a synchronous update while an older snapshot is pending.
`benchmark_gradient_pipeline.py --verify` compares real-game per-tick traces across
worker counts under the same delayed schedule. `check_gradient_pipeline.py` also
checks the one-worker/eight-tick defaults and save/resume at each of the eight
deadline phases with zero, one and two workers. Pending fields, supersession and
remaining deadlines are versioned save state; worker count is not.

## Experimental features and guard-area balancing

`ExperimentalFeatures` (`glob2-unit-tests`) covers the experiments registry and
the set a game carries: stable keys, the preferences text form, binary and text
stream round trips, unknown keys dropped, and the `GameHeader` forms with a
version 123 header reading no experiment. `SettingsExperiments` and the
`experiments` case of `CustomGameSetup` (`glob2-engine-tests`) cover the
preferences round trip, the string tables and the baked-in rule: every
registry entry's label and help are listed keys matching the English table, a new
game takes Settings → Experiments, its save keeps that set after the setting is
turned off, a fresh game then carries nothing, and a campaign mission never takes
the set. `test/tournament_cli_integration.py` covers `--run-game --experiment`. The `Settings` display cases toggle the switch on the
settings page. See [experimental features](../docs/features/experimental-features.md).

`GuardAreaBalance` (`glob2-engine-tests`, `python3 test/run_tests.py --filter
'GuardAreaBalance/*'`) runs the real engine on a blank 64x64 map with 24 warriors
for the `guard-area-balancing` experiment: spawn, drain, patches, size, three
areas, erase, settled-guard movement, a save/load continuation and the crowding
box sum against brute force, plus a `[benchmark]` timing case. Its first case
runs spawn and drain without the experiment, expects the old outcome, and compares
the default game's per-100-tick checksums with
`test/fixtures/guard-area/off-path-checksums.txt` (`[golden]`). Games start with the experiment through
`glob2test::GameOptions::experiments`; `GameOptions::header` installs the
one-local-player header and seed they need. Design and numbers:
[guard-area balancing](../docs/features/guard-area-balancing.md).

## JavaScript

See the [scripting guide](../docs/development/javascript.md) and
[API reference](../docs/development/javascript-api.md) for the public boundary.

Build `unit-tests engine-tests` with SCons and run
`python3 test/run_tests.py --build-dir build/darwin/client/release --filter 'JavaScript*/*'`
(use the build directory for your platform).
The runtime harness checks capability restrictions, deterministic work exhaustion,
automatic global snapshots, aliases/cycles, reload/rejection rollback, serial
worker migration and exact Math output bits.
Raw global-number fixtures also compare the persisted snapshot boundary:
NaNs have one canonical representation, while signed zero and infinity signs
survive save/load. The integration harness checks AI
visibility/ownership and transactional scenario effects and continuation. Run
`python3 test/check_javascript.py /absolute/path/to/glob2 --output artifacts/js-check`
with a fresh output directory for the frozen per-tick profile trace, worker
equivalence and full-game saved continuation.


`python3 test/run_tests.py --filter 'ScriptEditor/*'` checks the map editor's
SGSL/USL/JavaScript language selection, draft compilation and cancellation,
`.js` load/save, embedded map source/mode round trips, and dropdown interaction
with captures on desktop and both phone orientations. It also exercises a real
USL runtime resource failure during JavaScript-to-SGSL confirmation, verifies
that both live programs survive the failure, and executes the committed SGSL
program after its preparation objects are destroyed. SGSL exchange coverage
checks story owner pointers in both resulting runtimes.

The named `JavaScriptNumbers`, `JavaScriptTransactions`, `JavaScriptLifecycle`,
`JavaScriptRealistic`, `JavaScriptPresentation`, `JavaScriptSession` and
`JavaScriptSimulation` suites run alongside runtime/integration cases. The shared
corpus includes real map-reading economic planners and a scenario survey with
transcendental math, private RNG, returned data and executed orders. Browser and
iOS harnesses link the same production objects and select these suites; Android
uses the same native test registry.

`python3 test/check_javascript_corpus.py --build-dir BUILD --output artifacts/js-corpus`
retains numeric bits, serialized results, simulation traces, saves, replays, logs,
JUnit results, source/fixture hashes and compiler metadata. Harnesses emit their
build-time source revision and content hash; runners reject binaries built from
a different source tree. The shared comparator also requires identical normalized
Git source hashes across platforms. Git blob normalization accounts for checkout
line endings and symlink representations; modified and untracked inputs still
make development builds ineligible for the clean revision gate. Manifests retain
the actual Git status entries, and CI records checkout status before and after
compilation, so unexpected dirty inputs can be diagnosed without relaxing that
gate. A clean runner checkout alone does not establish binary provenance.
The native corpus runner requires a clean
committed revision; `--allow-dirty` is for development evidence only. Use fresh
output directories and compare identical final revisions across platforms.

Android: build `android-tests` for API 24 and the selected ABI, then run
`python3 mobile/android_device_tests.py --android-sdk SDK --serial SERIAL --arch ABI --suite 'JavaScript*' --output artifacts/js-android`.
The runner uses disposable shell directories, retrieves artifacts even after
failure and never accesses installed game data. Use a fresh Android output
directory; failed artifact transfers fail the run and retain its remote evidence
for recovery. iOS: build the separate app with
`python3 mobile/ios.py build --environment simulator --release --script-tests`;
for a device use `--environment device --team TEAM`. Its bundle identifier is
`org.globulation2.glob2.script-tests`, and evidence is exported in its own
Documents/ScriptingEvidence directory. Simulator evidence does not satisfy the
physical-device gate. Browser: build `web-tests` and run the shared corpus case in
`browser/tests/determinism.spec.js`; evidence is under
`artifacts/browser-determinism/script-corpus/`.

Cross-platform acceptance requires identical numeric/data results and complete
per-tick traces, plus decoded save payloads. Exclude only documented MapHeader
SHA1 metadata when save histories differ. Same-platform equal-history save and
replay bytes must match. Build success and simulator-only runs are insufficient.
CI retains evidence even when execution fails; unavailable devices/signing leave
those platform gates incomplete. See the [fixture notes](fixtures/javascript/README.md)
for the exact frozen worlds, seeds and intended draft profile corrections.
CI executes the shared scripting corpus in Chromium, Firefox and WebKit and
compares their numeric/data results, complete traces and decoded saves against
the Linux and Windows corpus runs. The separate released replay comparison
selects only its baseline traces, so scripting fixture traces cannot be mistaken
for the released replay.

`python3 test/check_javascript_evidence.py REFERENCE CANDIDATE --output artifacts/js-comparison.json`
compares shared numeric/data values, complete traces and decoded save payloads,
requiring the same artifact inventory. It excludes only MapHeader SHA1 from save
payloads. Review each runner manifest to establish matching source revisions and
successful execution before treating matching hashes as acceptance evidence.
Use `mobile/ios_script_tests.py` with an explicit device identifier and, for a
simulator, an owned `--simulator-set` to install, run and retrieve the separate app.

To retain released simulation compatibility traces, replays, commands, and logs,
pass `--output artifacts/released-compatibility` to
`test/check_telemetry_simulation.py`. Fresh-load traces compare complete bytes;
the legacy checkpoint comparison excludes the version-dependent aggregate and
compares every stored team/entity record. The evidence manifest records this
exception. CI retains these artifacts even when verification fails.

The shared evidence comparator requires successful runs of the same clean source
revision. `--allow-development` permits diagnostic comparisons while recording
provenance failures; those comparisons do not satisfy the final acceptance gate.
