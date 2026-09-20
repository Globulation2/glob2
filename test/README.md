# glob2/test/

CppUnit-based test fixtures and standalone harnesses for the C++ codebase. Most use this directory's `SConstruct`: run `scons -j16` here, then the in-tree `./TestsRunner` and `./WinningConditionsHarness` binaries. Rebuild these tests here before trusting a result; the top-level build does not build them. The exceptions are `GameGUISelectionHarness` and `TerrainResourcesHarness`, which use the top-level `selection-test` and `terrain-test` targets described below.




## Maxima

See [Maxima tests](maxima/README.md) for policy, configuration, integration and
saved-game continuation coverage.

## Selection lifetime regression

`GameGUISelectionHarness.cpp` links the real client objects with a test entry
point. It exercises selected building/unit deletion before the next GUI draw,
null selections, and a live unit with no peer. It runs headlessly, using the real
`GameGUI`, entity classes, selection setters, and destruction hooks. A friend
fixture accesses the private selection API without exposing it to game callers.

Build and run it from the repository root (the Linux CI also runs this target):

```sh
scons -j8 release=1 server=0 selection-test
./build/src/GameGUISelectionHarness
```

For AddressSanitizer and UndefinedBehaviorSanitizer on macOS or Linux:

```sh
scons -j8 release=0 server=0 --build=build/selection-asan selection-test \
  CXXFLAGS='-g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  LINKFLAGS='-g -fsanitize=address,undefined'
./build/selection-asan/src/GameGUISelectionHarness
```

If Homebrew sdl2-compat cannot locate SDL3 under the macOS sanitizer, prefix
the harness command with `DYLD_LIBRARY_PATH=/opt/homebrew/lib`.

SCons caches compiler/linker flags; pass `CXXFLAGS=-g LINKFLAGS=-g` to return to a
normal build. This is a direct method regression, not an interactive replay test.

## Terrain resource regression

From the repository root, run `scons -j8 release=1 server=0 terrain-test`
and `./build/src/TerrainResourcesHarness`. The harness links the actual client
objects and exercises terrain regeneration and resource clearing for all eight
resource types, all three base terrains, overlapping strokes, and all four
wrapped map corners. A whole-map oracle checks both removal and preservation.
These are headless map-operation tests; they do not drive editor mouse events.

## Map generator golden maps and colony sweep

From the repository root, `scons -j8 release=1 server=0 map-generator-golden-test` builds
`build/src/MapGeneratorGoldenTest`. Run it as `./build/src/MapGeneratorGoldenTest <profile>`
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

Build `scons release=1 server=0 orchard-conversion-test`, then run from the repository root:

```sh
build/src/OrchardCommonsConversionTest orchard-contracts "$PWD" "$PWD/artifacts/orchard-conversion"
```

The harness places competing inns on unmodified generated terrain and checks superior
advertised fruit variety, equal friendly diet, advertising off, lost fruit and lost
wheat using real game ticks. It also checks full-save generation repeatability,
telemetry neutrality, terrain/resource save-load preservation and invalid requests.
Saved fixtures retain each scenario. Loading rebuilds the existing conversion cooldown;
the test explicitly matures eligibility after loading, not immediate continuation.
CI builds and runs this harness on Linux and Windows and retains the fixtures.

For generation-only timing at 512×512/eight teams, append `--benchmark 60` to the
command. This skips the scenarios and reports separate telemetry-off/on means and
failure counts; it is a local profiling tool, not a timing threshold in CI.

## Map generator profiling fixture

`scons -j8 release=1 server=0 map-generator-profile-fixture` builds
`build/src/MapGeneratorProfileFixture <profile-dir> <seed> <rounds>`, which round-robins every
registered generator for `rounds` passes with parameters (shared and generator-specific) drawn
at random the same way `GenerationRequest::randomizeControls` does, and prints a per-generator
attempt/success/timing table. It exists to give an external sampling profiler (macOS `sample`,
Linux `perf record`) a sustained, representative mix of real generation work to attach to; run it
with a large round count in the background and sample its PID. It is not a regression test (see
`--sweep` and `--performance` above for that) and is not wired into CI, but a fixed seed and round
count also make it a quick way to compare a shared primitive's before/after cost: attempt/success
counts per generator repeat exactly across a behavior-preserving change, so a diff there is a
signal something changed, not just an optimization's timing.

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

