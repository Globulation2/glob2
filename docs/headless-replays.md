# Headless Mode & Replay Generation

Run AI games without a GUI to generate `.replay` files for cross-codebase fidelity testing (C++ vs Rust).

## CLI Flags

### `--nox <game-file> <steps> <runs>`

Runs a saved `.game` file headlessly.

- `<game-file>` — path to a `.game` save file containing map, players, and AI configuration
- `<steps>` — number of simulation ticks to run (0 = run until game over)
- `<runs>` — how many times to repeat the game

```bash
./glob2 --nox games/nicowar_2v2.game 5000 1
```

To create a `.game` file with specific AI players: start the game with GUI, set up a custom game with the desired AI types, then save immediately. That save becomes the `.game` file you pass to `--nox`.

### `-test-games-nox [count]`

Runs random AI-vs-AI games headlessly. Each game auto-ends at 90,000 ticks (~60 minutes of game time at 25 ticks/sec). An optional `count` parameter controls how many games to run (default: infinite).

```bash
./glob2 -test-games-nox 1    # run one game and exit
./glob2 -test-games-nox 5    # run five games and exit
./glob2 -test-games-nox      # run forever (kill with Ctrl+C)
```

The random game setup (`Engine::createRandomGame`) creates one local player + N AI players with randomly chosen AI types from the map's team count.

### `-test-games`

Same as `-test-games-nox` but **with GUI** — useful for visually verifying AI behavior.

## Replay Output

All modes write replays to `~/.glob2/replays/last_game.replay` by default.
**Each new game overwrites the previous replay** — copy it out between runs,
or override the path per-game with the `GLOB2_REPLAY_PATH` env var:

```bash
GLOB2_REPLAY_PATH=replays/game-001.replay ./glob2 -test-games-nox 1
```

This lets concurrent headless instances write to distinct files (used by the
AI-trainer replay-generation pipeline).

For cross-codebase testing, copy replays to the Rust test fixture directory:
```bash
cp ~/.glob2/replays/last_game.replay ../glob2-rust/test_data/replays/
```

The `ReplayWriter` records live during gameplay:
- At game start: writes the full game state header via `GameGUI::save()`, then replay version (`VERSION_MAJOR`, `VERSION_MINOR`)
- Each tick: calls `advanceStep()` to track tick deltas
- When an order executes: writes `u16 stepsSinceLastOrder` + the serialized order via `NetSendOrder::encodeData()`
- At game end: writes a final `NullOrder` to terminate the stream

## Replay File Format

```
[GameGUI::save() header]     — full game state at tick 0
[u16 VERSION_MAJOR]          — replay format version
[u16 VERSION_MINOR]
[order stream]               — repeating until NullOrder:
  u16 stepsSinceLastOrder    — tick delta since previous order
  NetSendOrder:
    u32 size                 — byte count of order data
    u8  orderType            — order type ID (see Order.h)
    [order data bytes]       — type-specific payload
    u8  sender               — player index
    u32 checksum             — game state checksum at this tick
  ...
[u16 0 + NullOrder]          — end marker
```

## AI Types

| ID | Name | Enum | Notes |
|----|------|------|-------|
| 0 | None | `AI::NONE` | Does nothing |
| 1 | Numbi | `AI::NUMBI` | Simple beginner AI |
| 2 | Castor | `AI::CASTOR` | Default toggle AI, moderate |
| 3 | Warrush | `AI::WARRUSH` | Aggressive rush strategy |
| 4 | ReachToInfinity | `AI::REACHTOINFINITY` | Expansionist (Echo wrapper) |
| 5 | Nicowar | `AI::NICOWAR` | Strongest all-round AI (Echo wrapper) |
| 6 | Toubib | `AI::TOUBIB` | Simple AI |

Player types that trigger AI loading: any `BasePlayer::type >= P_AI (5)`. The player type encodes which AI: `P_AI + implementationID` maps to the enum above.

## Key Source Files

- `src/Engine.cpp` — `initCustom()` loads `.game` files; `run()` contains the game loop; `createRandomGame()` sets up random AI matches
- `src/ReplayWriter.cpp` — writes replay data live during gameplay
- `src/ReplayReader.cpp` — reads replays for playback
- `src/GlobalContainer.cpp` — `parseArgs()` handles CLI flags
- `src/Glob2.cpp` — `runNoX()` and `runTestGames()` entry points
- `src/Game.cpp` — `executeOrder()` pushes orders to `ReplayWriter`
- `src/AI.cpp` — `AI::save()`/`AI::load()` with implementation dispatch
