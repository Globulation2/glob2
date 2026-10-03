# Turn protocol: relay-sequenced lockstep

This document describes the binary protocol between Glob2 clients and a turn relay. It
also covers the timing model, the per-client jitter buffer, reconnect and desync
handling, and the match record format. The implementation lives in `src/net/turn/`.
It is transport-agnostic and takes time as an argument, so it can be tested without
sockets. Keep this document in step with that code.

| Component | File | Role |
| --- | --- | --- |
| Constants and limits | `TurnProtocol.h` | Protocol version, message ids, timing defaults, size limits |
| Codecs | `TurnMessages.h/.cpp` | One `NetMessage` subclass per message, strict decoding |
| Relay core | `TurnSequencer.h/.cpp` | Clock, tick assignment, bundles, turn log, presence, grace, checksum arbitration |
| Delay maths | `JitterBuffer.h/.cpp` | Jitter estimate, target buffer with hysteresis, tick-rate nudge, catch-up |
| Client | `TurnSession.h/.cpp` | Lockstep session over an abstract transport |
| Record | `MatchRecord.h/.cpp` | Versioned file of setup, map hash, turns and checksum reports |
| Engine adapter | `TurnLockstep.h/.cpp` | `TurnSession` behind `LockstepSession`; `RecordTransport` replays a record |
| Match setup | `src/online/MatchSetup.*`, `SimVersion.*` | MatchSetup JSON to `GameHeader`; the simulation version |

