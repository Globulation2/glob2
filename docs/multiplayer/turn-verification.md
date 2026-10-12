# Turn records and verification

Match records, relay requirements and focused protocol verification.

## Match record

`MatchRecord` is the relay's persistent output, consumed by `match verify`. Its
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
setupJson        text32 (≤ 32 MiB; MatchSetup JSON, opaque to C++ here)
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

`glob2 match verify` replays a record through the same engine path a live client
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
- **Timing and threads.** Each match sleeps until `TurnSequencer::nextWakeMicros()`:
  the next bundle while anyone is connected (30 wakes/s with default one-tick bundles), the earliest grace
  expiry (at most a second away) while nobody is, and at once when an event left a
  presence change to broadcast. An update after a long sleep broadcasts the whole gap
  in one bundle. Every sequencer runs on one event-loop thread. The sequencer's own
  work per update no longer grows with the match: arbitration keeps the set of ticks
  still waiting for reports instead of walking every report since tick 0
  ([relay](relay.md#timing)).
- **End of a match.** The relay uploads the `MatchRecord` to the platform. The match
  is reported completed when the sequencer counts the game as decided (every seat
  still playing at the first `Quit(GameFinished)` left that way), and abandoned when
  every human left otherwise.


## Testing

The unit tests in `src/net/turn/TurnProtocolTest.cpp` (in `glob2-unit-tests`) use a fake clock
and drive the components directly:

- codec round trips, and rejection of malformed or oversized input;
- sequencer ordering, one order per seat per tick, assignment to the first unbroadcast
  tick, the byte budget, a flood dropped without losing the seat, grace and quit, a
  lone `GameFinished` that decides nothing while every seat's claim does, the wake
  schedule, arbitration over a 60-minute four-player match, majority arbitration and
  the two-client flag;
- session pacing: the burst and one order per tick, latest-wins for flag moves (with
  the drop kept), unmerged orders in order, voice behind gameplay orders, the bounded
  queue and its notice, and a flood `Reject` that reconnects;
- jitter estimation, a target that rises and then falls back, and the controller's
  handling of multi-tick bundles;
- match record round trip and corruption detection.

`src/net/turn/TurnHarnessTest.cpp` connects 2–4 `TurnSession` clients to a `TurnSequencer` over
a simulated network with per-link latency, jitter, loss (as TCP-style retransmission
delay) and disconnects. It checks that every client executes the same
`(tick, seat, order)` sequence as the relay's log. It covers a mid-game transport drop
with incremental resume, a client restart with a full reload, and a desync that the
majority repairs. It also checks that a stalled client never stalls the others, that
each client's buffer follows its own link's jitter, and that each player's input delay
follows their own connection. A regression case requires the mean and p95 input delay
to stay within bounds on loopback (60 / 80 ms) and at 50 ms one way (150 / 170 ms),
with no long stalls. A `[benchmark]` case writes `turn-delay-profiles.txt`; see
[measurement methodology](turn-timing.md#measured-delay). Summaries are written under `artifacts/tests/`.

The relay's own tests (`glob2-relay-tests` and `test/relay_service/`) run this protocol over
real WebSockets against `glob2-relay`; see [relay.md](relay.md#tests).

`src/net/turn/TurnEngineHarness.cpp` (in `glob2-engine-tests`) runs the same network with 2–4
real engines started by `Engine::initTurnMatch` on one MatchSetup, AI seats computed on
every client and each human seat driven by a bot that queues orders through the GUI's
order queue. Every client records the checksum before each tick, and the cases require
all of them to agree at every tick (and with the relay's agreed checksums):

- two engines with AI seats under latency, jitter and loss;
- four engines, one stalled for 5 s (incremental resume) and one restarted as a new
  process (full log, fast-forward from tick 0);
- three engines where one executes a tampered order: the majority tells it to rejoin,
  it reloads in place and fast-forwards, and `match verify` names its seat;
- a player who quits through the sequenced `PlayerQuitsGameOrder` while the others
  play on;
- three engines where one client sends malformed, mutated and cross-team orders of
  every type through the relay: no client crashes, all agree at every tick, every
  client and the verifier count the same rejections, and the record verifies;
- a record into which a hostile relay forged another seat's quit, a latency order,
  undecodable bytes, cross-team orders and voice: the verifier refuses them and still
  verifies.

Each case verifies the relay's record with `match verify` and requires the verifier's
per-tick checksums and `result.json` team outcomes to equal the live clients'. Forged
turns make the record unverifiable, and a seat whose reports disagree is named. A
`[benchmark]` case measures rejoin fast-forward time against game length and AI count
(`python3 test/run_tests.py --tag benchmark --filter 'TurnEngineHarness/*'`).

`src/game/orders/OrderValidationTest.cpp` checks each validation rule on a two-team game, runs
random payloads of every order type through decoding, the check and execution, and
executes hostile orders unchecked to show the executor guards.

`src/online/MatchSetupTest.cpp` runs every MatchSetup and SimVersion contract fixture: valid
ones must parse and round-trip, invalid ones must fail at the stage the manifest names.
It reads `platform/packages/protocol/fixtures` when that directory exists; until the
platform workspace is on the branch it reads the copy in `test/fixtures/protocol`, and
once both exist it requires the copy to equal the source.

`test/fixtures/multiplayer/FourSquares1.g2mr` is a short recorded match (two humans,
Nicowar and Warrush) with its expected verification trace. The browser/native
simulation equivalence job verifies it on Linux, Windows, macOS and in three browsers and
requires identical traces (see
[headless replays](../development/headless-replays.md#verifying-a-match-record)). A
simulation change makes it stale and must bump `SIM_REVISION`; `python3
test/run_tests.py --update-fixtures --filter 'TurnEngineHarness/the committed*'`
records a fresh match and trace.

Building catalogs are part of match rules identity: a setup carries the canonical
embedded catalog and its hash, and clients and verifiers compare it with the map
before starting. A different catalog, or an omitted catalog for a custom map, is
rejected. Historical schema-1 setups without catalog fields are accepted only
against the frozen legacy stock catalog. Engine executable routing still uses
the engine simulation version; catalog-specific rules identity partitions ratings.

[Multiplayer index](README.md) · [Documentation index](../README.md).
