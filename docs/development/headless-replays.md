# Headless Mode & Replay Generation

For structured single-game/generation commands and distributed execution, see
[Distributed tournaments](../tools/tournaments.md). These preserve raw engine outcomes;
capped-game adjudication belongs in offline analysis.

Run AI games without a GUI to generate `.replay` files for cross-codebase fidelity testing (C++ vs Rust).

Version 121 gives each AI controller an independent saved random stream. Version
122 also gives each Econo and Nicowar controller a private gradient cache. AI
orders and game trajectories can differ from earlier versions for the same
seed. Older saved games still load, with shared gradient cache state copied into
each controller. Terrain format 134 adds property-driven movement and ecology.
Runtime terrain definitions (save format 136) are embedded before tile identities.
Replays and headless loads rebuild their compiled movement metadata from those
bytes, with no dependency on local authoring JSON. The existing map-content hash
binds distributed matches to the definitions.
Building format 137 adds configurable services and capability-driven AI; replays
recorded before version 137 became incompatible and network protocol 57 separated
clients using those rules. The current replay floor is 140 for damage-weighted routing and idle safety.
Supported saved games still load and adopt the current simulation;
the save floor remains 58.

Headless runs and scripted `-test-games` runs default autosaving off for that
process. Normal-play preferences are preserved. Use explicit initial saves or
the structured `--run-game --save initial/final/every:N` options when snapshots
are needed. Test harnesses that exercise autosaving can enable
`settings.autosaveGames` after `GlobalContainer::load()`.

Every `.map`/`.game` file this engine writes — autosaves, `--save-game-as`,
`--save initial/final/every:N`, the map generator's `--output` — is gzip
level 6 by default, with a `.gz` suffix appended to whatever destination name
was given (a name that already ends in `.gz` is left as-is). Loading is
transparent either way: an existing `.gz` file is preferred when both it and a
raw file of the same name exist, and legacy raw `.map`/`.game` files (no `.gz`
suffix) keep loading unchanged. Replays are unaffected and stay uncompressed.

For optional JavaScript controllers and map scripts, see
[JavaScript scripting](javascript.md).

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

Runs random AI-vs-AI games headlessly. Each game auto-ends at 90,000 ticks (~60 minutes of game time at 25 ticks/sec), or after `GLOB2_TEST_MAX_TICKS` ticks when that environment variable is a positive integer; a game stopped at the cap reports `winner_team=-1`. The cap only decides when the driver stops the game, not how ticks are simulated. An optional `count` parameter controls how many games to run (default: infinite).

```bash
./glob2 -test-games-nox 1    # run one game and exit
./glob2 -test-games-nox 5    # run five games and exit
./glob2 -test-games-nox      # run forever (kill with Ctrl+C)
GLOB2_TEST_MAX_TICKS=30000 ./glob2 -test-games-nox 1   # stop at 30,000 ticks
```

The random game setup (`Engine::createRandomGame`) creates one local player + N AI players with randomly chosen AI types from the map's team count.

### `GLOB2_TEST_RULES`

The same names and ranges are accepted by repeatable `--rule name=value`
arguments to structured `--run-game` commands. A saved game already carries its
rules, so `--rule` overrides are rejected when using `--load-game`. Effective
values are written to `result.json` under `resolved.rules`.

Turns custom-game rules on for `-test-games` and `-test-games-nox` matches, as comma-separated `name=value` pairs. An unknown name or a value outside its range stops the run.

| Name | Values | Rule |
| --- | --- | --- |
| `noGrowth` | 0-1 | No resource growth |
| `scarcity` | 0-3 | Scarce resources (growth 2x, 4x, 8x slower) |
| `instantConstruction` | 0-1 | Instant construction |
| `stockpile` | 0-3 | Stockpile start (+50, +150, +300 of each resource) |
| `noHunger` | 0-1 | No hunger |
| `noUpgrades` | 0-1 | Disable unit training and building upgrades; repairs remain available |
| `glassCannon` | 0-2 | Glass cannon (x2, x3 damage; HP and armor divided alike) |
| `fearless` | 0-1 | Fearless |
| `noPermadeath` | 0-1 | No permadeath |
| `peaceful` | 0-1 | Peaceful mode |
| `fortress` | 0-2 | Fortress buildings (x5, x10 building HP) |
| `suddenDeathTick` | 0-100000000 | Sudden-death timer at this tick (0 = off; the lobby offers 30-90 minutes, 45,000-135,000 ticks) |
| `winProbabilityPermille` | 0 or 501-1000 | Estimated win-probability condition (0 = off); distinct from the sudden-death timer |
| `<experiment key>` | 0-1 | An [experimental feature](../features/experimental-features.md) by its key, e.g. `guard-area-balancing`. The profile's Settings > Experiments apply first; a rule here overrides that one experiment |