Provide a `*TestStubs.cpp` (e.g. `MapQueryTestStubs.cpp`) with empty bodies for `Sector::Sector(Game*)`, `Sector::~Sector`, `Sector::setGame`, `Sector::step`, `Sector::save`, `Sector::load`, `Sector::free`, and `UnitDeathAnimation::UnitDeathAnimation`. `Map.o`'s compiled `setSize` / `setGame` reference `Sector` symbols even though the test never calls them. ~30 lines of stubs avoid pulling all of `Sector.cpp`'s real deps.

### `SConstruct` surgery

The predicate test build needed these include paths beyond what existing tests had: `../src/building`, `../src/game/entities`, `../src/team`, `../src/unit`, `../src/gui`, `../src/net`, `../src/net/irc`, `../src/net/message`, `../src/yog`, `../libusl/src`. Linked sources for the predicate test: `Map.cpp`, `MapQuery.cpp`, `MapTerrain.cpp`, `BitArray.cpp`, `Utilities.cpp`, `building/BuildingUtils.cpp`, `unit/UnitUtils.cpp`, plus the stubs.

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
python3 test/run_lan_session_test.py build/src/LANSessionHarness
```

This runs separate host and joining client processes with real SDL lobby widgets,
YOG anonymous LAN server, game router, and TCP connections. The joiner uses the
actual `LANFindScreen` Connect path. It clicks Ready and Leave Game, then rejoins.
Both cycles force a map download and compare all 616018 bytes against the fixture
source (`maps/FourSquares1.map`). The host verifies readiness, roster size, unique
player IDs, slot masks, and both departures. The map's current size is not hardcoded
in the test. Linux CI runs this automatically with SDL's dummy video/audio drivers.

For two physical machines, run these from each machine's repository root, using
absolute capture prefixes whose parent directories already exist:

```sh
SDL_VIDEODRIVER=dummy ./build/src/LANSessionHarness host 127.0.0.1 2 /tmp/lan-host
SDL_VIDEODRIVER=dummy ./build/src/LANSessionHarness join HOST_IP 2 /tmp/lan-guest
```

Start the joiner after the host prints `HOST roster=1`. TCP ports 7489 and 7491
must be reachable; this does not connect to the public YOG service. Omit
`SDL_VIDEODRIVER=dummy` to show the real window. Normal game profiles are preserved;
the harness uses `.glob2-lan-test-host` and `.glob2-lan-test-join` profiles containing
only test data. Fixed input timers allow map transfer before leaving; the runner
bounds startup, execution, and child cleanup. Logs and captures are written under
`output/lan-session-test` by default (`--output` overrides it).

## Aspect-ratio and screen-capture regression

`FullscreenAspectHarness` links libgag and opens a real SDL window. It checks
presentation pixels, clipping, logical-resolution screen captures, and translated
mouse motion/button events and polling at equal, wide, tall, odd, and downscaled
window sizes. It exercises the same scaling path used by desktop fullscreen.
The software run also checks every pixel in 24 opaque/translucent rectangle
intersections, including rectangles above the clip area and empty rectangles.
It does not load a game profile or change saved display settings.

```sh
scons -j2 release=1 server=0 aspect-test
./build/libgag/src/FullscreenAspectHarness gl
./build/libgag/src/FullscreenAspectHarness software
```

Ubuntu CI runs both under `xvfb-run -a -s '-screen 0 1600x1400x24'`, using Mesa
software OpenGL (`LIBGL_ALWAYS_SOFTWARE=1`). Xvfb and xauth must be installed.
The GL run requires an OpenGL-enabled build. On macOS, the Homebrew SDL workaround
described above also applies. To run with sanitizers, use the flags in the selection
regression instructions with the `aspect-test` target instead.

## Wrapped building footprint regression

From the repository root, run `scons -j8 release=1 server=0 building-footprint-test`
and `./build/src/BuildingFootprintHarness`. The harness links the real engine and
round-trips generated fixtures through binary saved games. It checks exact map
occupancy, missing/stale-cell repair, repeated integrity checks, and ground exits
for interior, negative-origin and positive wrapped footprints. It protects the
runtime fix in `fafb5e9a`: the old predicate erased valid wrapped cells on load and
could subsequently abort in `Building::findGroundExit`.

It needs no display, AI tournament tooling, or external save files. Linux CI runs
it on both supported Ubuntu versions.

Saved state and step-by-step before/after reproduction: [PR #165 fixture](fixtures/wrapped-building/README.md).

## Entering unit save regression

From the repository root, run `scons -j8 release=1 server=0 entering-unit-save-test`
and `./build/src/EnteringUnitSaveHarness`. The harness links the real engine and
round-trips generated fixtures through binary saved games. It exercises eight
entry directions at five interior/edge/corner positions, preserves the building
reference and animation destination, and rejects both a misplaced entering
explorer and stale occupancy for an ordinary explorer. It protects the runtime
fix in `4ce1d5bc`; expected negative controls print integrity diagnostics.

It needs no display, AI tournament tooling, or external save files. Linux CI runs
it on both supported Ubuntu versions.

Saved state and step-by-step before/after reproduction: [PR #166 fixture](fixtures/entering-explorer/README.md).

## Entering unit draw regression

From the repository root, run `scons -j8 release=1 server=0 entering-unit-draw-test`
and `xvfb-run -a -s '-screen 0 1024x768x24' ./build/src/EnteringUnitDrawHarness`.

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

Frames land in `.cache/entering-unit-draw-check/`: `entering-delta<N>.png` and,
on failure, `walking-delta<N>.png` for the unit-only comparison, plus
`scene-delta<N>.png` with terrain and the inn for looking at by eye. The
comparison itself stays on the unit-only render, which has no animated water or
clouds to make two frames differ by themselves.

It needs a display and a GL context. CI already installs `xvfb` and mesa for the
fullscreen aspect harness, so a job step is a two-liner:

```yaml
      - name: Build and run the entering unit draw regression
        run: |
          scons -j$(nproc) release=1 server=0 entering-unit-draw-test
          timeout 300s xvfb-run -a -s '-screen 0 1024x768x24' ./build/src/EnteringUnitDrawHarness
