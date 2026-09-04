# glob2 (Legacy C++ Codebase)

This is the original Globulation 2 C++ codebase. It is legacy code and is not under active development.

## Build Commands

```bash
cd glob2

# Build (requires SCons, Python 3)
scons -j16

# Install (may need root)
scons install

# Clean
scons -c

# Build options
scons release=1          # Optimized release build
scons server=1           # Build YOG server only (no GUI/sound)
scons --build=/tmp/out   # Out-of-source build
scons mingw=1            # Native Windows build (MSYS2/mingw-w64)
scons mingwcross=1       # Cross-compile for Windows from Linux

# Custom paths
scons BINDIR=/path/bin INSTALLDIR=/path/share

# Uninitialized-read diagnostics (env var, NOT cached in options_cache.py)
DET_INIT=zero scons -j16     # compile with -ftrivial-auto-var-init=zero
DET_INIT=pattern scons -j16  # compile with -ftrivial-auto-var-init=pattern
```

**`DET_INIT` — hunting uninitialized-read nondeterminism.** Valgrind/MSan don't work on modern macOS, so instead build twice (`DET_INIT=zero` and `DET_INIT=pattern`) and hammer one seed serially per build (run `MallocPreScribble=1 MallocScribble=1` to also scribble the heap). If each build is internally stable but the two disagree, an uninitialized stack read is confirmed; if a scribbled build is still unstable run-to-run, the cause is not uninitialized memory. Rebuild affected objects when toggling — the env var is invisible to scons's dependency tracking.

**Dependencies:** SDL2, SDL2_net, SDL2_ttf, SDL2_image, libvorbis, libogg, speex, OpenGL, GLU, libepoxy, Boost (date_time; system is header-only from 1.69 on and only linked when present), zlib, fribidi, pcre. Optional: portaudio (voice chat). See `vcpkg.json` for the full list.

**Server build gotcha:** Always build the YOG server with `scons server=1` — never with a bare `scons build/src/glob2-server`. The `server=1` flag both selects the server target and defines `YOG_SERVER_ONLY` (which strips out GUI/audio code via `#ifndef` guards) and switches `libgag` to its stripped `libgag_server.a` variant. Without the flag, the .o files are compiled with the full GUI code path but link against the stripped libgag, producing dozens of misleading "undefined symbol" errors that look like fundamental rot. SCons also caches the option in `options_cache.py`, so once you've run `server=1`, subsequent `scons` invocations stay in server mode — pass `server=0` explicitly to switch back.

**Use `release=1` for headless runs.** The default `scons` build is `-g` with no `-O` flag, so `--nox` / `-test-games-nox` runs a debug binary that's roughly 10× slower than necessary. Always build with `scons release=1 -j16` for replay generation, benchmarking, or any throughput-sensitive headless work. Drop back to the default build only when you need a debugger, sanitizers, or fast incremental rebuilds. The flag is sticky via `options_cache.py`, so once set it persists until you pass `release=0`.

## Running Tests

Tests live in `glob2/test/` and have their own `test/SConstruct` — they are **not** built by the top-level `scons`. Build and run them from that directory:

```bash
cd glob2/test
scons -j16
./TestsRunner
./WinningConditionsHarness
```

The binaries are written in-tree (`test/TestsRunner`), not under `build/`. A top-level `scons` reports "up to date" without rebuilding them, so a stale binary will happily keep passing against deleted or changed assertions — rebuild from `test/` before trusting a run.

See [`test/README.md`](test/README.md) for the **Map subclass test pattern** — the trick used in `MapQueryTest.cpp` to unit-test Map predicates without pulling in `globalContainer`, `Bullet`, `Team`, or the full simulation surface.

## Build System Internals

SCons-based. `SConstruct` is the main build script with platform detection (Linux, Darwin, Windows/MinGW). Library checks are done via custom configure functions in `scons/`. Build options are cached in `options_cache.py`.

## Architecture

### Engine Loop (40ms tick)

The engine is **synchronous** — no multithreading in the core. Every 40ms, `Engine::run()` calls `.step()` on the class hierarchy. See `doc/sourceCodeUnderstanding.txt` for the full explanation.

```
Engine::run() loop:
  gui.step()                    → handle input
  net.pushOrder(gui.getOrder()) → send local player order to network
  net.pushOrder(ai.getOrder())  → send AI orders
  net.step()                    → exchange orders over network
  gui.executeOrder(...)         → execute all received orders
  gui.drawAll()                 → render
  sleep()                       → maintain 40ms frame
```

### Class Hierarchy

```
Engine
├─ NetGame          (network abstraction, order transmission via UDP)
└─ GameGUI          (rendering, input)
   └─ Game          (game state)
      ├─ Map        (terrain, resources)
      ├─ Team[32]   (a "colony" — has color, units, buildings)
      │  ├─ Unit[1024]
      │  └─ Building[1024]
      ├─ Player[32] (human interface — keyboard+mouse or AI)
      └─ Session    (serializable state: BasePlayer[32], BaseTeam[32])
```

**Team vs Player:** A Team is a logical colony (color, units, buildings). A Player is a controller (human or AI). Multiple players can control one team. Only teams can be allied.

### Deterministic Networking

