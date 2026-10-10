# Turn engine integration and versions

Build shared match setups and preserve the simulation-version acceptance boundary.

## Engine integration

`TurnLockstepSession` (`TurnLockstep.h/.cpp`) puts a `TurnSession` behind the engine's
`LockstepSession` interface. The engine calls it exactly where it calls `NetEngine` for
single player and the legacy games:

| Engine call | `TurnSession` behaviour |
| --- | --- |
| `addLocalOrder` | Encodes, submits and flushes a human order; null and latency orders are ignored |
| `pushOrder(order, p, isAI)` | Queues a locally computed AI order, as today |
| `advanceStep(checksum)` | Sends `ChecksumReport` when the executed tick is a multiple of `checksumInterval` |
| `tickReady` (`allOrdersReceived`) | The next tick is below the horizon and every AI seat has its order |
| `orderReceived(p)` | AI seat: its queue is not empty. Human seat: the next tick is authorized |
| `retrieveOrder(p)` | The bundled order for that seat and tick, or a `NullOrder`; `sender` is set to `p` |
| `clearTopOrders` | Drops the tick's orders and advances the executed tick |
| `getWaitingOnMask` | See [presence](turn-timing.md#presence-reconnect-and-grace) |
| `matchCheckSums` | Always true; the relay arbitrates |
| `flushAllOrders` | Nothing to do: orders are sent as soon as they are added |

### Starting a turn game

LAN and the online client start a match with one call:

```cpp
Engine::TurnMatchStart start;
start.setup = Online::MatchSetup::parse(setupJson);          // validated MatchSetup
start.mapFile = Online::resolveMatchMap(start.setup, path);  // content hash checked
start.localSeat = seat;                                      // from the ticket
start.transport = transport;   // std::shared_ptr<Turn::TurnTransport> to the relay
start.config.ticket = ticket;
engine.initTurnMatchTask(std::move(start));  // or initTurnMatch(), then run()
```

`initTurnMatchTask` builds the `GameHeader` from the setup (see
[match setup](turn-engine.md#match-setup-and-simulation-version)), loads the map with the saved
GUI data ignored, and installs the session in place of the `NetEngine`. The caller
keeps its own reference to the transport and closes it after the session has ended,
so frames queued by `quit()` can still be delivered. `Engine::turnSession()` exposes
the session (presence, latency, buffer) for a connection HUD.

### The engine loop

`Engine::stepSession` calls `pumpTurnSession` before gathering orders:

- `TurnSession::update(now)` pumps the transport and timers every frame. Between
  steps, the host loop calls `Engine::pollTurnSession` at least every `TURN_POLL_MS`
  (5 ms; `sessionPollDelay()` caps the host's sleep), which only reads the connection.
  A turn game draws only after a step, so these polls draw nothing.
- **Orders.** Each step hands every order the GUI has queued to the session, even while
  waiting for a bundle, rather than one order per executed tick. The session sends them
  at the rate the relay sequences them and merges waiting ones with the same target
  ([order pacing](turn-wire.md#order-pacing)). After the tick's orders the engine executes the
  forced resume of a [pause limit](turn-wire.md#pause-limit), if one is due.
- **Pacing.** A turn game's tick duration is `tickIntervalMicros()`, rounded to
  milliseconds (38–42 ms around 40), even while paused, because the relay's clock keeps
  going. The pacing budget advances only when a tick ran, so frames spent waiting for a
  bundle poll every millisecond instead of sleeping a whole tick. Headless turn clients
  are paced too; only `sessionDelay()`'s caller decides whether to wait.
- **After a stall.** When a tick runs after waiting for a bundle, the schedule moves
  back by the wait, up to one tick, instead of running the owed ticks back to back. A
  bundle a few milliseconds late then costs a hitch of that length once and becomes a
  little more buffer, which the rate control drains; a longer wait still catches up
  the rest at once.
- **Catch-up.** While `tickIntervalMicros()` is 0, the loop uses the replay fast-forward
  preset (`REPLAY_FAST_FORWARD_MS`, drawing one frame in
  `REPLAY_FAST_FORWARD_DRAW_RATIO`) and lifts the `MAX_CATCHUP_MS` cap. The screen host
  (`GameSessionScreen`, which the browser and every online match use) runs as many
  ticks per frame as fit in 30 ms while `Engine::turnFastForwarding()`, so the replay
  is not capped at the frame rate. The catching-up card estimates the time left from
  the rate at which the gap to the relay closes (replay rate minus match rate,
  `CatchUpPace`); when the gap has not shrunk for 15 s it says the device cannot keep
  up instead of showing a growing estimate. The card always offers Leave match.
- **Reload.** When `needsReload()` is set (told to rejoin, or a resume the relay could
  serve only from tick 0), the engine reloads the initial state in place from the same
  map and `GameHeader`, restarts its replay and checksum sidecar, and calls
  `reloadDone()`. The session then replays the turn log in catch-up mode.
- **Desync.** `matchCheckSums()` never fails for a turn game, so the single-player
  dump-and-assert path is not taken. A divergence reaches the engine as a reload
  request, and a flagged match (`desyncFlagged()`) is logged once; the verifier then
  decides the result. A refused client (`Rejected`) leaves the game.
- **Leaving.** Tearing the session down (`finishSessionForHost`, `abortSession`) calls
  `quit()`, with `GameFinished` once the game is decided (the end condition fired, or
  the local colony won) and `PlayerQuit` otherwise, including for a colony that lost
  while others play on. The relay connection stays open after the session is gone until
  the `Quit` is written (at most 3 s; the shutdown screen waits for it), so closing the
  window still tells the relay the seat left. The in-game Quit menu and the end-of-game
  dialog's Quit queue the usual `PlayerQuitsGameOrder`; in a turn match the engine
  sends `Quit` in its place (with `GameFinished` or `PlayerQuit` as described for session teardown), and the relay sequences the
  same quit order and marks the seat left. Submitting the order itself would leave
  the seat before the `Quit` could say the game was decided, and every finished
  match would be reported as abandoned.

As in a legacy network game, executing the local seat's own `PlayerQuitsGameOrder`
stops that client's loop.

`TurnSession` and `TurnSequencer` also measure the connection (round trips, jitter,
buffer depth, input delay, stalls, catch-up, reconnects, traffic, arbitration) without
changing the protocol or the record: see [network telemetry](../development/network-telemetry.md).


## Match setup and simulation version

### MatchSetup to GameHeader

`src/online/MatchSetup.{h,cpp}` turns the platform's MatchSetup JSON
(`platform/packages/protocol`, `src/matchSetup.ts`, is the source of truth) into the
`GameHeader` that every client and the verifier run:

1. `MatchSetup::parse` checks the JSON Schema rules (every field required, no unknown
   properties, ranges, patterns, the closed AI list without `javascript`), then the
   cross-field rules: teams listed `0..n-1` in order, seats numbered `0..k-1`, each seat
   on a listed team, names at most 32 UTF-8 bytes, one seat per account, at least one
   human or AI seat, closed seats after every human and AI seat and each on a different
   team that no human or AI seat plays, a generator's `teams` equal to the number of
   teams, and only known experiment keys. Errors carry the stage (`Schema`, `Semantic`
   or `Map`) and a JSON pointer.
2. `resolveMatchMap` finds the map: a given file, or `<cache>/<hash>.map[.gz]` or
   `.game[.gz]`. The file's decompressed bytes must hash (SHA-256) to `map.hash`, and it
   must be a saved game exactly when the source is an uploaded save.
3. `toGameHeader(mapHeader)` requires the map's team count to equal `teams.length`.
   Human or AI seat `s` becomes player record `s` on its team. **Every human seat is `P_IP` on every
   client and in the verifier**, so the heavy checksum the engine enables when a
   network player exists is the same everywhere; the local seat is chosen by
   `localPlayer`, never by the player type. AI seats use the `AINames` CLI ids (`none`
   is `AI::NONE`) and their `aiConfig`. Each team's ally-team number is
   `alliance + 1`. The rules set the `GameHeader` setters of the same names, starting
   from the default winning conditions with prestige and the sudden-death timer
   (`minutes × 60 × GAME_TICKS_PER_SECOND` ticks, with 30 ticks/s by default) toggled.

**Seats, players and teams.** A human or AI seat is a player: seat `s` is
`BasePlayer` `s`, and that number is what tickets (`seat`, `humanSeats`), the relay,
`TurnSession`'s local seat, the match record, `match verify`, the order audit and
`match_participants.seat` use. A seat's `team` is the map team it controls. Team
indices are never renumbered: `result.json`, `match_team_stats` and
`match_participants.team` use the map's own numbering.

**Closed teams.** A team that no human or AI seat controls is closed, exactly like a
"Closed" colony in a custom game (`CustomGameSetup::writeHeader` gives it no player):
the engine removes its colony at the start (`Game::clearingUncontrolledTeams`), and a
team without players dies on its first step (`TeamStep`: `playersMask == 0`), so it has
lost and never stands in the way of the opponents-defeated victory. A `closed` seat
(`{seat, kind: "closed", team}`) says so explicitly. Closed seats are not players and
create no `BasePlayer`; they are numbered after every human and AI seat so players
keep the numbers `0..p-1`. Rooms send each empty or locked room seat this way
(`roomMatchSeats` in `platform/apps/api/src/play/rooms.ts`): the taken room seats become
match seats `0..p-1` in room seat order on their own map teams, and the empty ones
follow as closed seats. A match seat therefore equals its room seat only while no empty
room seat comes before it. LAN rooms list no seat at all for a team nobody took, which
means the same. AI `none` is different: an idle player whose colony stays on the map,
alive. Rooms used to send empty seats that way, which kept a player who had beaten
every real opponent from ever winning; records of those matches still verify as they
were played.

`MatchSetup::fromGameHeader` is the inverse where it is meaningful, for a LAN host or
an uploaded save: it rejects JavaScript AIs and winning-condition lists other than the
standard one.

**Saves.** For an uploaded save, the seats replace every saved player record
(`Game::setGameHeader` with `saveAI = false`). A seat takes control of its team as
saved; naming any saved team is how reteaming works. AI seats start fresh AIs of the
given kind, and teams no human or AI seat controls (closed teams) are cleared as on a
new map. The rules, seed and
experiments come from the setup like any other match, so a platform that wants to
continue a save unchanged builds the setup with `fromGameHeader` from the save's
header. Saved unit, building, map-operation and story RNG streams retain their
progress even when a replacement setup changes the seed. New entities use the
replacement seed. AI controllers are newly created for the replacement seats and
use their own streams derived from that seed.

### Shared scripted generator sources

Schema 1 also accepts `map.kind = "scripted"`. It carries the resulting ordinary map's
`hash`, the worker's `chosenSeed`, and a separate `ScriptGeneratorDescriptor`: immutable
`libraryId`/`versionId`, `fileHash`/`packageHash`, namespaced `generatorId`/`revision`,
requested `seed`, complete `params`, `candidates`, and `startingUnitLevel = 0`.
The native generator descriptor remains unchanged. The scripted descriptor's `teams`
must match the setup teams. Clients load the resulting map by hash; joining a match
never executes or requires installing the package.

Clients advertise `client.generatorSharing = true` in `hello`. Creating, joining or
reconnecting to a scripted room or match requires this support; older clients
receive an update-required response before a scripted contract is delivered. Hosts pin an
exact release, and the platform checks visibility, moderation, playable status and
validation for the room's exact simulation version on selection and before starting.
Changing settings clears readiness and starts or reuses generation for the complete
request. Pending results apply only to the currently selected generation job.

Generated maps and previews use existing room/match access checks, including cache
hits. Match history retains the release descriptor and chosen seed, and blob cleanup
retains packages referenced by match setups after catalogue deletion. See the
[JavaScript generator guide](../map-generators/javascript.md) for publication and
validation coverage.

### Simulation version

A sim version identifies builds that produce identical games. Its JSON form is
`{versionMinor, netProtocol, dataHash}` (`SimVersion` in the protocol package) and its
key is `<versionMinor>-<netProtocol>-<dataHash>`, the string relays copy into the match
record. `glob2 info sim-version --format json` prints the JSON.

- `versionMinor` is `VERSION_MINOR` and `netProtocol` is `NET_PROTOCOL_VERSION` in
  `src/app/Version.h`.
- `dataHash` is the lowercase hex SHA-256 of `SIM_REVISION` (`src/game/SimRevision.h`)
  followed by the simulation data files. The revision is hashed first as a pseudo-file
  with path `#sim-revision` and the revision in decimal ASCII as its content. The data
  files are those listed in `Online::simDataFiles()`: the Maxima strategies (`data/maxima/*.strategy`), the
  Nicowar tables (`data/nicowar.default.txt`, `data/nicowar.txt`), the default resource
  registry (`data/resources/registry.json`), the default building
  manifest (`data/buildings/manifest.json`) and every definition it references, and the USL runtime
  (`data/usl/*/Runtime/*.usl`), in byte-wise sorted path order. For each file the hash
  takes the path bytes, one zero byte, the content length as a big-endian 64-bit number
  and the content, with every CR LF pair replaced by LF so a Windows checkout with
  automatic line-ending conversion hashes the same. A missing file contributes its path,
  a zero byte and the length `0xFFFFFFFFFFFFFFFF`. Files are read through the engine's
  file manager, so the browser's packaged file system gives the same value.

A unit test checks that the list covers every file in those directories.
`deploy/sim_version.py` computes the same key from a source tree (engine-agent images
are labelled with it).

MatchSetup schema 1 additionally accepts `buildingCatalog: {snapshot, hash}`.
The snapshot is canonical catalog JSON (at most 8 MiB of UTF-8), and `hash` is its
SHA-256. Maps, saves, LAN rooms, online rooms, reconnects, and match records retain
that snapshot. A concrete map's catalog is authoritative: a setup with a different
catalog is rejected. Embedded experiment declarations validate saved keys without
changing the process-wide experiments UI registry. Older schema-1 setups omit the
field and use the legacy map catalog.

Engine jobs, relay tickets and client compatibility still use the executable's
sim version. AI ratings use a separate `matches.rules_identity`: the original sim
version with dataHash replaced by SHA-256 of
`glob2-building-rules-v1\n<engine-version-key>\n<catalog-hash>` (no trailing newline).
Absent catalog metadata keeps the historical identity. This separates results for
different building rules without requiring a separate verifier executable for each
catalog. Map validation/generation records the catalog metadata; current engine-agent
heartbeats advertise their default catalog hash for default AI ladder selection.
Native online text messages and match setup records accept up to 32 MiB to leave
room for JSON escaping of an 8 MiB embedded snapshot; binary turn limits are unchanged.

**Bump `SIM_REVISION` with every simulation change.** Everything else the simulation
depends on is compiled in, and `VERSION_MINOR` tracks the save format, so nothing else
moves the sim version when simulation code changes: rules, units, buildings,
pathfinding, AI code and parameters, order validation, map loading, random number use,
scripting. Without a bump, builds that simulate differently share rooms, queues, AI
ratings and verifiers, and their matches desync or fail verification. The revision
only ever increases. A bump also needs a fresh golden record
(`test/fixtures/multiplayer/FourSquares1.g2mr`, which carries the sim version) and
its trace: `python3 test/run_tests.py --update-fixtures --filter 'TurnEngineHarness/the committed*'`.

CI enforces what it can detect:

- `test/check_sim_revision.py` (the change-selection job) fails when the committed
  record names another sim version than the tree, and when the record or its
  verification trace changed relative to the base revision while the sim version
  did not.
- The browser/native equivalence job fails when Linux, Windows, macOS and the browsers agree
  on a `match verify` trace that differs from the committed one: the simulation
  changed.

The golden match covers only what one short Nicowar/Warrush game reaches, so a passing
check does not prove the simulation is unchanged; bump whenever a change can matter.

[Multiplayer index](README.md) · [Documentation index](../README.md).
