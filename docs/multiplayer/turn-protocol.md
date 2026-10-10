# Turn protocol: lockstep overview

Relay-sequenced lockstep provides one order stream for every participant. For byte layouts see the [wire reference](turn-wire.md); for buffering and recovery see [timing](turn-timing.md).

## Model

- The relay owns the clock. Tick `t` starts `t × 1000 / 30 ms` after the match starts
  (30 ticks/s, or `tickRateMilliHz / 1000` ticks per second).
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


## AI order scheduling

MatchSetup's optional `rules.aiOrderDelay` is an integer from 0 through 8;
omitting it means 0 for backward compatibility. New match defaults explicitly set
it to 8 (about 267 ms at the normal 33⅓ ms tick interval). This delays AI responses to
observed changes and allows decisions to overlap subsequent simulation ticks. The engine stores it in GameHeader and uses the same delay
for every AI seat. A decision at tick `t` yields an order for `t + delay`, with
due computation completed before delivery. This logical AI delay is independent
of the transport's jitter buffer and does not delay human commands. Match setup,
save continuation and verification must preserve the value.

The authoritative simulation owner captures immutable AI inputs and delivers
commands in stable request order. It checks entity incarnations and normal order
validity at delivery, then feeds execution receipts into subsequent decisions.
Remote/replay JavaScript replicas retain visibility memory through ordered frozen
observations without running an extra decision. Worker count and elapsed
computation time do not select simulation order ticks. See the
[engine scheduling contract](../architecture/ai-observations.md#ai-observations-and-delayed-orders)
for save and lifecycle boundaries.


## Session flow

1. The client opens the transport and sends `Hello` with its ticket. `haveHorizon` is
   0 on a fresh start. After a transport loss without a restart, it is the horizon the
   client already holds.
2. The relay calls its admission function to get a seat. In `glob2-relay` this is
   the ticket check described in [the relay section](turn-verification.md#relay). It refuses with `Reject` and closes when the ticket
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

[Multiplayer index](README.md) · [Documentation index](../README.md).
