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

### `--ai-types <list>`

Constrains the AI pool that `createRandomGame` draws from when generating
random matchups for `-test-games` / `-test-games-nox`. Comma-separated,
case-insensitive AI names. Default (no flag) is the legacy uniform pick
over `numbi, castor, warrush, reachtoinfinity, nicowar`.

```bash
# Bias the dataset toward strong AIs only:
./glob2 -test-games-nox 100 --ai-types nicowar,warrush

# Single-AI self-play replays (every AI slot is Nicowar):
./glob2 -test-games-nox 50 --ai-types nicowar
```

Valid names: `numbi`, `castor`, `warrush`, `reachtoinfinity`, `nicowar`.
Unknown names are reported on stderr and skipped (an empty
remaining pool falls back to default behavior).

### `--map <name>` and `--matchup <list>`

Pin the map and per-team AI assignment for `-test-games-nox`, replacing
the random pieces with explicit choices. Used by the AI-trainer
pipeline to produce curated datasets (exact counts per matchup).

```bash
# Nicowar (team 0) vs. Warrush (team 1) on the Playground map:
./glob2 -test-games-nox 1 --map Playground --matchup nicowar,warrush

# Three-team game on a custom map:
./glob2 -test-games-nox 1 --map "BigArena" --matchup nicowar,warrush,numbi
```

- `--map <name>` is the bare map filename without `.map` (resolved as
  `maps/<name>.map`). On a typo the binary fails fast with a clear
  message; it does **not** silently retry random maps.
- `--matchup <list>` is one AI name per team. The list length must
  match the loaded map's `getNumberOfTeams()` exactly — startup fails
  otherwise.
- `--matchup` requires `--map` (we need the map's team count to
  validate the matchup before launching).
- `--matchup` is mutually exclusive with `--ai-types` (pool vs. exact).

### `--save-game-as <path>`

Writes the fully-initialised tick-0 game state to `<path>` as a `.game` file before running. Lets a `-test-games-nox` scenario be replayed deterministically later via `--nox <path>`. Pair with `GLOB2_TEST_SEED` for full reproducibility — the seed is mirrored into the saved `GameHeader` so the reloaded run matches the original.

```bash
GLOB2_TEST_SEED=42 ./glob2 -test-games-nox 1 \
  --map BigArena --matchup reachtoinfinity,nicowar \
  --save-game-as games/cross-replay.game
```

`<path>` is resolved by the file manager: relative paths land under `~/.glob2/` (so `--save-game-as games/foo.game` writes to `~/.glob2/games/foo.game`); absolute paths (`/tmp/foo.game`, `C:\foo.game`) are used as-is. Requires `-test-games` or `-test-games-nox`; the save fires at random-game creation time. Without `GLOB2_TEST_SEED`, the wall-clock seed at run-start is captured and the .game file is still reproducible — just not predictable across separate invocations.

**Local-player quirk:** the engine still creates a passive `P_LOCAL`
player on team 0 in `-test-games-nox` mode (the headless engine
expects one). The `GLOB2_GAME_END players=...` summary will show
`team0:local` alongside the matchup-assigned AI for team 0; both are
expected. Only the matchup AIs issue orders.

### `-test-games`

Same as `-test-games-nox` but **with GUI** — useful for visually verifying AI behavior.

## AI-Trainer Dataset Output

When `GLOB2_DATASET_PATH` is set, the engine writes one binary record
per executed order to that path, alongside the normal `.replay`. Used
by the `glob2-ai-trainer` pipeline to feed BC training without needing
to re-simulate the replay.

```bash
GLOB2_DATASET_PATH=/tmp/game.dataset \
GLOB2_REPLAY_PATH=/tmp/game.replay \
  ./glob2 -test-games-nox 1 --map A_big_pond --matchup nicowar,warrush,numbi
```

Format (little-endian):

```
HEADER (8 bytes)
  [4B] magic "GDS1"
  [4B] u32 num_records          (patched at close)

PER-RECORD
  [4B] u32 tick
  [1B] u8  sender_player_index
  [1B] u8  order_type
  [4B] u32 state_blob_len
  [state_blob_len bytes]        state features
  [4B] u32 order_payload_len
  [order_payload_len bytes]     order payload (Order::getData())
```

`state_blob_len` is currently always 0 — observation features land
alongside the trainer's training loop. The wire format doesn't change
shape when that happens; the blob just stops being empty.

No version field: single producer, single consumer, regenerating
datasets is cheap. If the schema ever changes wire-incompatibly, bump
the magic to `GDS2` and parsers reject by magic mismatch.

See `glob2/src/DatasetWriter.{h,cpp}` for the writer.

## Replay Output

All modes write replays to `~/.glob2/replays/last_game.replay` by default.
**Each new game overwrites the previous replay** — copy it out between runs,
or override the path per-game with the `GLOB2_REPLAY_PATH` env var:

```bash
GLOB2_REPLAY_PATH=replays/game-001.replay ./glob2 -test-games-nox 1
```

This lets concurrent headless instances write to distinct files (used by the
AI-trainer replay-generation pipeline).

## Game-End Summary Line

When `automaticEndingGame` fires (set by `--nox`, `-test-games-nox`, and
`-test-games`), the engine prints a machine-parseable summary line right
after the existing tick/minute log:

```
GLOB2_GAME_END ticks=2483 winner_team=1 seed=1777219846 map="Playground" orders=2525 players=team0:local,team1:Nicowar,team2:Warrush
```

- `ticks` — total simulation ticks elapsed
- `winner_team` — first team with `hasWon` set, or `-1` on timeout
- `seed` — `GameHeader::getRandomSeed()` value used for this game
- `map` — map name (`MapHeader::getMapName()`); double-quoted to allow
  spaces. Value never contains literal `"` characters in practice
- `orders` — count of orders pushed into the replay (excludes voice and
  null orders); from `ReplayWriter::getOrderCount()`
- `players` — comma-separated `teamN:type` pairs; type is `local`, `ip`,
  `none`, or an AI name from `AINames::getAIText`

The format is intended for grep/regex consumption — fields are
space-separated key=value, with `map` quoted.

For cross-codebase testing, the canonical baselines live in `glob2/tests/baselines/`. See [`docs/replay-verification.md`](../../docs/replay-verification.md) at the workspace root for the full verification workflows and the regeneration procedure.

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
| 5 | Nicowar | `AI::NICOWAR` | Strongest economy-focused AI (Echo wrapper) |

Player types that trigger AI loading: any `BasePlayer::type >= P_AI (5)`. The player type encodes which AI: `P_AI + implementationID` maps to the enum above.

## Key Source Files

- `src/Engine.cpp` — `initCustom()` loads `.game` files; `run()` contains the game loop; `createRandomGame()` sets up random AI matches
- `src/ReplayWriter.cpp` — writes replay data live during gameplay
- `src/ReplayReader.cpp` — reads replays for playback
- `src/GlobalContainer.cpp` — `parseArgs()` handles CLI flags
- `src/Glob2.cpp` — `runNoX()` and `runTestGames()` entry points
- `src/Game.cpp` — `executeOrder()` pushes orders to `ReplayWriter`
- `src/AI.cpp` — `AI::save()`/`AI::load()` with implementation dispatch
