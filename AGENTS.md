# Working on Globulation 2

This is the shared contributor and coding-agent guide. `CLAUDE.md` is a Git-tracked
symlink to this file; edit `AGENTS.md` only. If a checkout materializes symlinks as
plain text, read `AGENTS.md` directly rather than creating a second copy.

## Repository map

| Area | Start here |
| --- | --- |
| Simulation and orders | `src/Game_sync.cpp`, `src/EngineRun.cpp`, `src/Order*.cpp` |
| Units, buildings and teams | `src/unit/`, `src/building/`, `src/team/` |
| Map state and pathfinding | `src/map/`, especially `gradient/` and `pathfind/` |
| AI implementations | `src/ai/`, behind `AIImplementation` |
| Rendering, menus and editor | `src/render/`, `src/gui/`, `src/*Screen*`, `src/map/edit/` |
| Network and multiplayer service | `src/net/`, `src/yog/` |
| Graphics/UI and scripting libraries | `libgag/`, `libusl/`, `src/sgsl/` |
| Builds and platform coverage | `SConstruct`, `src/SConscript`, `scons/`, `.github/workflows/build.yml`, `vcpkg.json` |
| Tests and replay usage | [test/README.md](test/README.md), [docs/headless-replays.md](docs/headless-replays.md) |

A `Team` is a colony; a `Player` controls a team, and several players can share one.
The engine advances in 40 ms ticks. Multiplayer clients must compute the same
simulation from the same initial state and orders. See
[doc/sourceCodeUnderstanding.txt](doc/sourceCodeUnderstanding.txt) for more context.

## Build and test entry points

```sh
scons -j8                        # default client: debug information, no optimization
scons -j8 release=1 server=0       # optimized client, including headless runs
scons -j8 release=1 server=1       # server with the correct stripped library
scons -C test -j8                 # rebuild the separate test suite
(cd test && ./TestsRunner && ./WinningConditionsHarness)
```

- Top-level `scons` does not rebuild the separate `test/` suite. Rebuild there
  before trusting its binaries; explicit real-engine harness targets are listed
  in `test/README.md` and CI. Extend an existing relevant harness where practical.
- `options_cache.py` retains build options: specify `release` and `server` when
  switching configurations. A bare `build/src/glob2-server` target does not enable
  `YOG_SERVER_ONLY`; use `server=1`. Use `release=1` for headless measurements: the
  unoptimized build can be roughly ten times slower. Use `release=0` for debugging.
  `scons -c` cleans; `--build=/tmp/out` selects an out-of-source build directory;
  `BINDIR=/path/bin INSTALLDIR=/path/share` selects installation locations.
- `mingw=1` builds natively on Windows; `mingwcross=1` cross-compiles. Dependencies
  are in `vcpkg.json` and CI. Check the affected platform jobs rather than assuming
  a successful local build covers another compiler or operating system.
- Dependencies include SDL2/net/ttf/image, Vorbis/Ogg, Speex, OpenGL/GLU, libepoxy,
  Boost date_time, zlib, fribidi and pcre; PortAudio is optional.
- `CCACHE=1` opts into the shared compiler cache. Unset it when generating
  `compile_commands.json`; do not add `CCACHE_SLOPPINESS` settings that weaken
  content or time-macro validation (`include_file_mtime`, `include_file_ctime`,
  `time_macros`). `scons/ccache.py` is used by both build entry points; the environment
  opt-in is not persisted in `options_cache.py`.
- Keep harness runs out of personal profiles: use the existing disposable-profile
  runners and retain fixtures, seeds, logs and checksums needed to reproduce a result.

For headless games, use the client binary's `--nox <game-file> <steps> <runs>`
option. `-test-games-nox` runs random AI games indefinitely unless bounded as
explained in [docs/headless-replays.md](docs/headless-replays.md). Replays default
to `~/.glob2/replays/last_game.replay`; `GLOB2_REPLAY_PATH` overrides the location.
See `test/README.md` for the Map-subclass test pattern that avoids linking the
full simulation for map-predicate tests.

## Preserve the engine's contracts

- Use `Utilities::syncRand()` for simulation randomness. Keep iteration and tie
  breaking deterministic; never depend on pointer ordering, hash-table iteration,
  thread scheduling or wall-clock budgets for simulation decisions.
