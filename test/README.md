# glob2/test/

CppUnit-based test fixtures and standalone harnesses for the C++ codebase. Most use this directory's `SConstruct`: run `scons -j16` here, then the in-tree `./TestsRunner` and `./WinningConditionsHarness` binaries. Rebuild these tests here before trusting a result; the top-level build does not build them. The exceptions are `GameGUISelectionHarness` and `TerrainResourcesHarness`, which use the top-level `selection-test` and `terrain-test` targets described below.

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

## Immobile unit gradient regression

From the repository root, run `scons -j8 release=1 server=0 immobile-unit-gradient-test`
and `./build/src/ImmobileUnitGradientHarness`. The harness links the real engine on a
64x64 map and checks that a freshly built map (`Map::setSize`) carries no immobile
unit on any tile and that a building's local gradient is reachable, then marks
immobile units and checks that each blocks exactly its own tile of the local
gradient and nothing else. It needs no display or external files.

### Savegame safety

Build `scons release=1 server=0 savegame-safety-test`, then run
`python3 test/run-savegame-safety-tests.py build/src/SavegameSafetyHarness`
(use `.exe` on Windows). No display is required. The runner uses a disposable
profile and working directory; an optional final argument supplies a truncated
save that must be rejected.

The harness checks the production autosave path, byte equivalence with direct
serialization, successful reload, truncated map data from file and memory
streams, recovery after failed loads, and oversized map-area strings. Atomic
replacement tests cover callback/open/rename failures and temporary-file cleanup.
On POSIX, child processes impose file-size limits to exercise short writes and
buffered flush errors while checking that the previous save survives unchanged.