The relay never simulates the game, so it does not know the rules. It treats orders as
opaque bytes. The only exceptions are the few order type ids in the
[order handling](#order-handling) section.

## Model

- The relay owns the clock. Tick `t` starts `t × 40 ms` after the match starts
  (25 ticks/s, or `tickRateMilliHz / 1000` ticks per second).
- Each human order gets an explicit **execution tick** from the relay when it arrives.
  That tick is normally the one after the current relay tick. Each seat gets at most one
  order per tick, and later orders from that seat queue onto later ticks.
- Every `bundleInterval` ticks (2), the relay broadcasts a `TurnBundle` covering
  `[fromTick, horizonTick)`, even when it is empty. The bundle authorizes clients to
  execute every tick below `horizonTick`. A seat with no entry at a tick executes a
  `NullOrder` there.
- AI seats are unchanged: every client computes AI orders locally each tick.
- All clients execute the same `(tick, seat, order)` triples. Determinism therefore
  does not depend on network timing, and a slow client delays only itself.

Ticks are counted as executions. Tick `t` is the `(t+1)`-th call to the engine's
execute step: `retrieveOrder` for each seat, then `clearTopOrders`. Tick 0 is the first
tick of the match. The state checksum for tick `t` is the one the engine passes to
`advanceStep` just before executing tick `t`, after `t` ticks have run. Pause orders
pause the simulation in the usual way, and ticks keep counting while it is paused.

## Framing and versioning

Each message is a frame payload: one message-type byte followed by the body. The
payload is the content of one `NetConnection` frame, so TCP (LAN) and WSS connections
reuse the existing 2-byte big-endian length prefix and the 65,535-byte frame limit
unchanged. All integers are big-endian, as in `GAGCore::BinaryOutputStream`.
`bytes16` is a `u16` length followed by that many bytes. `text32` is the
`BinaryOutputStream::writeText` encoding: a `u32` length followed by the bytes.

Turn messages use the reserved message-type range `0xA0`–`0xBF`, which is assigned
explicitly in `NetMessageType.h`. Legacy YOG message ids are numbered implicitly and
can never reach this range, so deleting YOG messages at cutover does not renumber the
turn protocol. `NetMessage::getNetMessage` decodes turn messages too, so a
`NetConnection` can carry them. The relay and `TurnSession` use the narrower
`TurnCodec::decode`, which accepts only turn messages.

Decoding fails closed. The codec rejects truncated input, trailing bytes, unknown
types, oversized fields and violated invariants, and a peer that sends any of these is
disconnected. The limits come from `TurnProtocol.h`:

| Limit | Value | Reason |
| --- | --- | --- |
| Frame payload | 65,535 bytes | `NetConnection` length prefix |
| Order bytes | 1–4,096 | The largest real order is `OrderVoiceData` (≤ 2,054 bytes) |
| Ticket | ≤ 8,192 bytes | A signed ticket token |
| Order bytes per tick | 30,000 | A bundle of two ticks always fits one frame |
| Seats | 0–31 | The engine's waiting mask is 32 bits |

`TURN_PROTOCOL_VERSION` (currently 1) is carried in `Hello` and `Welcome`. The relay
rejects a client whose protocol version differs. The relay does not compare simulation
versions: that comparison happens on the platform, which issues tickets only for one
sim version per match. A protocol change that alters any encoding bumps
`TURN_PROTOCOL_VERSION`. A change that keeps the encodings but alters relay behaviour
that clients depend on also bumps it.

## Messages

C→R is client to relay, R→C is relay to client.

| Id | Name | Dir | Body |
| --- | --- | --- | --- |
| `0xA0` | `Hello` | C→R | `u16 protocolVersion`, `text32 ticket` (≤ 8 KiB), `u32 haveHorizon` |
| `0xA1` | `Welcome` | R→C | `u16 protocolVersion`, `u8 seat`, `u32 humanSeatMask`, `u32 tickRateMilliHz`, `u8 bundleInterval`, `u16 checksumInterval`, `u32 relayTick`, `u32 resumeFromTick`, `u32 graceTicks`, `u32 lastClientSequence` |
| `0xA2` | `Reject` | R→C | `u8 reason`, `text32 detail` (≤ 1 KiB) |
| `0xA3` | `OrderSubmit` | C→R | `u32 clientSequence`, `bytes16 order` |
| `0xA4` | `TurnBundle` | R→C | `u32 fromTick`, `u32 horizonTick`, `u16 count`, `count × (u32 tick, u8 seat, bytes16 order)` |
| `0xA5` | `ChecksumReport` | C→R | `u32 tick`, `u32 checksum` |
| `0xA6` | `Presence` | R→C | `u8 count`, `count × (u8 seat, u8 state, u32 graceRemainingTicks, u32 lagTicks)` |
| `0xA7` | `ResyncRequest` | C→R | `u32 fromTick` |
| `0xA8` | `DesyncNotice` | R→C | `u32 tick`, `u8 verdict`, `u32 divergedSeatMask` |
| `0xA9` | `Quit` | C→R | `u8 reason` |
| `0xAA` | `Ping` | C→R | `u32 nonce`, `u32 executedTick` |
| `0xAB` | `Pong` | R→C | `u32 nonce`, `u32 relayTick`, `u32 lastClientSequence` |

Field rules that the decoder enforces:

- `TurnBundle`: `fromTick ≤ horizonTick`. Entries are strictly ordered by
  `(tick, seat)`, so each `(tick, seat)` pair appears at most once. Every entry
  satisfies `fromTick ≤ tick < horizonTick` and `seat < 32`, and every order is 1–4,096
  bytes.
- `Presence`: seats are below 32, unique and ascending; the state is a known value.
- `Welcome`: `seat < 32`, the seat is in `humanSeatMask`, `bundleInterval ≥ 1`,
  `checksumInterval ≥ 1` and `tickRateMilliHz > 0`.
- `Reject.reason`, `DesyncNotice.verdict` and `Quit.reason` must be known values.

Enumerations:

- **Reject reason:** 1 protocol version, 2 bad ticket, 3 seat already connected
  (reserved; the relay currently replaces the old connection instead), 4 seat has
  left, 5 match over, 6 malformed message, 7 flooding.
- **Presence state:** 0 not yet connected, 1 connected, 2 lagging, 3 reconnecting,
  4 resyncing, 5 left.
- **Desync verdict:** 1 rejoin (this client is in the minority and must reload),
  2 flagged (no majority; the match continues and the verifier decides).
- **Quit reason:** 0 player quit, 1 game finished.

## Session flow

1. The client opens the transport and sends `Hello` with its ticket. `haveHorizon` is
   0 on a fresh start. After a transport loss without a restart, it is the horizon the
   client already holds.
2. The relay calls its admission function (ticket verification is the relay
   workstream's job) to get a seat. It refuses with `Reject` and closes when the ticket
   is bad, the protocol version differs, the seat has left, or the match is over. A
   valid `Hello` for a seat that still has a connection replaces that connection: the
   relay often learns that a socket is dead only after the client has reconnected.
3. The relay replies with `Welcome`, then a `Presence` snapshot. It then sends
   `TurnBundle` chunks from `resumeFromTick` up to its current horizon. These cover the
   turn log without voice orders. `resumeFromTick` equals `haveHorizon` when the
   relay can resume from there, which is any value up to its current horizon. It is
   0 otherwise, and then the client must reload the initial state and fast-forward.
4. From then on the relay streams a live `TurnBundle` every `bundleInterval` ticks. The
   client sends `OrderSubmit` for each local order, `ChecksumReport` every
   `checksumInterval` ticks and `Ping` every 500 ms.
5. When the player quits, the client sends `Quit`, or a `PlayerQuitsGameOrder` for its
   own seat. The relay sequences the quit order, marks the seat left and closes the
   connection.

`OrderSubmit.clientSequence` numbers a client's orders from 1. The relay remembers the
highest sequence it accepted from each seat and ignores anything at or below it, so a
resubmission is harmless. It reports that value in `Welcome` and `Pong`. The client keeps
every order the relay has not yet confirmed, drops the confirmed ones, and resends the
rest after each `Welcome`. An order sent just before a connection died is therefore
neither lost nor executed twice. A restarted client continues numbering after the
`Welcome` value.

Every bundle's `fromTick` must equal the client's current horizon. A gap means a bug or
a lost stream, so the client then asks for `ResyncRequest(currentHorizon)`.

## Order handling

The relay looks at the first byte of each order, which is the order type
(`src/net/NetConsts.h`):

- `ORDER_NULL` (51) and `ORDER_ADJUST_LATENCY` (100) are dropped. Null orders are
  implied by an absent entry. Latency adjustment belongs to the old global-lockstep
  pipeline and has no meaning here.
- `ORDER_PLAYER_QUIT_GAME` (67) is accepted only when its 4-byte player field equals
  the sender's seat. It is sequenced, and then the seat is marked left.
- `ORDER_VOICE_DATA` (72) is sequenced and broadcast like any order, but it is left out
  of the turn log, resync and the match record. Voice does not change simulation state,
  and the replay writer already leaves it out.

Each entry's seat is the sender's authenticated seat, never a value from the client.
The engine sets `order->sender` from the bundle seat. The relay does not validate the
rest of an order. Orders that act for another team are an engine-level concern, as
they are today.

## Tick assignment

When an order arrives at relay tick `R`, the relay assigns it to tick `t`, the smallest
value that satisfies all of these:

- `t ≥ R + 1`, so the order lands after the tick in progress;
- `t ≥ sentHorizon`, so it never lands in a tick already authorized;
- `t ≥ nextFreeTick[seat]`, so a seat gets at most one order per tick;
- `bytes[t] + size ≤ 30,000`.

Then `nextFreeTick[seat] = t + 1`. A seat whose queue reaches more than 250 ticks
(10 s) ahead of `R` is flooding: its order is dropped and the connection closed with
`Reject(7)`. A client that keeps to the engine's rate of one order per tick never gets
near this limit.

### Bundles

`update(now)` computes `R = floor(elapsed × tickRate)`. Every tick up to `R` is closed,
because new orders always land at `R + 1` or later. When `R + 1 − sentHorizon ≥
bundleInterval`, the relay emits a bundle `[sentHorizon, R + 1)` with every pending
entry below `R + 1`, and sets `sentHorizon = R + 1`. A bundle is split at tick
boundaries when it would exceed 60,000 bytes. The per-tick byte budget means a single
tick always fits.

## Timing model and per-client delay

Let `P` be the tick period (40 ms) and `B` the bundle interval (2). The relay emits
horizon `H` at roughly time `(H − 1) × P`, measured from match start.

On each live bundle arrival at local time `a`, the client records the offset
`o = a − H × P`. Bundles that replay the log after a `Welcome` or a resync are not
live: a bundle counts only if its horizon is above both `Welcome.relayTick + 1` and
every horizon seen before it. The offset equals a constant (one-way latency plus the
unknown clock offset) plus the jitter on that path. `JitterEstimator` keeps the last 128 offsets
(about 10 s) and reports:

- `jitter = p95(o) − min(o)`, the 95th-percentile delay above the fastest delivery in
  the window. Using a minimum within the window, rather than over all time, absorbs
  clock drift over a long match.

`JitterBuffer` turns jitter into a target buffer level, measured in ticks of
authorized-but-unexecuted work (`horizon − executedTick`):

```
required = ceil(jitter / P) + ceil(B / 2) + safetyTicks      (safetyTicks = 1)
required = clamp(required, minTarget = 2, maxTarget = 50)
```

The `B / 2` term is the mean of the sawtooth the bundle interval creates: the buffer
level jumps by `B` on every arrival and drains by one each tick. Hysteresis keeps the
target from oscillating:

- **Up:** when `required > target`, the target rises to `required` at once. Stalls are
  worse than a little extra delay.
- **Down:** when `required < target` has held continuously for 5 s, the target falls
  by one tick, and the hold timer restarts. The target therefore drains back to
  baseline at no more than one tick (40 ms) per 5 s once jitter subsides. Any update
  in which `required ≥ target` cancels the hold.

`DelayController` holds the buffer at the target by nudging the client's tick rate. It
keeps an exponential moving average of the buffer level, sampled once per executed
tick with α = 0.05 (a time constant of about 20 ticks, or 0.8 s):

```
error      = ema − target
error      = 0                                 if |error| ≤ 0.5   (deadband)
multiplier = 1 + clamp(0.02 × error, −0.05, +0.05)
interval   = P / multiplier
```

A client with too much buffered runs up to 5% fast, and one with too little runs up to
5% slow. Over 10 s that moves the buffer by up to 12.5 ticks, which absorbs drift and
new jitter without a visible change in game speed.

**Catch-up.** When the instantaneous buffer exceeds `target + 25` (one second behind),
the controller enters catch-up mode. The engine then runs ticks at uncapped speed with
rendering skipped, reusing the replay fast-forward path (`REPLAY_FAST_FORWARD_MS`) with
the `MAX_CATCHUP_MS` cap lifted. Catch-up ends when the buffer drops to `target + 2`, and
the moving average is reset to the current level. A reconnect or resync from tick 0
always starts in catch-up.

The input delay a player feels is therefore their own round trip, plus about one tick
of assignment, plus up to `B` ticks of bundle wait, plus their own buffer. It no longer
depends on the worst connection in the match.

## Presence, reconnect and grace

The relay keeps a presence state per human seat and broadcasts a full `Presence`
snapshot when any state changes, and at least every 25 ticks. `lagTicks` is
`R − executedTick` from the seat's last `Ping`. A connected seat whose lag exceeds 50
ticks (2 s) is shown as lagging.

- A seat starts as not yet connected. When its transport closes, it becomes
  reconnecting.
- In either state, a grace timer (default 3 min, `graceTicks` in `Welcome`) runs from
  match start or from the disconnect. If the seat reconnects in time, it is connected
  again. If the timer expires first, the relay sequences a `PlayerQuitsGameOrder` for
  the seat through the normal assignment path and marks the seat left.
- A left seat cannot reconnect: its `Hello` is refused with `Reject(4)`.
- The match is over when every human seat has left. The relay then closes the match and
  produces the match record.

Other clients keep running throughout. The absent seat simply sends no orders, so its
colony keeps acting on its own.

The client side handles a transport loss by retrying with exponential backoff (250 ms
doubling to a 5 s cap). It then sends `Hello` with `haveHorizon` set to its current
horizon and resumes from there. A client that restarted, or that was told to rejoin,
has no state: it reloads the initial game state, sends `haveHorizon = 0`, receives the
full turn log and fast-forwards in catch-up mode. While starved, the engine's waiting
mask shows the seats the relay reports as not connected, lagging or resyncing, or the
local seat when no other seat explains the wait. The "waiting for players" notice can
then show "reconnecting" instead of stalling silently.

## Desync arbitration

Each client sends `ChecksumReport(t, checksum)` for every tick `t` with
`t % checksumInterval == 0` (every 25 ticks, once per second). The reports travel out
of band and never block execution. For each tick, the relay arbitrates once every seat
that is not left, reconnecting or resyncing has reported. If some reports are still
missing 250 ticks after tick `t`, it arbitrates with what it has.

| Reports | Outcome |
| --- | --- |
| 0 or 1 | No verdict. A single report becomes the reference. |
| All equal | Agreed. |
| 2, differing | `DesyncNotice(flagged)` to both. The match continues and the record is flagged; the verifier decides. |
| ≥ 3 with a strict majority | The majority value is agreed. Each minority seat gets `DesyncNotice(rejoin)` and moves to resyncing. |
| ≥ 3 without a strict majority | `DesyncNotice(flagged)` to all reporters. |

A resyncing seat's reports are ignored until it sends `ResyncRequest(0)`, which means it
has reloaded. The relay then sends the full turn log, the client fast-forwards and
reports checksums for the past ticks again. Each late report is compared with the
agreed value. If an agreement supported by at least two seats differs from it, the seat
is told to rejoin again. After three rejoins the seat is no longer told to rejoin; the
match is flagged instead. Late reports never create a new verdict for ticks already
arbitrated.

## Engine integration

`TurnLockstepSession` (`TurnLockstep.h/.cpp`) puts a `TurnSession` behind the engine's
`LockstepSession` interface. The engine calls it exactly where it calls `NetEngine` for
single player and the legacy games:

| Engine call | `TurnSession` behaviour |
| --- | --- |
| `addLocalOrder` | Encodes and submits a human order; null and latency orders are ignored |
| `pushOrder(order, p, isAI)` | Queues a locally computed AI order, as today |
| `advanceStep(checksum)` | Sends `ChecksumReport` when the executed tick is a multiple of `checksumInterval` |
| `tickReady` (`allOrdersReceived`) | The next tick is below the horizon and every AI seat has its order |
| `orderReceived(p)` | AI seat: its queue is not empty. Human seat: the next tick is authorized |
| `retrieveOrder(p)` | The bundled order for that seat and tick, or a `NullOrder`; `sender` is set to `p` |
| `clearTopOrders` | Drops the tick's orders and advances the executed tick |
| `getWaitingOnMask` | See [presence](#presence-reconnect-and-grace) |
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
[match setup](#match-setup-and-simulation-version)), loads the map with the saved
GUI data ignored, and installs the session in place of the `NetEngine`. The caller
keeps its own reference to the transport and closes it after the session has ended,
so frames queued by `quit()` can still be delivered. `Engine::turnSession()` exposes
the session (presence, latency, buffer) for a connection HUD.

### The engine loop

`Engine::stepSession` calls `pumpTurnSession` before gathering orders:

- `TurnSession::update(now)` pumps the transport and timers every frame.
- **Pacing.** A turn game's tick duration is `tickIntervalMicros()`, rounded to
  milliseconds (38–42 ms around 40), even while paused, because the relay's clock keeps
  going. The pacing budget advances only when a tick ran, so frames spent waiting for a
  bundle poll every millisecond instead of sleeping a whole tick. Headless turn clients
  are paced too; only `sessionDelay()`'s caller decides whether to wait.
- **Catch-up.** While `tickIntervalMicros()` is 0, the loop uses the replay fast-forward
  preset (`REPLAY_FAST_FORWARD_MS`, drawing one frame in
  `REPLAY_FAST_FORWARD_DRAW_RATIO`) and lifts the `MAX_CATCHUP_MS` cap.
- **Reload.** When `needsReload()` is set (told to rejoin, or a resume the relay could
  serve only from tick 0), the engine reloads the initial state in place from the same
  map and `GameHeader`, restarts its replay and checksum sidecar, and calls
  `reloadDone()`. The session then replays the turn log in catch-up mode.
- **Desync.** `matchCheckSums()` never fails for a turn game, so the single-player
  dump-and-assert path is not taken. A divergence reaches the engine as a reload
  request, and a flagged match (`desyncFlagged()`) is logged once; the verifier then
  decides the result. A refused client (`Rejected`) leaves the game.
- **Leaving.** Tearing the session down (`finishSessionForHost`, `abortSession`) calls
  `quit()`, with `GameFinished` once the game has ended for the local team and
  `PlayerQuit` otherwise. The in-game Quit menu also submits the usual
  `PlayerQuitsGameOrder`; the relay sequences it and marks the seat left.

As in a legacy network game, executing the local seat's own `PlayerQuitsGameOrder`
stops that client's loop.

## Match setup and simulation version

### MatchSetup to GameHeader

`src/online/MatchSetup.{h,cpp}` turns the platform's MatchSetup JSON
(`platform/packages/protocol`, `src/matchSetup.ts`, is the source of truth) into the
`GameHeader` that every client and the verifier run:

1. `MatchSetup::parse` checks the JSON Schema rules (every field required, no unknown
   properties, ranges, patterns, the closed AI list without `javascript`), then the
   cross-field rules: teams listed `0..n-1` in order, seats numbered `0..k-1`, each seat
   on a listed team, names at most 32 UTF-8 bytes, one seat per account, a generator's
   `teams` equal to the number of teams, and only known experiment keys. Errors carry
   the stage (`Schema`, `Semantic` or `Map`) and a JSON pointer.
2. `resolveMatchMap` finds the map: a given file, or `<cache>/<hash>.map[.gz]` or
   `.game[.gz]`. The file's decompressed bytes must hash (SHA-256) to `map.hash`, and it
   must be a saved game exactly when the source is an uploaded save.
3. `toGameHeader(mapHeader)` requires the map's team count to equal `teams.length`.
   Seat `s` becomes player record `s` on its team. **Every human seat is `P_IP` on every
   client and in the verifier**, so the heavy checksum the engine enables when a
   network player exists is the same everywhere; the local seat is chosen by
   `localPlayer`, never by the player type. AI seats use the `AINames` CLI ids (`none`
   is `AI::NONE`) and their `aiConfig`. Each team's ally-team number is
   `alliance + 1`. The rules set the `GameHeader` setters of the same names, starting
   from the default winning conditions with prestige and the sudden-death timer
   (`minutes × 60 × 25` ticks) toggled.

`MatchSetup::fromGameHeader` is the inverse where it is meaningful, for a LAN host or
an uploaded save: it rejects JavaScript AIs and winning-condition lists other than the
standard one.

**Saves.** For an uploaded save, the seats replace every saved player record
(`Game::setGameHeader` with `saveAI = false`). A seat takes control of its team as
saved; naming any saved team is how reteaming works. AI seats start fresh AIs of the
given kind, and teams no seat controls are cleared as on a new map. The rules, seed and
experiments come from the setup like any other match, so a platform that wants to
continue a save unchanged builds the setup with `fromGameHeader` from the save's
header. If the seed equals the saved one, the saved random state is kept; otherwise
the simulation is reseeded.

### Simulation version

A sim version identifies builds that produce identical games. Its JSON form is
`{versionMinor, netProtocol, dataHash}` (`SimVersion` in the protocol package) and its
key is `<versionMinor>-<netProtocol>-<dataHash>`, the string relays copy into the match
record. `glob2 --sim-version` prints the JSON.

- `versionMinor` is `VERSION_MINOR` and `netProtocol` is `NET_PROTOCOL_VERSION` in
  `src/Version.h`.
- `dataHash` is the lowercase hex SHA-256 of the simulation data files listed in
  `Online::simDataFiles()`: the Maxima strategies (`data/maxima/*.strategy`), the
  Nicowar tables (`data/nicowar.default.txt`, `data/nicowar.txt`) and the USL runtime
  (`data/usl/*/Runtime/*.usl`), in byte-wise sorted path order. For each file the hash
  takes the path bytes, one zero byte, the content length as a big-endian 64-bit number
  and the content, with every CR LF pair replaced by LF so a Windows checkout with
  automatic line-ending conversion hashes the same. A missing file contributes its path,
  a zero byte and the length `0xFFFFFFFFFFFFFFFF`. Files are read through the engine's
  file manager, so the browser's packaged file system gives the same value.

A unit test checks that the list covers every file in those directories. Everything
else the simulation depends on is compiled in: a change to simulation code must bump
`VERSION_MINOR` or `NET_PROTOCOL_VERSION` to change the sim version.

## Match record

`MatchRecord` is the relay's persistent output, consumed by `--verify-match`. Its
encoding is self-contained and big-endian:

```
magic            "G2MR" (4 bytes)
formatVersion    u16   (currently 1)
flags            u32   bit 0: desync flagged; bit 1: incomplete (relay shut down early)
matchId          text32 (≤ 256 bytes)
simVersion       text32 (≤ 256 bytes; opaque, from the ticket)
tickRateMilliHz  u32
bundleInterval   u8
checksumInterval u16
humanSeatMask    u32
endTick          u32   the final horizon
setupJson        text32 (≤ 4 MiB; MatchSetup JSON, opaque to C++ here)
mapHash          32 bytes (SHA-256 of the decompressed map bytes)
turnCount        u32,  turnCount × (u32 tick, u8 seat, bytes16 order)   sorted by (tick, seat)
reportCount      u32,  reportCount × (u32 tick, u8 seat, u32 checksum)   sorted by (tick, seat)
eventCount       u32,  eventCount × (u32 tick, u8 seat, u8 kind)        sorted by tick
crc32            u32   CRC-32 (IEEE) of every preceding byte
```

- **Turns** are the sequenced orders without voice. The quit orders the relay creates
  appear here as ordinary `PlayerQuitsGameOrder` entries.
- **Reports** are the first live checksum report from each seat for each tick. Re-reports
  after a resync are not recorded, so the verifier sees which client diverged first.
- **Events** are presence transitions for the match page and diagnostics. The kinds are
  1 connected, 2 disconnected, 3 left by quit, 4 left by grace expiry, 5 told to
  rejoin, 6 resynced and 7 flagged.

Readers reject unknown magic, a newer `formatVersion`, a bad CRC, trailing bytes and any
violated ordering or limit. A future version may add fields, but only behind a version
check, so older records stay readable.

`glob2 --verify-match` replays a record through the same engine path a live client
runs: `Engine::initTurnMatch` with a `RecordTransport` that serves the record's turns as
one relay would. Its contract is in
[headless replays](../development/headless-replays.md#verifying-a-match-record).

## Testing

The unit tests in `test/TurnProtocolTest.cpp` (in `glob2-unit-tests`) use a fake clock
and drive the components directly:

- codec round trips, and rejection of malformed or oversized input;
- sequencer ordering, one order per seat per tick, the byte budget, flooding, grace and
  quit, majority arbitration and the two-client flag;
- jitter estimation and a target that rises and then falls back;
- match record round trip and corruption detection.

`test/TurnHarnessTest.cpp` connects 2–4 `TurnSession` clients to a `TurnSequencer` over
a simulated network with per-link latency, jitter, loss (as TCP-style retransmission
delay) and disconnects. It checks that every client executes the same
`(tick, seat, order)` sequence as the relay's log. It covers a mid-game transport drop
with incremental resume, a client restart with a full reload, and a desync that the
majority repairs. It also checks that a stalled client never stalls the others, that
each client's buffer follows its own link's jitter, and that each player's input delay
follows their own connection. Summaries are written under `artifacts/tests/`.

`test/TurnEngineHarness.cpp` (in `glob2-engine-tests`) runs the same network with 2–4
real engines started by `Engine::initTurnMatch` on one MatchSetup, AI seats computed on
every client and each human seat driven by a bot that queues orders through the GUI's
order queue. Every client records the checksum before each tick, and the cases require
all of them to agree at every tick (and with the relay's agreed checksums):

- two engines with AI seats under latency, jitter and loss;
- four engines, one stalled for 5 s (incremental resume) and one restarted as a new
  process (full log, fast-forward from tick 0);
- three engines where one executes a tampered order: the majority tells it to rejoin,
  it reloads in place and fast-forwards, and `--verify-match` names its seat;
- a player who quits through the sequenced `PlayerQuitsGameOrder` while the others
  play on.

Each case verifies the relay's record with `--verify-match` and requires the verifier's
per-tick checksums and `result.json` team outcomes to equal the live clients'. Forged
turns make the record unverifiable, and a seat whose reports disagree is named. A
`[benchmark]` case measures rejoin fast-forward time against game length and AI count
(`python3 test/run_tests.py --tag benchmark --filter 'TurnEngineHarness/*'`).

`test/MatchSetupTest.cpp` runs every MatchSetup and SimVersion contract fixture: valid
ones must parse and round-trip, invalid ones must fail at the stage the manifest names.
It reads `platform/packages/protocol/fixtures` when that directory exists; until the
platform workspace is on the branch it reads the copy in `test/fixtures/protocol`, and
once both exist it requires the copy to equal the source.

`test/fixtures/multiplayer/FourSquares1.g2mr` is a short recorded match (two humans,
Nicowar and Warrush) with its expected verification trace. The browser/native
simulation equivalence job verifies it on Linux, Windows and in three browsers and
requires identical traces (see
[headless replays](../development/headless-replays.md#verifying-a-match-record)). A
simulation change makes it stale; `python3 test/run_tests.py --update-fixtures --filter
'TurnEngineHarness/the committed*'` records a fresh match and trace.
