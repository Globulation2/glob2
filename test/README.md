# glob2/test/

CppUnit-based test fixtures for the C++ codebase. These have their own `SConstruct` and are **not** built by the top-level `scons` — run `scons -j16` from this directory, then the in-tree `./TestsRunner` and `./WinningConditionsHarness` binaries. A top-level build reports "up to date" without touching them, so rebuild here before trusting a result.

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
        // arraysBuilt stays false, so clear() takes the else-branch
    }
    ~GrassMap() {
        // Map::clear()'s else-branch asserts these are 0 before letting Map::~Map() proceed
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
