# Code conventions

Local coding and tooling conventions for contributors. Contribution policy lives in [AGENTS.md](../../AGENTS.md).

## Local conventions

Preserve other contributors' work; use an isolated checkout when work overlaps.
Use PascalCase C++ filenames (not snake_case) and `#pragma once`. The map generator tree
(`src/map/generator/`, its tests and the lobby's landscape picker) is kept in the style
`.clang-format` at the repository root describes: tabs, Allman braces, 100 columns. Run
`clang-format -i` on the files you touch there (`pip install clang-format` gives a current
binary), and `clang-format --dry-run -Werror` over the tree to check it. Case-only renames
on macOS need an intermediate filename, e.g. `git mv Foo.cpp temp.cpp` then
`git mv temp.cpp foo.cpp`. Keep comments terse and about the current code; put change
history and rationale in commit messages; omit tombstone or “moved to” comments.
Diagnostics use `std::cerr`; there is no logging facility to target.
Windows headers define `near`, `far` and `small` as macros, so never name an identifier
after one: mingw expands `int near[3]` to `int [3]`, which then fails as a structured
binding declaration, and the error points at the syntax rather than at the macro.

For unused-include cleanup, generate `compile_commands.json` and use
`tools/remove-unused-includes.py`; do not apply blind bulk fixes. Rebuild client,
relay and affected tests, then verify behavior-preserving simulation changes as above.

## Header dependencies

Most rebuild time is spent parsing headers, so a header's includes are part of its
interface. Keep version constants (`VERSION_MINOR`, `SIM_REVISION`,
`FILE_FORMAT_VERSION_*`) out of headers: give a function an overload whose
current-format form is defined in the `.cpp`, and move version-gated bodies there.
Prefer a forward declaration or a small leaf header over including a wide one, and
keep simulation headers (`src/map`, `src/unit`, `src/building`, `src/team`,
`src/game`, `src/engine/sim`, `src/resource`, `src/field`) free of rendering, HUD,
UI and the full SDL umbrella. Hot per-cell accessors stay inline in their headers
because release builds do not use LTO.

Team, building and unit code reaches its game through `Team::ctx`
(`src/engine/sim/SimContext.h`): plain pointers to the game header, map header,
map, building types, unit cargo, area effects, animations, step counter, teams and
players, plus `ctx.services` (`sim/SimServices.h`, implemented by `Game`) for the
few calls such as `allocateScriptIdentity` or `resolveUnit`. Use `owner->ctx.x` and
the specific header of `x` rather than `owner->game->x` and `Game.h`; the pointer
read costs the same as the `game` read it replaces. Code that owns a game-wide
pipeline (hiring, turret targeting, team statistics, area effects) still takes a
`Game&`.

Map routing lives in `MapRouting` (`src/map/gradient/MapRouting.h`), owned by the
map and reached through `map->routing()`: pathfinding (`pathfindBuilding`,
`pathfindMaterial`, `pathfindArea`, point-to-point A*), building and material field
requests and queries (`requestBuildingField`, `queryMaterialDistance`,
`getMaterialGradient`, `materialAvailable`), the per-team material, market,
forbidden, guard and clear gradients with their stale flags, the material routing
cache, building gradient leases and the gradient pipeline. `Map.h` only
forward-declares it, so cell-only code does not rebuild when routing changes;
include `gradient/MapRouting.h` where a file routes. The routing implementation
files still include `Map.h` and read cells through its inline accessors, so a
query pays one pointer load to reach `MapRouting`, never one per cell. Static
movement helpers (`Map::swimClass`, `Map::SWIM_CLASS_EVEN`,
`Map::GRADIENT_COST_LIMIT`) and `topologyGeneration` stay on `Map`.

`python3 test/check_header_layering.py` enforces this from the static include
graph in a few seconds: layer ranks in `test/header_layers.json`, no version
defaults in headers, no new directory-level include cycles, and fan-in budgets (with
2% slack, so new source files do not trip them) in
`test/header_fanin_budget.json`. Existing violations are listed in
`test/header_layering_allowlist.txt` and `test/header_cycles_allowlist.txt`; after
removing some, run it with `--write` to drop their lines and lower the budgets.
It also caps `globalContainer` references in non-test simulation code per directory
(`test/global_container_budget.json`): simulation code reads the game's own
registries, so that tests can build a game without the application globals.
`python3 tools/include_graph.py top|fanin|why` reports which translation units a
header reaches and through which includers. `include_graph.py cost --days 60
--attribute 5` ranks headers by what their edits actually cost: edits in the
window times today's fan-in, with the includers that bring the costliest headers
to the most translation units. Raw fan-in treats a header nobody edits like one
edited daily, so start decoupling work from the cost ranking.

Decoupling must not slow the simulation. Keep hot accessors (per-cell map reads,
`MapStateView.h` queries, entity reference factories, the constexpr terrain cost
tables) inline; a body may move to another header, never out of line. For a
change to how simulation headers or their owners are held, build `perf=1 opt=3`
and run `python3 tools/inline_guard.py --baseline master build/linux/client/perf/o3`.
It lists hot-directory objects that gained calls into core simulation classes
(an accessor that stopped being inlined), and exits non-zero if any did. Add a
`tools/perf_ab.py` table at the perf-confirm tier (see
[comparing against master](build-options.md#comparing-against-master)).