- For behavior-preserving refactors and optimizations, compare base and changed
  builds using identical saves/maps, seeds, settings and orders. Compare per-tick
  state/checksums as well as replay bytes: matching orders alone do not prove that
  clients computed identical states.
- For changes affecting simulation portability, run the same retained scenarios
  across the affected compilers/platforms and compare traces. Separate repeatability
  on one machine from cross-platform equivalence. CI build success alone proves neither.
- Check save/load continuation when state or scheduling changes. A derived cache
  can still affect future decisions; do not assume that it is safe to discard.
  Treat save-format, replay and network compatibility as separate questions. If
  simulation rules change, assess replay acceptance and protocol/version gates even
  when the saved byte layout is unchanged.
- Versioning rule: when the save format changes, bump `VERSION_MINOR` and preserve
  older saves through version-gated loading, or explicitly document an approved
  compatibility break. When simulation changes invalidate old replays or mixed-client
  games, update replay acceptance and `NET_PROTOCOL_VERSION`/YOG minimums as needed.
  Test acceptance/rejection at the version boundaries; unchanged saved bytes do not
  establish replay or network compatibility.
- Intentional bug fixes or gameplay changes may change old outcomes. Explain the
  difference and test the intended behavior rather than claiming old/new equivalence.
- Building/resource gradients use shared serial scratch; adding parallel work needs
  independent scratch, stable inputs and determinism tests. The byte AI helper's
  256-pass chamfer cap is a value-range bound, not an obstacle-free geometry bound.
  Obstacles can require many sweeps even on 128×128 maps; the cap guards monotonicity
  failures. Do not lower it based on the obstacle-free one-pass result.
- For suspected uninitialized reads on macOS, `DET_INIT=zero` versus `pattern` and
  allocator scribbling (`MallocPreScribble=1 MallocScribble=1`) can help isolate
  the cause where Valgrind/MSan are unavailable. Repeat the same seed serially in
  each build: stable runs that differ between initialization modes suggest an
  uninitialized read. Instability within a mode needs further investigation.
  SCons does not track `DET_INIT`:
  rebuild affected objects when changing it. Report the actual sanitizer or diagnostic
  coverage and its limits.

## Review and merge ownership

The author prepares the PR and addresses feedback. An independent reviewer owns
approval and merging; the author does not merge their own PR. Record the responsible
human contributors when agents do the work. A second agent session for the same
author does not establish independent review.

- Focused, reproducible bug fixes and behavior-preserving cleanup may be reviewed
  and merged by an agent acting for a different maintainer, after the relevant
  evidence and checks pass and outstanding objections are resolved.
- Features, balance changes and substantive engine/architecture/gameplay changes
  need explicit approval from a human maintainer other than the author. These
  changes are welcome; discuss broad designs and tradeoffs early. Do not label an
  intentional mechanic change as a bug fix to bypass that approval.
- Policy changes need human maintainer agreement. Unresolved technical or design
  objections hold a merge; tests and AI agreement do not settle disagreement.
- Before merging, the reviewer checks the current head, discussions, approvals and
  required CI. Merge only the reviewed/tested head and respect repository protections.
  Further edits require review and revalidation appropriate to those edits.

On the PR, give the problem, intended result and a concise verification record:
exact tested revision/base, commands, fixtures, platforms/toolchains, results and
limits. Bug fixes should include a regression demonstrated to fail before the fix
where feasible; explain manual-only coverage. Performance claims need comparable
before/after workloads, including regressions. Keep unrelated changes in separate PRs.

## Local conventions

Preserve other contributors' work; use an isolated checkout when work overlaps.
Use PascalCase C++ filenames (not snake_case) and `#pragma once`. Case-only renames
on macOS need an intermediate filename, e.g. `git mv Foo.cpp temp.cpp` then
`git mv temp.cpp foo.cpp`. Keep comments terse and about the current code; put change
history and rationale in commit messages; omit tombstone or “moved to” comments.
Diagnostics use `std::cerr`; there is no logging facility to target.

For unused-include cleanup, generate `compile_commands.json` and use
`tools/remove-unused-includes.py`; do not apply blind bulk fixes. Rebuild client,
server and affected tests, then verify behavior-preserving simulation changes as above.