All clients compute identical game state. Only Orders (player actions) are transmitted. This requires:
- **Use `Utilities::syncRand()`** instead of `rand()` — all machines must get the same random numbers
- **Avoid `std::set`** — it is non-deterministic across platforms
- Orders are buffered in per-player FIFO queues (256 slots) to handle latency

### Key Source Directories

- **`src/`** — Main game (382 files): engine, game logic, AI, networking, GUI screens, YOG online system
- **`libgag/`** — Graphics/GUI toolkit library (widget system, sprites, file I/O, rendering)
- **`libusl/`** — USL scripting language for maps/campaigns
- **`data/`** — Runtime assets (graphics, fonts, music, GUI resources)
- **`maps/`** — Game maps
- **`campaigns/`** — Campaign definitions
- **`doc/`** — Architecture documentation

### Headless Mode & Replay Generation

See [docs/headless-replays.md](docs/headless-replays.md) for full details on running headless AI games and generating `.replay` files for cross-codebase testing.

Quick reference:
```bash
# Random AI-vs-AI game, headless, runs until game over or 90k ticks (loops forever — kill after first game)
./glob2 -test-games-nox

# Single game from a .game file, headless
./glob2 --nox <game-file> <steps> <runs>
# Example: ./glob2 --nox games/my_ai_game.game 5000 1
```

Replays are always written to `replays/last_game.replay` (overwritten each game).

### AI System

Multiple AI implementations in `src/`: AICastor, AIEcho, AINicowar, AINumbi, AIToubib, AIWarrush — all inherit from `AI` base class.

### Orders System

All player actions are serialized as `Order` objects (see `src/Order.h`). `NullOrder` means "player did nothing this tick" — distinct from "we haven't heard from this player yet." Orders carry complete action data (team, position, type, unit counts).

### YOG (Online Gaming)

Server/client architecture for online play. `YOGServer` handles matchmaking, chat channels, ratings, map database. LAN play is peer-to-peer. Build server-only binary with `scons server=1`.

## Conventions

**Filenames:** PascalCase (`Game.cpp`, `GameRenderUnits.cpp`). Do NOT introduce snake_case C++ filenames in `glob2/`. When splitting an existing PascalCase file, the new pieces stay PascalCase too.

**Header guards:** `#pragma once`. We are moving away from legacy `#ifndef` guards.

**macOS rename caveat:** Case-only filename renames need `git mv -f Foo.cpp foo_tmp.cpp && git mv -f foo_tmp.cpp Foo.cpp` — plain `git mv foo.cpp Foo.cpp` may silently no-op on a case-insensitive filesystem.

**Unused includes:** run `tools/remove-unused-includes.py` after `scons compile_commands.json` — it drives `clang-tidy misc-include-cleaner` with a per-file compile gate. Never hand-guess or `clang-tidy --fix` blindly. Afterwards rebuild both `scons` and `scons server=1`, then verify with Workflow 1 in [`../docs/replay-verification.md`](../docs/replay-verification.md).

**Comments:** match the surrounding terse style — don't annotate every fix. The *why* of a change goes in the commit message, not bolted onto the line. Add a comment only when the code genuinely can't carry the reasoning itself (a non-obvious platform quirk, a subtle invariant); skip it when the line explains itself.

**Never document the past.** How code used to look, why it changed, what was moved/removed/renamed, or where something "used to live" belongs in the commit message and nowhere else — not in code comments, not in doc files, not in prose to the user. When you remove something, remove it completely: no tombstone comments, no "moved to X" pointers, no reference docs preserving deleted code "for context." Describe only what the code does *now*. The same applies to your replies — don't recount the history of a change unless explicitly asked.

## Logging is dead — do not restore

`glob2/src/LogFileManager.h` defines `#define fprintf if(false)fprintf`. Every translation unit that includes that header gets all `fprintf` calls rewritten to a runtime `if(false)` branch the compiler dead-code-eliminates. ~60 files include `LogFileManager.h`. Original devs disabled logging "for bugs" (per the deprecation comment) and never came back; the infrastructure has been load-bearing in constructors but doing nothing for 15+ years.

**Cleanup pattern when refactoring a class with a `FILE* logFile` member and a `globalContainer->logFileManager->getFile(...)` call in its constructor:** drop the field, drop the `getFile()` call, drop every `fprintf(logFile, …)` site (already no-ops), drop the include of `LogFileManager.h`. No replacement needed — there's no live consumer.

`LogFileManager.h` itself stays — it carries the silencing macro.

**Don't propose adding `spdlog` or another logging library to "preserve diagnostic intent."** There's no consumer asking for the data and 15 years of silence means there's no demand. Re-introduce only when something concrete needs it.

## Pathfinding gotcha — chamfer pass cap

For the chamfer distance transform in `glob2/src/map/gradient/MapGradientGlobal.cpp`, the convergence-pass cap is bounded by the Uint8 value range (256), **not** by the Borgefors 1986 1-pass result.

Borgefors 1-pass holds only on an obstacle-free grid. With obstacles forcing the propagation path to bend (mountains, water channels, building footprints), each direction change costs ~K/2 passes. Real glob2 maps (128×128, e.g. `G2.game`) need significantly more than 4 passes — empirically 32 was sufficient and 8 was not.

Use the value-range bound (256). The cap is a tripwire for monotonicity violations, not a real-workload throttle. Do not try to derive a tighter bound from grid geometry — obstacle topology dominates.
