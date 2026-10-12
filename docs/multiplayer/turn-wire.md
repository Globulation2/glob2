# Turn wire and order reference

Framing, messages, order validation and tick assignment. The [lockstep overview](turn-protocol.md) explains the session model.

## Framing and versioning

Each message is a frame payload: one message-type byte followed by the body. The
payload is the content of one `NetConnection` frame, so TCP (LAN) and WSS connections
reuse the existing 2-byte big-endian length prefix and the 65,535-byte frame limit
unchanged. One implementation, `NetFrame` (`src/net/NetFrame.h`), writes and reassembles
these frames for `NetConnection`, `LanLink`, the client's `RelayTransport` and the relay.
All integers are big-endian, as in `GAGCore::BinaryOutputStream`.
`bytes16` is a `u16` length followed by that many bytes. `text32` is the
`BinaryOutputStream::writeText` encoding: a `u32` length followed by the bytes.

Turn messages use the reserved message-type range `0xA0`–`0xBF`, which is assigned
explicitly in `NetMessageType.h`; the YOG lobby messages that once used the low
values were deleted at the cutover without touching it. `NetMessage::getNetMessage` decodes turn messages too, so a
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

`TURN_PROTOCOL_VERSION` (currently 2) is carried in `Hello` and `Welcome`. A relay
accepts every version from `MIN_PROTOCOL_VERSION` (1) to its own and answers `Welcome`
in the client's version. It rejects anything outside that range. Version 2 adds
`SeatLatency`, which the relay sends only to version-2 clients. A version-2 client
that an older relay refuses with `Reject(1)` offers version 1 once before giving up. The relay does not compare simulation
versions: that comparison happens on the platform, which issues tickets only for one
sim version per match. A protocol change that alters any encoding bumps
`TURN_PROTOCOL_VERSION`. A change that keeps the encodings but alters relay behaviour
that clients depend on also bumps it.