```

## Immobile unit gradient regression

From the repository root, run `scons -j8 release=1 server=0 immobile-unit-gradient-test`
and `./build/src/ImmobileUnitGradientHarness`. The harness uses a fresh 64x64 map
and real engine orders to check empty immobile-unit bookkeeping, exact blocked
cells, and immediate building-route invalidation after painting and erasing a gap.
It exercises all seven swim classes on weighted full-map gradients. No display or
external save fixture is needed; normal game data must be available.

Pass `fresh`, `occupancy`, or `forbidden` to run one scenario. The latter two clear
the initial occupancy explicitly, so failures in painting or occupancy can be
reproduced independently of the fresh-map initialization bug. Linux CI runs all
scenarios.

## Building gradient invalidation regression

From the repository root, run `scons -j8 release=1 server=0 building-gradient-invalidation-test`
and `./build/src/BuildingGradientInvalidationHarness`. The harness links the real
engine and places every building through `OrderCreate` / `OrderDelete`, so it
exercises `Game::addBuilding` and `Team::syncStep` rather than a test double. On a
fresh 64x64 grass map with two teams, it checks that a cached route field notices
the ground moving under it: a ring of inn sites closed around a site whose field is
already cached stops offering that site to a unit outside, a site placed inside a
standing ring is never offered, and clearing the ring restores it.

Pass `ring-before`, `ring-after`, `ring-other-team` or `ring-flag` to run one
scenario; the default is all four. Linux CI runs all of them.

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

## Building expulsion regression

From the repository root, run `scons -j8 release=1 server=0 building-expel-test`
and `./build/src/BuildingExpelHarness`. The harness links the real engine and
checks that a destroyed building puts the units inside it, entering it, or
waiting to leave it back on the map alive (footprint first, then the ring around
it; a unit with no free tile dies), and that the expelled units keep the share of
the meal or healing they had already received while a started meal still costs
the building one wheat. Every scenario then runs real simulation steps and
re-checks `Game::integrity`. It needs no display, AI tournament tooling, or
external save files.

### Savegame safety

Build `scons release=1 server=0 savegame-safety-test`, then run
`python3 test/run-savegame-safety-tests.py build/src/SavegameSafetyHarness`
(use `.exe` on Windows). No display is required. The runner uses a disposable
profile and working directory; an optional final argument supplies a truncated
save that must be rejected.

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

## Cortex placement regression

Build with `scons release=1 cortex-geometry-test` and run
`./build/src/CortexGeometryHarness`. It compares 57,600 candidates against
the tile-scan helpers, including wrapped corners, upgrade reservations,
construction sites, map-only occupants, dead buildings, and empty colonies.

## Clearing flag resource bounds

Build `scons release=1 server=0 clearing-gradient-test` and run
`python3 test/run-savegame-safety-tests.py --check-preferences build/src/ClearingFlagGradientTest`
(add `.exe` on Windows). The shared runner isolates the working directory and
profile and verifies that preferences remain unchanged. The regression covers
weighted building gradients, basic-resource switches, fruit, empty tiles,
allocation padding and every swimming class. CI executes it on Linux and Windows.

### Trapped colony elimination

Build `scons release=1 server=0 trapped-unit-test`, then run
`python3 test/run-savegame-safety-tests.py --check-preferences build/src/TrappedUnitLifecycleTest`
(use `.exe` on Windows). The shared runner uses a disposable profile and checks
that the normal preferences remain unchanged; Linux and Windows CI run it.

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
scons -j8 release=1 server=0 team-stats-save-test
python3 test/run-savegame-safety-tests.py --check-preferences build/src/TeamStatsSaveHarness .
python3 test/run-savegame-safety-tests.py --check-preferences --expect-stdout test/fixtures/team-stats/version88.expected.txt build/src/TeamStatsSaveHarness . --legacy test/fixtures/team-stats/version88.game
python3 test/run-savegame-safety-tests.py --check-preferences --expect-stdout test/fixtures/team-stats/version84.expected.txt build/src/TeamStatsSaveHarness . --legacy games/gd-small-2ai.game
```

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

