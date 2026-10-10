# Turn timing and recovery

Input delay, jitter buffering, presence, reconnection and desync arbitration.

## Timing model and per-client delay

Let `P` be the tick period (33⅓ ms) and `B` the bundle interval. The relay emits horizon
`H` at time `(H − 1) × P` after match start, on a tick boundary.

### Input delay

An order clicked on a client goes through these stages before that client executes it
(`src/net/turn/TurnLatencyTrace.h` measures each one):

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
moves its schedule (see [pacing](turn-engine.md#the-engine-loop)). Hysteresis keeps the target from
oscillating:

- **Up:** when `required > target`, the target rises to `required` at once. Stalls are
  worse than a little extra delay.
- **Down:** when `required < target` has held continuously for 5 s, the target falls
  by one tick, and the hold timer restarts. The target therefore drains back to
  baseline at no more than one tick (33⅓ ms) per 5 s once jitter subsides. Any update
  in which `required ≥ target` cancels the hold.

### Rate control

`DelayController` holds the buffer at the target by nudging the client's tick rate. It
keeps an exponential moving average of the buffer level with a time constant of about
20 ticks (about 0.67 s at the default 30 ticks/s). With `B > 1` the level saws between `L` and `L + B − 1` as bundles
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
5% slow. Over 10 s that moves the buffer by up to 15 ticks at the default rate, which absorbs drift and
new jitter without a visible change in game speed.

**Catch-up.** When the instantaneous buffer exceeds `target + 25` (about 0.83 s beyond the target at 30 ticks/s),
the controller enters catch-up mode. The engine then runs ticks at uncapped speed with
rendering skipped, reusing the replay fast-forward path (`REPLAY_FAST_FORWARD_MS`) with
the `MAX_CATCHUP_MS` cap lifted. Catch-up ends when the buffer drops to `target + 2`, and
the moving average is reset to the current level. A reconnect or resync from tick 0
always starts in catch-up.

**Stalls.** `TurnSession::stallStats()` counts the times the engine wanted to run a tick
the relay had not yet authorized (outside catch-up and reloads), their total length, and
the long ones (over half a tick), which are visible hitches.

### Measured delay

Measure both order submission-to-execution and click-to-execution. The latter
includes frame pickup; report it separately from uplink, relay wait, downlink and
buffer wait. Record tick rate, bundle interval, link profile, seed, source revision,
compiler/build flags and machine load alongside mean, median, p95 and stall counts.

The benchmark cases produce review evidence under `artifacts/tests/`:

| Harness | Case | Measures |
| --- | --- | --- |
| `TurnHarness` | `input delay and stalls per link profile` | Simulated transport with latency, jitter and TCP-style retransmission delay; multiple network seeds per profile |
| `TurnEngineHarness` | `input delay and stalls of real engines per link profile` | Real engine execution over simulated links, including frame pickup |
| `LanMatchHarness` | `LAN input delay on loopback and delayed links` | Host/guest execution over loopback WSS with optional guest-link delay and a per-stage breakdown |

Select the benchmark suites through the [test runner](../development/testing/README.md).
Compare the same link profiles, seeds and build inputs when changing pacing or
buffering. Simulated clocks isolate protocol behavior; real-time LAN runs also
reflect thread scheduling and host load. Use release builds for representative
performance measurements and attach generated reports to the change being
reviewed. Historical before/after tables belong with their original evidence,
rather than serving as current latency guarantees.

See the [LAN playtest](lan-playtest.md#baseline-input-delay) for subjective gameplay
checks and the [verification guide](turn-verification.md#testing) for regression
coverage.


## Presence, reconnect and grace

The relay keeps a presence state per human seat and broadcasts a full `Presence`
snapshot when any state changes, and at least every 25 ticks. `lagTicks` is
`R − executedTick` when the seat's last `Ping` arrived. Once no `Ping` has come for a
second (clients ping every 500 ms), the extra time counts as lag. A connected seat whose
lag exceeds `lagThresholdTicks` (default 50, about 1.67 s at 30 ticks/s) is
shown as lagging. Version-2 clients also receive
`SeatLatency` with every `Presence`: each connected human seat's round trip as the
relay measures it on its transport (the online relay's WebSocket ping, smoothed with
weight ¼; 0 when not measured, as on a LAN host). The connection panel shows it as
Ping ([connection quality](connection-quality.md)).

- A seat starts as not yet connected. When its transport closes, it becomes
  reconnecting.
- In either state, a grace timer (default 3 min, `graceTicks` in `Welcome`) runs from
  match start or from the disconnect. If the seat reconnects in time, it is connected
  again. If the timer expires first, the relay sequences a `PlayerQuitsGameOrder` for
  the seat through the normal assignment path and marks the seat left.
- A left seat cannot reconnect: its `Hello` is refused with `Reject(4)`.
- The match is over when every human seat has left. The relay then closes the match and
  produces the match record.
- A client's `Quit(GameFinished)` is a claim, not a verdict: one client cannot end the
  match for the others or shorten their grace. The relay counts the match as decided
  (`gameDecided()`, and the report says `completed`) only when every human seat still
  in the match at the first such claim has also left with `GameFinished`. Seats that
  left before the first claim (resigned, or out of grace) do not need to agree. A seat
  that lost its connection when another claimed the end keeps its full grace: it can
  come back, see the end itself and leave with `GameFinished`, or come back and play
  on if the claim was false. If it never returns, the match ends when its grace runs
  out and is reported `abandoned`. The verifier, not the claim, decides the result.
  (Before this rule, a single claim ended the match as soon as nobody was connected,
  quitting the seats in grace at once, so one player could cut an opponent's reconnect
  short and have the match reported as completed.)

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
`t % checksumInterval == 0` (every 25 ticks, about every 0.83 seconds). The reports travel out
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

[Multiplayer index](README.md) · [Documentation index](../README.md).