Clients do not depend on which tick the relay picks for an order (only that it is at
or above the horizon), on the bundle interval (it comes in `Welcome`), or on how they
size their own buffer. The latency changes described under
[timing](turn-timing.md#timing-model-and-per-client-delay) therefore kept version 1: a client and a
relay from either side of them play together, with the older side's delay.

[Order pacing](turn-wire.md#order-pacing), the relay dropping a flood instead of refusing the
client, the stricter rule for a [decided game](turn-timing.md#presence-reconnect-and-grace) and the
relay's wake scheduling also kept version 2: no encoding changed, and no client relied
on the old behaviour. An older client against a newer relay keeps its seat when it
floods (it loses the orders beyond the limit); a newer client against an older relay
paces its orders, so it never floods, and treats a `Reject(7)` that still arrives (the
relay's frame-rate limit) as a lost connection and reconnects.


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
| `0xAC` | `SeatLatency` (v2) | R→C | `u8 count`, `count × (u8 seat, u32 rttMicros)` |

Field rules that the decoder enforces:

- `TurnBundle`: `fromTick ≤ horizonTick`. Entries are strictly ordered by
  `(tick, seat)`, so each `(tick, seat)` pair appears at most once. Every entry
  satisfies `fromTick ≤ tick < horizonTick` and `seat < 32`, and every order is 1–4,096
  bytes.
- `Presence`: seats are below 32, unique and ascending; the state is a known value.
- `SeatLatency`: seats are below 32, unique and ascending.
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
(`src/game/orders/OrderValidation.h`). The check reads only the game state at that point of that
tick and the seat from the bundle, never an identity the order claims, so every client
and `match verify` reach the same verdict. An order that fails, or whose bytes do not
decode, executes as a `NullOrder` everywhere; the game and every checksum stay the same
on all machines. AI orders, single player, replays and legacy network games are not
checked.

| Verdict | Meaning | Examples |
| --- | --- | --- |
| accepted | executed unchanged | anything the user interface sends |
| stale | it no longer applies; network latency can cause it | cancelling a deletion that is no longer pending |
| rejected | no unmodified client sends it | another team's buildings or alliances, another player's quit, a worker count above 20, an unplaceable building type, an off-map flag position, an unknown brush mode or message type, an `AdjustLatency`, undecodable bytes |

The rules per order type are in `OrderValidation.cpp`. Pause orders stay allowed for
every player, as in legacy games, unless the match has a [pause limit](turn-wire.md#pause-limit).
Voice packets are checked (at most 128 frames) and the mixer drops packets beyond about
ten seconds of backlog per player.

### Pause limit

A MatchSetup may carry `pauseLimit: {pauses, seconds}`. The platform sets
`{pauses: 3, seconds: 60}` for every queue match (quick and rated); rooms and LAN
games have none, and pausing there is unlimited, as before. With a limit, each human
seat may start at most `pauses` pauses and keep the game paused for at most `seconds`
in total (counted in executed ticks, which keep running while paused: 60 s is 1,500
ticks). Any player may resume at any time, and a pause another seat started costs the
resuming player nothing.

`TurnLockstepSession` keeps the bookkeeping from the orders it executes, so every
client and `match verify` agree on it:

- A seat's `PauseGameOrder(true)` while the game runs starts a pause and counts one of
  that seat's pauses. With none left, or no time left, it executes as a `NullOrder`
  (verdict `stale`, reason `pause_limit`) and the player sees "You have no pauses left
  in this match". A pause while already paused changes nothing and costs nothing.
- Every tick executed while paused counts against the seat whose pause is running.
- When that seat's time is used up, the engine executes `PauseGameOrder(false)` for it
  right after that tick's orders (`takeForcedResume`), and every player sees "%0 has
  used all their pause time: the game resumes". The resume is in the replay like any
  pause order.
- The bookkeeping restarts with the order audit when the engine reloads the initial
  state, and is rebuilt from the replayed orders.

Clients from before this rule refuse a setup with `pauseLimit` (MatchSetup allows no
unknown properties), so they can never play a limited match without it and diverge.

The session counts verdicts per seat (`TurnLockstepSession::orderAudit()`: accepted,
stale, rejected, per-reason counts and the first rejected tick). The counts restart when
the engine reloads the initial state. `match verify` reports the same counts for the
record, so the platform can flag a seat that sent rejected orders. Voice is not in the
record, so rejected voice packets are counted separately and only by live clients.

Executors that a hostile order could stop with an assert or an out-of-range index
(`Game::executeCreate`, the area brushes, alliances, quits, building lookups,
`Building::cancelConstruction`, chat and map marks in `GameGUI`) now ignore such an
order instead. This only changes what invalid orders do, so legacy games and replays
of valid orders run as before.

### Order pacing

The relay sequences at most one order per seat per tick: 30 orders/s at the
default rate. GUI interactions can produce orders faster than this, so the
session paces submission and coalesces unsent absolute-setting orders:

- **Credit.** It sends at most what the relay sequences: a credit of `orderBurst` (4)
  orders, refilled at one per tick. A click that makes a few orders at once still sends
  them all at once. Orders resent after a reconnect use the credit too.
- **Latest wins.** Orders beyond the credit wait in a local queue. A queued order whose
  effect is an absolute setting replaces a waiting one with the same target, and takes
  its place at the back: a flag move (keeping the drop of the replaced one, since a
  drop also refreshes the flag's gradients), a worker count, a flag range, a clearing
  flag's resources, a minimum unit level, swarm ratios, market exchanges, a priority,
  and the pause state. The intermediate values never reach the relay, so a drag sends
  the flag's latest position once per tick, and the player sees the flag follow the
  pointer with the usual delay. Orders that must all execute (creating, deleting and
  upgrading buildings, brush strokes, which are already one order per stroke, chat,
  alliances, map marks) wait their turn unmerged.
- **Bounded queue.** The queue holds at most `maxQueuedOrders` (250 orders, about 8.33 s at the default rate) of
  them; beyond it a new order is dropped. While more than a second of orders waits, or
  for two seconds after a drop, the HUD shows "Too many actions: some are still
  waiting to be sent" (`TurnSession::tooManyActions()`).
- **Voice.** Voice packets have their own queue of at most 8 (the oldest is dropped).
  A packet goes out only when no gameplay order waits, and at most one every
  `voiceGapTicks` (3) ticks, so talking delays a command by at most the packet already
  on its way.

Coalescing happens only for orders that have not been sent, so it never changes what
the relay has sequenced, and every client still executes exactly the relay's log. The
GUI's own `queueFlagMove` coalescing works the same way inside the GUI queue, which a
turn game drains every frame.


## Tick assignment

When an order arrives, the relay assigns it to tick `t`, the smallest value that
satisfies all of these:

- `t ≥ sentHorizon`, so it never lands in a tick already authorized;
- `t ≥ nextFreeTick[seat]`, so a seat gets at most one order per tick;
- `bytes[t] + size ≤ 30,000`.

Then `nextFreeTick[seat] = t + 1`. The relay's clock does not impose another
minimum execution tick: clients can only execute authorized horizons, so the
first unbroadcast tick is safe and travels in the next bundle.

A seat whose queue reaches more than `maxAheadTicks` (default 250, about 8.33 s
at 30 ticks/s) ahead of the relay tick is flooding. The relay drops the order,
counts `flood_rejections` in its network summary, and keeps the connection open.
A dropped order never enters a bundle, so every client retains the same log.


### Bundles

`update(now)` computes `R = floor(elapsed × tickRate)`. When `R + 1 − sentHorizon ≥
bundleInterval`, the relay emits a bundle `[sentHorizon, R + 1)` with every pending
entry below `R + 1`, and sets `sentHorizon = R + 1`. A bundle is split at tick
boundaries when it would exceed 60,000 bytes. The per-tick byte budget means a single
tick always fits.

The default `bundleInterval` is 1: a bundle every tick, 30 per second at the default rate. An empty bundle
is 11 bytes before framing, so this costs well under 2 KB/s per client, and it removes
up to a tick of waiting for every order and a tick of buffer (see below). Longer
intervals still work. Flushing a bundle early when an order arrives would not help:
the client's buffer has to cover the longest gap between bundles anyway, so an early
bundle only arrives early, not executes early.

`nextBundleMicros()` is when the next bundle is due. A host should call `update()` at
that moment rather than on a coarse timer: a timer of `T` ms delays each bundle by up
to `T`, which the clients see as jitter. The LAN host updates every millisecond; the
online relay sets its match timer to `nextBundleMicros()` ([relay](relay.md)).

[Multiplayer index](README.md) · [Documentation index](../README.md).
