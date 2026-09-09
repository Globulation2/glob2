# Development reference

Repository-specific build pitfalls, verification techniques and conventions.
Consult the sections relevant to the change; choose tools and workflow appropriate
to the task. Verify implementation details against current code and update these
notes when the referenced behavior changes.

## Build and test entry points

Choose build concurrency for available memory and other running builds; CPU count
alone is not a safe job limit. The commands below leave concurrency unspecified.

```sh
scons                        # default client: debug information, no optimization
scons release=1 server=0       # optimized client, including headless runs
scons release=1 server=1       # server with the correct stripped library
scons -C test                 # rebuild the separate test suite
(cd test && ./TestsRunner && ./WinningConditionsHarness)
```

- Top-level `scons` does not rebuild the separate `test/` suite. Rebuild there
  before trusting its binaries; explicit real-engine harness targets are listed
  in `test/README.md` and CI. Extend an existing relevant harness where practical.
- `options_cache.py` retains build options: specify `release` and `server` when
  switching configurations. A bare `build/src/glob2-server` target does not enable
  `YOG_SERVER_ONLY`; use `server=1`. Use `release=1` for headless measurements: the
  unoptimized build can be substantially slower. Use `release=0` for debugging.
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
explained in [docs/headless-replays.md](headless-replays.md). Replays default
to `~/.glob2/replays/last_game.replay`; `GLOB2_REPLAY_PATH` overrides the location.
See `test/README.md` for the Map-subclass test pattern that avoids linking the
full simulation for map-predicate tests.

## Simulation verification and diagnostics

A `Team` is a colony; a `Player` controls a team, and several players can share one.
For timing and scheduling, start with `src/Game_sync.cpp` and `src/EngineRun.cpp`.

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
- Before parallelizing gradients, inspect scratch ownership and input lifetimes in
  the current implementation; independent scratch, stable inputs and deterministic
  publication are relevant checks. The byte AI helper's
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