Optional UI artifacts (requires a graphical SDL driver):

```sh
python3 test/run-savegame-safety-tests.py build/src/TeamStatsSaveHarness . --screenshots output/gameplay-statistics-ui
```


## AI helper gradient regression

`Map::updateGlobalGradient(Uint8*)` supplies the Castor/Warrush helper maps.
Run its independent byte-for-byte oracle from the repository root:

```sh
scons -j8 release=1 server=0 global-gradient-test
./build/src/GlobalGradientHarness
```

The harness covers 3,000 random fields, mixed seed strengths, inert inputs,
toroidal seams, thin dimensions, obstacles, distance cutoff and idempotence.
It runs in the Linux CI jobs; the weighted pathfinder has separate `GradientTest`
coverage in `TestsRunner`.

## Resource-fetch target regression

From the repository root:

```sh
scons -j8 release=1 server=0 resource-fetch-target-test
python3 test/run-savegame-safety-tests.py --check-preferences build/src/ResourceFetchTargetHarness .
```

The real-engine movement-method fixture checks every swim class: a valid resource
target remains unchanged, and a depleted target is refreshed after its resource
gradient is rebuilt. It invokes the movement method directly, rather than running
an entire match. The shared runner isolates the profile and working directory and
checks that preferences remain unchanged. Linux CI runs this regression.

## Hiring bucket iteration

`HiringBucketHarness` uses the real engine to check that two competing inns
receive one worker each before either retries. Hiring the first worker reorders
the live bucket; iteration must keep following building identity. The old loop
fails this fixture with two workers at the first inn and zero at the second. Run with:

```sh
scons -j6 release=1 server=0 hiring-bucket-test
python3 test/run-savegame-safety-tests.py --check-preferences build/src/HiringBucketHarness .
```

The harness runs headlessly in disposable profile directories in Linux and Windows CI.

## Echo building-order id save compatibility

`EchoBuildingOrderSaveLoadTest` covers `AIEcho::Construction::BuildingOrder::id`,
the `BuildingRegister` key handed out at runtime by `Echo::add_building_order`.
`save()` and `load()` never moved the field and the member had no initialiser, so
every pending building order restored from a save carried an uninitialised heap
value into `BuildingRegister::issue_order` and `AssignWorkers`: an AI game resumed
from a save was not reproducible run to run, and a resumed multiplayer game could
desync without packet loss or a version mismatch. Version 96 serialises the field.
Older saves do not carry it and load leaves the member at `-1`, the sentinel
`Echo::load` replaces with a fresh `register_building()` key.

