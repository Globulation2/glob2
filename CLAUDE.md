# glob2 (C++ Codebase)

The Globulation 2 C++ codebase. Actively maintained.

## Build

```bash
scons -j16                 # default: -g, no -O
scons release=1 -j16       # optimized; use for any headless/replay work
scons server=1             # YOG server only (no GUI/sound)
scons -c                   # clean
scons --build=/tmp/out     # out-of-source
scons mingw=1              # native Windows (MSYS2/mingw-w64)
scons mingwcross=1         # cross-compile for Windows from Linux
scons BINDIR=/path/bin INSTALLDIR=/path/share
```

Options are cached in `options_cache.py`, so `release=1` and `server=1` stick until you pass `release=0` / `server=0`.

**Server build gotcha.** Always build the server with `scons server=1`, never a bare `scons build/src/glob2-server`. The flag defines `YOG_SERVER_ONLY` and switches to the stripped `libgag_server.a`. Without it the objects are compiled with GUI code but linked against the stripped library, producing dozens of misleading undefined-symbol errors.

**Use `release=1` for headless runs.** The default build is roughly 10× slower under `--nox` / `-test-games-nox`. Drop back to the default only when you need a debugger, sanitizers, or fast incremental rebuilds.

**`DET_INIT` — hunting uninitialized-read nondeterminism.** Valgrind/MSan don't work on modern macOS. Instead build twice, `DET_INIT=zero scons` and `DET_INIT=pattern scons`, and hammer one seed serially per build (add `MallocPreScribble=1 MallocScribble=1` to scribble the heap). If each build is internally stable but the two disagree, an uninitialized stack read is confirmed; if a scribbled build is still unstable run-to-run, the cause is not uninitialized memory. The env var is invisible to scons's dependency tracking, so rebuild affected objects when toggling.

**Dependencies:** SDL2, SDL2_net, SDL2_ttf, SDL2_image, libvorbis, libogg, speex, OpenGL, GLU, libepoxy, Boost date_time, zlib, fribidi, pcre. Optional: portaudio. See `vcpkg.json`.

## Tests

Tests live in `test/` with their own `test/SConstruct` and are **not** built by the top-level `scons`:

```bash
cd test
scons -j16
./TestsRunner
./WinningConditionsHarness
```

Binaries are written in-tree. A top-level `scons` reports "up to date" without touching them, so a stale binary keeps passing against changed assertions. Rebuild from `test/` before trusting a run.

See [`test/README.md`](test/README.md) for the Map subclass pattern used to unit-test Map predicates without pulling in `globalContainer`, `Team`, or the full simulation.

## Headless Runs and Replays

See [docs/headless-replays.md](docs/headless-replays.md).

```bash
./glob2 -test-games-nox                      # random AI-vs-AI games, loops forever
./glob2 --nox <game-file> <steps> <runs>     # single game from a .game file
```

Replays go to `~/.glob2/replays/last_game.replay` unless `GLOB2_REPLAY_PATH` is set. The lockstep engine is the regression test: identical replays mean identical behavior. See [`../docs/replay-verification.md`](../docs/replay-verification.md).

## Architecture

The engine is synchronous, one 40ms tick at a time, and every client computes identical state from the same stream of `Order`s. `doc/sourceCodeUnderstanding.txt` has the full walkthrough. Two things to hold onto:

- **Team vs Player.** A Team is a colony (color, units, buildings). A Player is a controller, human or AI. Several players may control one team; only teams can be allied.
- **Determinism.** Use `Utilities::syncRand()`, never `rand()`. Avoid `std::set`; iteration order is not portable.

AI implementations live under `src/ai/`: Castor, Cortex, Echo, Nicowar, Numbi, Warrush, all behind `AIImplementation`.

## Conventions

**Filenames:** PascalCase (`Game.cpp`, `GameRenderUnits.cpp`). Do not introduce snake_case C++ filenames. On macOS a case-only rename needs `git mv -f Foo.cpp foo_tmp.cpp && git mv -f foo_tmp.cpp Foo.cpp`; a direct rename may silently no-op.

**Header guards:** `#pragma once`.

**Unused includes:** run `tools/remove-unused-includes.py` after `scons compile_commands.json`. Never hand-guess or run `clang-tidy --fix` blindly. Afterwards rebuild both `scons` and `scons server=1` and verify with Workflow 1 in the replay-verification doc.

**Comments:** match the surrounding terse style. The *why* of a change goes in the commit message. Add a comment only when the code can't carry the reasoning itself.

**Never document the past.** How code used to look, why it changed, or where something "used to live" belongs in the commit message only. When you remove something, remove it completely: no tombstone comments, no "moved to X" pointers. Describe only what the code does now. The same applies to replies: don't recount the history of a change unless asked.

## Logging

There is no logging facility; diagnostics go to `std::cerr`. Do not propose adding a logging library; nothing consumes the data.

## Pathfinding: chamfer pass cap

In `src/map/gradient/MapGradientGlobal.cpp` the chamfer distance transform's convergence-pass cap is bounded by the Uint8 value range (256), not by the Borgefors 1-pass result. Borgefors holds only on an obstacle-free grid; with obstacles each bend in the propagation path costs about K/2 passes, and real 128×128 maps needed well over 8. The cap is a tripwire for monotonicity violations, not a throttle. Do not derive a tighter bound from grid geometry.