```bash
GLOB2_TEST_RULES=scarcity=2,instantConstruction=1 ./glob2 -test-games-nox 1 --map Playground --matchup castor,warrush
```

### `--ai-types <list>`

Constrains the AI pool that `createRandomGame` draws from when generating
random matchups for `-test-games` / `-test-games-nox`. Comma-separated,
case-insensitive AI names. Default (no flag) is the legacy uniform pick
over `numbi, castor, warrush, econo, nicowar`.

```bash
# Bias the dataset toward strong AIs only:
./glob2 -test-games-nox 100 --ai-types nicowar,warrush

# Single-AI self-play replays (every AI slot is Nicowar):
./glob2 -test-games-nox 50 --ai-types nicowar
```

Valid names: `numbi`, `castor`, `warrush`, `econo`, `nicowar`, `cortex`, `cabino`.
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
  `maps/<name>.map`, or `maps/<name>.map.gz` when that exists). On a typo the
  binary fails fast with a clear message; it does **not** silently retry
  random maps.
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
  --map BigArena --matchup econo,nicowar \
  --save-game-as games/cross-replay.game
```

`<path>` is resolved by the file manager: relative paths land under `~/.glob2/` (so `--save-game-as games/foo.game` writes to `~/.glob2/games/foo.game.gz`); absolute paths (`/tmp/foo.game`, `C:\foo.game`) are used as-is (also gaining a `.gz` suffix). Requires `-test-games` or `-test-games-nox`; the save fires at random-game creation time. Without `GLOB2_TEST_SEED`, the wall-clock seed at run-start is captured and the .game.gz file is still reproducible — just not predictable across separate invocations. The command prints the actual path written.

**Local-player quirk:** the engine still creates a passive `P_LOCAL`
player on team 0 in `-test-games-nox` mode (the headless engine
expects one). The `GLOB2_GAME_END players=...` summary will show
`team0:local` alongside the matchup-assigned AI for team 0; both are
expected. Only the matchup AIs issue orders.

### `-test-games`

Same as `-test-games-nox` but **with GUI** — useful for visually verifying AI behavior.

## Verifying a match record

```sh
glob2 --verify-match <record.g2mr> --map <map-file> --out <dir> [--profile <name>]
glob2 --sim-version
```

`--verify-match` replays a relay match record (the format is in the
[turn protocol](../multiplayer/turn-protocol.md#match-record)) headlessly and judges the
checksums the live clients reported. Pass absolute paths: a macOS build changes its
working directory at startup. `--output-dir` is accepted for `--out`.

It reads the record, parses its MatchSetup JSON and checks that the record's map hash
and human seats agree with the setup. The map file must hash (SHA-256 of its
decompressed bytes) to the setup's `map.hash`. It then builds the `GameHeader` from the
setup with every human seat `P_IP`, as live clients do, and runs the match through
`Engine::initTurnMatch` with the record standing in for the relay. Recorded human
orders execute at their ticks and AI orders are computed locally. The end-of-replay GUI
path is never involved, and a seat's quit order does not stop the run. The verifier
takes the state checksum before every tick from 0 to the record's `endTick`, where a
live client takes it, and compares every recorded report with it.

Outputs in `<dir>`:

| File | Contents |
| --- | --- |
| `verdict.json` | `{"verdict": "verified" \| "diverged" \| "unverifiable", "seats": [...], "reason": "..."}`; `seats` only when diverged, `reason` only when unverifiable. It also carries the protocol package's `VerifyVerdict` members: `clients` (the same seats) and, unless unverifiable, `outcome` (`finalTick`, per-team `outcome`, `prestige` and `eliminatedTick`, and the SHA-256 of `result.json` and `match.replay`). `orderRejections` lists each human seat that sequenced orders the engine refused: `seat`, `rejected`, `stale`, per-reason counts and `firstRejectedTick` (see order validation in `docs/multiplayer/turn-protocol.md`). |
| `result.json` | The `--run-game` result format (`players`, `teams` with outcomes, `standard_statistics` and the 512-tick `history`, `winning_teams`) with `"job_type": "verify_match"`, the match id, both sim versions, the record flags, and a `verification` object (verdict, compared reports, first divergent tick per seat, and `order_checks`: per human seat, the orders accepted, stale and rejected, with reasons). It has no wall-clock fields, so a record verifies to the same bytes everywhere. |
| `checksums.txt` | One `tick checksum` line (hexadecimal) for every tick from 0 to `endTick`. |
| `match.replay` | A standard replay of the verified match, written by `ReplayWriter`. |
| `artifacts.json` | The file manifest, as for `--run-game`. |

The verdict is **verified** when every seat that reported matched at every tick it
reported; **diverged** when some seats differ and at least one matched, with the
differing seats listed; and **unverifiable** when no seat matched (engine
nondeterminism or a corrupt record), when no report could be compared, or when the
setup's sim version is not this build's (the run still completes and writes its trace).

The exit code is 0 for any verdict. It is 2 for a bad request (unreadable or corrupt
record, invalid setup, a map whose hash or team count does not match) and 3 for an
engine or I/O failure; both write a `result.json` with `status` and `diagnostic`.

`--sim-version` prints this build's simulation version as JSON,
`{"versionMinor": ..., "netProtocol": ..., "dataHash": "<64 hex>"}`. Engine agents
partition verification jobs by it; the definition of the data hash is in the
[turn protocol](../multiplayer/turn-protocol.md#simulation-version).

CI verifies `test/fixtures/multiplayer/FourSquares1.g2mr` on Linux, Windows, macOS and in
three browsers (`test/run-browser-determinism.py` and `browser/tests/determinism.spec.js`)
and requires every selected `checksums.txt` trace to be identical. Full verification
compares seven traces: two Linux builds, Windows, macOS and three browsers. The committed
`FourSquares1.verify-trace.txt` is the expected trace, and CI fails when the platforms
agree on a different one; the engine test that checks it also regenerates both files
under `--update-fixtures`. A change that moves the trace changed the simulation and
must bump `SIM_REVISION` ([simulation version](../multiplayer/turn-protocol.md#simulation-version)).

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

New output uses **GDS2**, a little-endian format with an embedded, immutable
building catalog. Its header is `GDS2`, a u32 record count, a u32 metadata byte
length, and UTF-8 JSON metadata. Metadata contains the engine simulation version,
canonical catalog snapshot and SHA-256 hash, projection version, and one model
channel per concrete variant. Empty files have zero records and metadata bytes.

Each record stores a u32 tick, u8 sender, u8 order type, a u32 state length and
state bytes, then a u32 payload length and order payload. State contains the
sender team's prestige, flags, resource and unit counts, 13 bounded model building
counts, then a fog-filtered grid up to 32×32. Each building contributes to exactly
one model channel (or none for an unsupported overlay); combined capabilities
never inflate the count. This projection is a model compatibility adapter, not an
engine building classification.

Grid cells are nine bytes: four u8 terrain/resource/own-unit/enemy-unit values,
two u16 own/enemy building values, then u8 discovery. Building values are concrete
catalog IDs plus one; zero means absent. Enemy state remains vision filtered.
See `src/game/diagnostics/DatasetWriter.h` for the complete layout.

Readers, including the external `glob2-ai-trainer` reader, must branch on magic
and add GDS2 support before consuming new files. Retain the GDS1 branch for old
datasets: its header has no metadata and its grid uses two u8 legacy family
channels (seven bytes per cell). Never reinterpret those family IDs as concrete
GDS2 catalog IDs. The repository's decoder fixtures cover both layouts; the
external trainer implementation is maintained separately.

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

### Per-team results (`GLOB2_TEAM_RESULTS`)

With `GLOB2_TEAM_RESULTS` set, the engine also prints one line per team
after `GLOB2_GAME_END`:

```
GLOB2_TEAM_RESULT team=0 result=undecided alive=1 eliminated_tick=-1 start=17,29 prestige=0 units=4 workers=4 explorers=0 warriors=0 buildings=1 sites=1
```

- `result` — `won`, `lost` or `undecided`: the team's win-condition
  state when the game stopped (`undecided` at a tick cap)
- `alive` — `Team::isAlive` at the end
- `eliminated_tick` — the tick at which `isAlive` was cleared, counted
  like `ticks`; `-1` for a team still alive
- `start` — the team's start position from the map
- `prestige` — final prestige; `units`, `workers`, `explorers` and
  `warriors` count live units; `buildings` counts finished buildings and
  `sites` building sites (virtual flags excluded)

The lines only read game state. `tools/map_fairness_tournament.py` uses
them together with `GLOB2_TEST_MAX_TICKS`; see
[Map fairness tournament](../map-generators/FAIRNESS_TOURNAMENT.md).

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
| 4 | Econo | `AI::ECONO` | Expansionist (shared AI runtime) |
| 5 | Nicowar | `AI::NICOWAR` | Strongest economy-focused AI (shared AI runtime) |
| 6 | Cortex | `AI::CORTEX` | Food-aware growth and supported attack waves (experimental) |
| 7 | Maxima | `AI::MAXIMA` | Standalone colony developer with relentless attacks; strategy configured through `data/maxima` and `GLOB2_MAXIMA_*` (see [Maxima](../ai/maxima/README.md)) |
| 8 | Cabino | `AI::CABINO` | Resurrected 2005-2007 Nicowar: independent cooperating modules, outside the shared AI runtime. |

Player types that trigger AI loading: any `BasePlayer::type >= P_AI (5)`. The player type encodes which AI: `P_AI + implementationID` maps to the enum above.

## Key Source Files

- `src/engine/Engine.cpp` — `initCustom()` loads `.game` files; `run()` contains the game loop; `createRandomGame()` sets up random AI matches
- `src/replay/ReplayWriter.cpp` — writes replay data live during gameplay
- `src/replay/ReplayReader.cpp` — reads replays for playback
- `src/app/GlobalContainer.cpp` — `parseArgs()` handles CLI flags
- `src/app/Glob2.cpp` — `runNoX()` and `runTestGames()` entry points
- `src/game/Game.cpp` — `executeOrder()` pushes orders to `ReplayWriter`
- `src/AI.cpp` — `AI::save()`/`AI::load()` with implementation dispatch

The existing `GLOB2_TEAM_TIMELINE` option also exports timestamped
[gameplay measurements](../ai/gameplay-statistics.md), retained measurement history and
an exact final snapshot. Legacy timeline records keep their existing formats.

AI controller readouts use the same option; see [AI telemetry](../ai/telemetry.md).
They identify individual players and controller generations. Replay playback does
not reconstruct internal AI decisions from recorded orders.

The same timeline option also exports [engine performance telemetry](performance-telemetry.md):
frame/work timing, subsystem costs, sampled pathfinding, per-player AI time, and autosave work.
Performance records describe this execution session and are not stored in saves.

### Gradient scheduling compatibility

Version 120 makes periodic resource, guard and clear fields publish eight ticks
after seeding. The current default is two background workers. Saves retain
completed pending fields and their remaining deadlines without publishing them early;
older saves remain loadable and start with an empty queue. The save compatibility
floor remains 58. Version 123 narrows forbidden-zone invalidations to affected
fields and gives escape fields an independent bounded refresh schedule. Replay
versions before 123 used a different routing schedule. The replay floor introduced in format
127 reflected the sixteen-team capacity changing Warrush's opening window from 24 to 32 ticks.
Format 127 also counts Maxima opponents and script-generation team slots while
keeping old saves loadable. Format 128 losslessly packs save data without changing
that replay floor. Network protocol 51 requires compact-map readers and rejects
older and newer clients. Background save finalization owns a captured state and
does not advance simulation; continuation checks must still compare the same
captured tick, seed and orders. Routing worker availability affects wall time only:
the serial fallback publishes on the same ticks. Headless `--gradient-workers 0` is the deterministic serial propagation control.

Version 139 / simulation revision 21 selects periodic preparation after the whole
Game tick, then lets Engine seed private gradient jobs alongside AI decisions in
one completed-tick observation phase. `--compute-threads 1` serializes that phase
at the same boundary. The default worker cap is unchanged. Fixed publication
cadence and saved pending deadlines are unchanged; saves drain deferred preparation
before serializing, and old saves still load. Moving the observation point can
change routes/AI trajectories, so version 139 introduced a replay boundary.
Damage-weighted routing raises the current floor to 140. LAN and online
sim-version gates reject clients using older rules. See the
[phase contract](reference.md) before adding new parallel work.

### Probability-based early victory

Structured `--run-game` runs accept `--win-probability PERMILLE` (501–1000).
This appends the optional rule after existing winning conditions; omit it to play
the game out. Evaluation begins at tick 5120 and repeats every 512 ticks. Results
called by the model report `termination: "win_probability"`. See the
[model guide](../win-probability-model.md) for calibration and its limits.

Format 132 preserves legacy AI clocks, Nicowar explorer phase latches, Cabino
specialist/cache state and queued order envelopes, and Cortex learned policy
selection and weights. The save floor stays 58 and replay floor stays 127;
fresh-game decision behavior is unchanged. Older saves use historical defaults
for omitted state, whose original values cannot be recovered. Protocol 54 carries
the additional continuation fields.
