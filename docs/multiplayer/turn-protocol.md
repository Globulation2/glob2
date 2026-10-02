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
- **Load barrier.** Clients connect once they have loaded the game, so the match
  starts when every human seat has said `Hello`: until then the relay sends no bundle,
  answers `Welcome` with `relayTick` 0 and runs no grace. After
  `SequencerConfig::startBarrierMicros` (the online relay's
  `GLOB2_RELAY_LOAD_WAIT_SECONDS`, 60 s by default) the clock starts anyway; a seat
  still loading then shows as not connected and joins late under the reconnect grace,
  counted from the start. The LAN host gets the same barrier by creating its sequencer
  only after every `Hello` (`LanHost::Options::loadWaitMicros`). The barrier changes
  no message and nothing a client simulates.
- Each human order gets an explicit **execution tick** from the relay when it arrives:
  the earliest tick it has not yet broadcast. Each seat gets at most one order per tick,
  and later orders from that seat queue onto later ticks.
- Every `bundleInterval` ticks (1 by default), the relay broadcasts a `TurnBundle` covering
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

Clients do not depend on which tick the relay picks for an order (only that it is at
or above the horizon), on the bundle interval (it comes in `Welcome`), or on how they
size their own buffer. The latency changes described under
[timing](#timing-model-and-per-client-delay) therefore kept version 1: a client and a
relay from either side of them play together, with the older side's delay.

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
2. The relay calls its admission function to get a seat. In `glob2-relay` this is
   the ticket check described in [the relay section](#relay). It refuses with `Reject` and closes when the ticket
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
rest of an order; the engine does, as described next.

### Order validation in the engine

The relay passes order bytes through, so a modified client (or relay, or record) can
put any bytes in its seat's turns. Before the engine executes a human seat's order,
`TurnLockstepSession::retrieveOrder` checks it with `OrderValidation::validate`
(`src/OrderValidation.h`). The check reads only the game state at that point of that
tick and the seat from the bundle, never an identity the order claims, so every client
and `--verify-match` reach the same verdict. An order that fails, or whose bytes do not
decode, executes as a `NullOrder` everywhere; the game and every checksum stay the same
on all machines. AI orders, single player, replays and legacy network games are not
checked.

| Verdict | Meaning | Examples |
| --- | --- | --- |
| accepted | executed unchanged | anything the user interface sends |
| stale | it no longer applies; network latency can cause it | cancelling a deletion that is no longer pending |
| rejected | no unmodified client sends it | another team's buildings or alliances, another player's quit, a worker count above 20, an unplaceable building type, an off-map flag position, an unknown brush mode or message type, an `AdjustLatency`, undecodable bytes |

The rules per order type are in `OrderValidation.cpp`. Pause orders stay allowed for
every player, as in legacy games. Voice packets are checked (at most 128 frames) and
the mixer drops packets beyond about ten seconds of backlog per player.

The session counts verdicts per seat (`TurnLockstepSession::orderAudit()`: accepted,
stale, rejected, per-reason counts and the first rejected tick). The counts restart when
the engine reloads the initial state. `--verify-match` reports the same counts for the
record, so the platform can flag a seat that sent rejected orders. Voice is not in the
record, so rejected voice packets are counted separately and only by live clients.

Executors that a hostile order could stop with an assert or an out-of-range index
(`Game::executeCreate`, the area brushes, alliances, quits, building lookups,
`Building::cancelConstruction`, chat and map marks in `GameGUI`) now ignore such an
order instead. This only changes what invalid orders do, so legacy games and replays
of valid orders run as before.

## Tick assignment

When an order arrives, the relay assigns it to tick `t`, the smallest value that
satisfies all of these:

- `t ≥ sentHorizon`, so it never lands in a tick already authorized;
- `t ≥ nextFreeTick[seat]`, so a seat gets at most one order per tick;
- `bytes[t] + size ≤ 30,000`.

Then `nextFreeTick[seat] = t + 1`. The relay's own clock plays no part: no client can
run a tick before the relay broadcasts a horizon above it, so the first unbroadcast
tick is the earliest safe one, and it rides in the very next bundle. (The first
version also required `t ≥ R + 1`, where `R` is the relay tick in progress. With
bundles every tick that is the same tick; with longer intervals or a coarse relay
timer it cost up to `bundleInterval` ticks.) A seat whose queue reaches more than 250
ticks (10 s) ahead of `R` is flooding: its order is dropped and the connection closed
with `Reject(7)`. A client that keeps to the engine's rate of one order per tick never
gets near this limit.

### Bundles

`update(now)` computes `R = floor(elapsed × tickRate)`. When `R + 1 − sentHorizon ≥
bundleInterval`, the relay emits a bundle `[sentHorizon, R + 1)` with every pending
entry below `R + 1`, and sets `sentHorizon = R + 1`. A bundle is split at tick
boundaries when it would exceed 60,000 bytes. The per-tick byte budget means a single
tick always fits.

The default `bundleInterval` is 1: a bundle every tick, 25 per second. An empty bundle
is 11 bytes before framing, so this costs well under 2 KB/s per client, and it removes
up to a tick of waiting for every order and a tick of buffer (see below). Longer
intervals still work. Flushing a bundle early when an order arrives would not help:
the client's buffer has to cover the longest gap between bundles anyway, so an early
bundle only arrives early, not executes early.

`nextBundleMicros()` is when the next bundle is due. A host should call `update()` at
that moment rather than on a coarse timer: a timer of `T` ms delays each bundle by up
to `T`, which the clients see as jitter. The LAN host updates every millisecond; the
online relay sets its match timer to `nextBundleMicros()` ([relay](relay.md)).

## Timing model and per-client delay

Let `P` be the tick period (40 ms) and `B` the bundle interval. The relay emits horizon
`H` at time `(H − 1) × P` after match start, on a tick boundary.

### Input delay

An order clicked on a client goes through these stages before that client executes it
(`test/TurnLatencyTrace.h` measures each one):

| Stage | Typical | What sets it |
| --- | --- | --- |
| Pickup | ½ frame (≈ 20 ms) | The GUI queues the order; the next engine step hands every queued order to the session |
| Uplink | one-way latency | `addLocalOrder` sends and flushes at once |
| Relay wait | ½ tick, plus up to `B − 1` ticks | The order waits for the next bundle boundary |
| Downlink | one-way latency | Bundles are read every `TURN_POLL_MS` (5 ms) between engine steps |
| Buffer wait | the client's margin | The client's schedule runs behind the horizon by its buffer |

With `B = 1` and a steady link the total is about one round trip plus 1–1.5 ticks plus
pickup. The buffer is the term the client controls.

### Jitter estimate

On each live bundle arrival at local time `a`, the client records the offset
`o = a − H × P`. Bundles that replay the log after a `Welcome` or a resync are not
live: a bundle counts only if its horizon is above both `Welcome.relayTick + 1` and
every horizon seen before it. The offset equals a constant (one-way latency plus the
unknown clock offset) plus the jitter on that path. `JitterEstimator` keeps the last 128
offsets (about 5 s at one bundle per tick) and reports:

- `jitter = p95(o) − min(o)`, the 95th-percentile delay above the fastest delivery in
  the window. Using a minimum within the window, rather than over all time, absorbs
  clock drift over a long match.

The arrival time is when the session reads the frame, so the engine reads the
connection between its steps (`Engine::pollTurnSession`, at most `TURN_POLL_MS` = 5 ms
apart). Reading it only once per frame would round every arrival up to the next
frame, and a client's own speed changes would then look like jitter.

### Buffer target

`JitterBuffer` turns jitter into a target buffer level, measured in ticks of
authorized-but-unexecuted work (`horizon − executedTick`, sampled after each tick):

```
required = 0                                   if jitter ≤ tolerance (10 ms)
required = ceil(jitter / P) + safetyTicks      otherwise (safetyTicks = 1)
required = clamp(required, minTarget = 0, maxTarget = 50)
```

A target of 0 means the client runs one tick behind the horizon it has received: the
bundle for tick `t` arrives during tick `t − 1`'s frame. Jitter within the tolerance
needs no buffer, because the engine absorbs it as a stall of the same size and then
moves its schedule (see [pacing](#the-engine-loop)). Hysteresis keeps the target from
oscillating:

- **Up:** when `required > target`, the target rises to `required` at once. Stalls are
  worse than a little extra delay.
- **Down:** when `required < target` has held continuously for 5 s, the target falls
  by one tick, and the hold timer restarts. The target therefore drains back to
  baseline at no more than one tick (40 ms) per 5 s once jitter subsides. Any update
  in which `required ≥ target` cancels the hold.

### Rate control

`DelayController` holds the buffer at the target by nudging the client's tick rate. It
keeps an exponential moving average of the buffer level with a time constant of about
20 ticks (0.8 s). With `B > 1` the level saws between `L` and `L + B − 1` as bundles
arrive, so the controller first averages each bundle period (the mean of a whole period
does not depend on where it starts), feeds those means to the average with the same
per-tick time constant, and aims at `target + (B − 1) / 2`:

```
error      = ema − target − (B − 1) / 2
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

**Stalls.** `TurnSession::stallStats()` counts the times the engine wanted to run a tick
the relay had not yet authorized (outside catch-up and reloads), their total length, and
the long ones (over half a tick), which are visible hitches.

### Measured delay

Click (or submission) to execution, before and after the latency work (relay assigns
the first unbroadcast tick, one-tick bundles by default, orders flushed at once, the
buffer target and schedule changes above, connection polling between frames).

Simulated network (`TurnHarness`, "input delay and stalls per link profile"): two
humans and an AI, submission to execution, 60 s per run, five network seeds per row.

Before, LAN used one-tick bundles and online relays two-tick bundles; after, both use
one. Delay is mean / p95 in ms; stalls are counted over the five 60 s runs (long: over
half a tick).

| Measured link (one way) | Before, 1-tick bundles | Before, 2-tick bundles | After | Stalls before (1 / 2-tick) | Stalls after |
| --- | --- | --- | --- | --- | --- |
| loopback | 120 / 120 | 160 / 160 | 40 / 40 | 0 / 0 | 0 |
| 15 ms | 160 / 160 | 160 / 160 | 80 / 80 | 0 / 0 | 0 |
| 25 ms | 160 / 160 | 200 / 200 | 80 / 80 | 0 / 0 | 0 |
| 50 ms | 200 / 200 | 240 / 240 | 120 / 120 | 0 / 0 | 0 |
| 30 ms, 80 ms jitter | 346 / 376 | 367 / 400 | 299 / 320 | 0 / 0 | 0 |
| 60 ms, 80 ms jitter | 407 / 440 | 425 / 472 | 358 / 400 | 0 / 0 | 0 |
| 30 ms, 80 ms jitter, 3% loss | 515 / 607 | 499 / 599 | 476 / 559 | 0 / 7 (3 long, 170 ms) | 1 (10 ms) |
| 120 ms, 3% loss | 561 / 617 | 554 / 651 | 509 / 590 | 0 / 17 (12 long, 495 ms) | 4 (35 ms) |

Add about half a frame (20 ms) of pickup for a click. Real engines on the same
simulated network (`TurnEngineHarness`, "input delay and stalls of real engines per
link profile"; click to execution, where the bot's click waits a whole tick for
pickup; before is the then-default two-tick bundles):

| Measured link (one way) | Before | After | Stalls before | Stalls after |
| --- | --- | --- | --- | --- |
| 15 ms | 240 / 240 | 120 / 120 | 0 | 0 |
| 50 ms | 320 / 320 | 200 / 200 | 0 | 0 |
| 60 ms, 80 ms jitter | 466 / 480 | 409 / 440 | 0 | 0 |
| 120 ms, 3% loss | 621 / 680 | 537 / 600 | 0 | 1 (5 ms) |
| 120 ms, 20 ms jitter, 3% loss | 675 / 795 | 606 / 640 | 0 | 0 |

LAN, real engines over loopback WSS (`LanMatchHarness`, "LAN input delay ..."): host
and one guest, FourSquares1 with a Nicowar AI, clicks at random moments, 25 s per run,
macOS arm64 on a shared, loaded machine (real-time numbers vary by a tick or so from
run to run).

| Guest link (one way) | Bundles | Host before | Host after | Guest before | Guest after |
| --- | --- | --- | --- | --- | --- |
| loopback | 1 tick (LAN) | 197 / 238 | 140 / 162 | 279 / 383 | 193 / 241 |
| +25 ms | 1 tick (LAN) | 237 / 289 | 149 / 201 | 274 / 318 | 221 / 262 |
| +50 ms | 1 tick (LAN) | 281 / 347 | 140 / 160 | 455 / 505 | 261 / 292 |
| loopback | 2 ticks | 369 / 568 | 163 / 197 | 365 / 620 | 194 / 257 |
| +50 ms | 2 ticks | 248 / 298 | 159 / 197 | 378 / 434 | 250 / 309 |

No run stalled more than once. On that machine (load average around 60 on 8 cores)
the engines' threads were descheduled often enough that every client measured over
10 ms of jitter and held a two-tick buffer. In a quieter run of the same code (load
about 25), the host held no buffer and measured 59 / 76 ms on loopback, and the guest
87 / 144 ms on loopback and 164 / 262 ms at +50 ms.

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
- A decided game does not wait out grace. Once any client has left with
  `Quit(GameFinished)` (its engine declared the game over), the relay ends the match as
  soon as no human seat is connected, lagging or resyncing: seats still in grace are
  marked left by grace at that moment (their quit orders land after the decisive tick,
  so they change nothing). A player who closes the window after the end therefore does
  not hold the result back for three minutes.

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
| `addLocalOrder` | Encodes, submits and flushes a human order; null and latency orders are ignored |
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

- `TurnSession::update(now)` pumps the transport and timers every frame. Between
  steps, the host loop calls `Engine::pollTurnSession` at least every `TURN_POLL_MS`
  (5 ms; `sessionPollDelay()` caps the host's sleep), which only reads the connection.
  A turn game draws only after a step, so these polls draw nothing.
- **Orders.** Each step hands every order the GUI has queued to the session, even while
  waiting for a bundle, rather than one order per executed tick. The relay still gives
  each its own tick.
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
  sends `Quit` in its place (with the reason above), and the relay sequences the
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
   (`minutes × 60 × 25` ticks) toggled.

**Seats, players and teams.** A human or AI seat is a player: seat `s` is
`BasePlayer` `s`, and that number is what tickets (`seat`, `humanSeats`), the relay,
`TurnSession`'s local seat, the match record, `--verify-match`, the order audit and
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

## Relay

`glob2-relay` (`src/relay/`, operated as described in [relay.md](relay.md)) hosts
`TurnSequencer` for online matches:

- **Transport.** Clients connect over a WebSocket (`wss://…/relay`). The frames of
  this protocol, each with its 2-byte length prefix, form a byte stream carried in
  binary WebSocket messages, as `NetConnection` sends them over `WssTransport`.
- **Matches.** One sequencer per match is created by the first valid ticket for its
  `matchId`, with `humanSeatMask` taken from that ticket's `humanSeats`. Every
  later ticket must carry the same `simVersion` and `humanSeats`. A match that has
  ended cannot be started again by a late ticket.
- **Admission.** The admission function is the Ed25519 ticket check. The relay
  verifies the ticket before it passes the `Hello` to the sequencer, and refuses a
  bad ticket with `Reject(2)`.
- **New matches refused.** A draining or full relay refuses a new match with
  `Reject(5)`, and still admits reconnects to its running matches.
- **Timing and threads.** The relay calls `update` every 10 ms, and runs every
  sequencer on one event-loop thread.
- **End of a match.** The relay uploads the `MatchRecord` to the platform. A client
  that sends `Quit` with reason 1 (game finished) marks the match as completed rather
  than abandoned in the relay's report. The sequencer treats both reasons alike.

## Testing

The unit tests in `test/TurnProtocolTest.cpp` (in `glob2-unit-tests`) use a fake clock
and drive the components directly:

- codec round trips, and rejection of malformed or oversized input;
- sequencer ordering, one order per seat per tick, assignment to the first unbroadcast
  tick, the byte budget, flooding, grace and quit, majority arbitration and the
  two-client flag;
- jitter estimation, a target that rises and then falls back, and the controller's
  handling of multi-tick bundles;
- match record round trip and corruption detection.

`test/TurnHarnessTest.cpp` connects 2–4 `TurnSession` clients to a `TurnSequencer` over
a simulated network with per-link latency, jitter, loss (as TCP-style retransmission
delay) and disconnects. It checks that every client executes the same
`(tick, seat, order)` sequence as the relay's log. It covers a mid-game transport drop
with incremental resume, a client restart with a full reload, and a desync that the
majority repairs. It also checks that a stalled client never stalls the others, that
each client's buffer follows its own link's jitter, and that each player's input delay
follows their own connection. A regression case requires the mean and p95 input delay
to stay within bounds on loopback (60 / 80 ms) and at 50 ms one way (150 / 170 ms),
with no long stalls. A `[benchmark]` case writes the delay and stall table above
(`turn-delay-profiles.txt`). Summaries are written under `artifacts/tests/`.

The relay's own tests (`glob2-relay-tests` and `tests/relay/`) run this protocol over
real WebSockets against `glob2-relay`; see [relay.md](relay.md#tests).

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
  play on;
- three engines where one client sends malformed, mutated and cross-team orders of
  every type through the relay: no client crashes, all agree at every tick, every
  client and the verifier count the same rejections, and the record verifies;
- a record into which a hostile relay forged another seat's quit, a latency order,
  undecodable bytes, cross-team orders and voice: the verifier refuses them and still
  verifies.

Each case verifies the relay's record with `--verify-match` and requires the verifier's
per-tick checksums and `result.json` team outcomes to equal the live clients'. Forged
turns make the record unverifiable, and a seat whose reports disagree is named. A
`[benchmark]` case measures rejoin fast-forward time against game length and AI count
(`python3 test/run_tests.py --tag benchmark --filter 'TurnEngineHarness/*'`).

`test/OrderValidationTest.cpp` checks each validation rule on a two-team game, runs
random payloads of every order type through decoding, the check and execution, and
executes hostile orders unchecked to show the executor guards.

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