The fixture checks the version-96 round trip, that an unregistered order's `-1`
survives the `Uint32` on the wire rather than returning as a huge positive key,
and that a pre-96 stream leaves the sentinel with every following field still
decoding from the right offset. `BuildingOrder.cpp` is linked against
`EchoBuildingOrderTestStubs.cpp`, which satisfies the `find_location` /
`passes_conditions` link surface (`globalContainer`, `BuildingsTypes`, `Map`,
`FlagMap`, `GradientManager`, and the `Constraint` / `Condition` factories) that
a constraint-free order never reaches at runtime. It needs no profile or display:

```sh
cd test
scons -j8 EchoBuildingOrderSaveLoadTest
./EchoBuildingOrderSaveLoadTest
```

Linux CI runs it through this directory's "Build and run the tests" step, which
executes `./TestsRunner` and then every `./*Harness` and `./*Test` binary.

### Native main Settings redesign

Build `scons -j6 release=1 settings-tests speed-tests` and run
`python3 test/run-settings-tests.py`. The harness uses disposable profiles and
writes native captures to `artifacts/settings-redesign/`. It covers all six
categories, building stages, automatic saving and retry, software display
confirmation/rollback, pending OpenGL changes, language refresh, and keyboard
sequence/conflict handling. The shared dropdown checks cover anchoring, mouse and
keyboard selection, dismissal, wrapping, and scrolling without committing a value.
It runs at 640×480, 800×600, 1000×700, and 1280×900,
plus software rendering and doubled English strings. `--quick` runs only 1000×700
OpenGL. Window and drawable dimensions are logged so 1× runs are not mistaken
for physical HiDPI validation.

The redesigned screen exposes semantic row IDs (for example `gameplay.speed`)
for tests; do not locate settings controls by pixel coordinates. Slider updates
preview immediately and commit on release/idle, whereas discrete changes save
immediately. Bindings commit only after a complete edit. Legacy preference and
keyboard file formats remain unchanged.

`python3 test/run-game-speed-tests.py --settings-only` runs the main/in-game
settings, language, persistence, keyboard, multiplayer eligibility and camera
cadence regressions without starting the unrelated engine/replay scenarios.
The full invocation remains available and reports buffered diagnostics on timeout.

## Pre-game map preview regression

Build `scons -j6 release=1 server=0 map-preview-test`, then run
`python3 test/run-savegame-safety-tests.py --check-preferences build/src/MapPreviewHarness`.
For native software captures, run
`./build/src/MapPreviewHarness glob2-map-preview-tests --visual artifacts/map-preview` (under `xvfb-run -a` on
headless Linux). Linux CI builds and runs both modes. Fixtures exercise rectangular
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
Build `scons -j4 release=1 server=0 tournament-compatibility-test` and run
`build/src/TournamentCompatibilityTest` for real per-player Cortex/Maxima and partial
network-header checks. `python3 test/tournament_cli_integration.py --output DIR`
runs production CLI cases and retains saves, traces and logs. Use a fresh output
directory. `--initial FILE --ticks N` runs a retained initial state on another platform.

`test/tournament_reliability_pilot.py` is an opt-in localhost/SSH integration pilot.
It requires immutable macOS/Linux bundles and explicitly configured disposable
worker directories, and kills only processes belonging to that pilot. See
[the tournament guide](../docs/tools/tournaments.md) for commands and validation policy.
## Map CLI

Build the normal client with `scons release=1 server=0`, then run
`python3 test/test_map_cli.py build/src/glob2` (use `.exe` on Windows).
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
`python3 test/test_map_report.py build/src/glob2 build/src/MapReportHarness`
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
scons -j8 release=1 server=0 performance-telemetry-test
build/libgag/src/PerformanceTelemetryHarness
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
scons release=1 server=0 nicowar-farming-test
python3 test/run-savegame-safety-tests.py build/src/NicowarFarmingHarness .
```



## Engine save continuation

Build `scons release=1 server=0 unit-continuation-test`, then run
`build/src/UnitContinuationHarness`. Five checkpoints compare 256 subsequent
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
with `python3 test/maxima/check_save_continuation_fixture.py build/src/glob2`.

The retained Maxima format-115 checkpoint compares all 512 ticks from 30000
through 30511 against uninterrupted execution. Its compressed save, expected
per-tick hashes, and reproduction commands are in
[maxima/fixtures/save-continuation](maxima/fixtures/save-continuation/README.md).
