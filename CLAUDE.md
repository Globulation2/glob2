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
scons mingw=true         # Windows cross-compile

# Custom paths
scons BINDIR=/path/bin INSTALLDIR=/path/share
```

**Dependencies:** SDL2, SDL2_net, SDL2_ttf, SDL2_image, libvorbis, libogg, speex, OpenGL, GLU, libepoxy, Boost (thread, date_time, system), zlib, fribidi, pcre. Optional: portaudio (voice chat). See `vcpkg.json` for the full list.

## Running Tests

```bash
# Tests are in test/ - built as part of scons, run the test binary after build
```

## Build System Internals

SCons-based. `SConstruct` is the main build script with platform detection (Linux, Darwin, Windows/MinGW). Library checks are done via custom configure functions in `scons/`. Build options are cached in `options_cache.py`.

## CI

Travis CI runs Docker-based builds on Ubuntu 18.04–21.04. See `.travis.yml` in the repo root.

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
- **`libwee/`** — Utility library
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
