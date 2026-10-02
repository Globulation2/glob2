# Network telemetry

Turn games (online and LAN, [turn protocol](../multiplayer/turn-protocol.md)) measure
their network behaviour on both ends: every client measures its own session, and the
relay (the online `glob2-relay` or a LAN host) measures every seat. The measurements
follow the [performance telemetry](performance-telemetry.md) collection contract and
leave the outputs it describes as they were, apart from the additions listed here.

## Collection contract

- **Diagnostic only.** Nothing enters saves, orders, RNG, simulation checksums, match
  records (`G2MR`), replays or AI decisions. Collection never changes what a session
  sends or when. The `TurnEngineHarness` case "engines report network telemetry, and
  its output changes nothing they execute" plays the same match with
  `GLOB2_TEAM_TIMELINE` on and off and requires identical per-tick checksums.
- **Existing events only.** Counters update where the session already handles a frame,
  pong, bundle, order, tick or presence change. No extra messages, polling or clocks.
- **Bounded.** Distributions are fixed-size log-linear histograms (`Turn::Histogram`:
  16 bins per power of two, exact below 16; a quantile is within about 3% of a
  recorded value and is clamped to the observed minimum and maximum). The time series
  keeps one compact point per interval, at most 4320 points (six hours at 5 s); later
  points are counted in `dropped_points`. Input-delay matching holds at most 1024
  unexecuted submissions.
- **Clocks.** The client uses its session clock (the time the engine passes to
  `TurnSession::update`, steady milliseconds in the game). The relay uses its own
  steady clock. Ticks are 40 ms at the default 25 ticks/s. Units are in every field
  name: `_us` microseconds, `_ms` milliseconds, `_ticks` simulation ticks.
- **Identity.** A client series belongs to one match and one local seat. It survives
  reconnects and in-place reloads; a client restart (a new process) starts a new
  series. The relay's counters belong to one match.

## Outputs

| Output | Where | When |
| --- | --- | --- |
| `GLOB2_NET_SESSION`, `GLOB2_NET_SAMPLE`, `GLOB2_NET_FINAL`, `GLOB2_NET_SEAT`, `GLOB2_NET_SUMMARY` | stdout, the standard telemetry stream | `GLOB2_TEAM_TIMELINE=1`, turn games with a local seat |
| `ClientNetworkSummary` (JSON) | `<replay>.network.json` next to the game's replay (`replays/last_game.network.json` by default, or beside `GLOB2_REPLAY_PATH`) | every turn game that writes a replay, at the end of the session |
| `RelayNetworkSummary` (JSON) | LAN: `<user dir>/replays/lan-last.network.json` next to `lan-last.g2mr` | when the LAN host writes its record |
| `RelayNetworkSummary` (JSON) | online: `RelayMatchEnded.network`, stored per participant by the platform | when the relay reports the match end |
| `glob2_relay_net_*` | relay `/metrics` (`RelayNetworkTotals::writePrometheus`) | accumulated as matches end |
| `network` (JSON) | `--verify-match` `result.json` | always; derived from the match record alone |
| `pacing.network_sleep` | `GLOB2_PERF_*` records | see [pacing](#pacing-network-sleep) |

The stdout records use the key/value conventions of the other `GLOB2_*` records.
`session=` is the performance-telemetry session id, so network and performance records
of one game can be joined. A distribution `name` exports `name.count`, `name.mean`,
`name.p50`, `name.p95` and `name.max`; an empty one exports `na` for all but the count.

- `GLOB2_NET_SESSION`: once per game: `schema`, `schema_version`, `transport`, `seat`,
  `sim_version`, `interval_us`.
- `GLOB2_NET_SAMPLE`: one per closed interval (default every 5 s of session time, not
  aligned with the 512-tick performance windows): `tick_start`/`tick` (executed ticks)
  and `elapsed_start_us`/`elapsed_us` (session-relative), then the interval's values
  of the series fields below.
- `GLOB2_NET_FINAL`: cumulative totals at the end of the session, with every counter
  of the summary in flat form.
- `GLOB2_NET_SEAT`: per seat seen in presence updates: `<state>.transitions` and
  `<state>.time_us` for each presence state.
- `GLOB2_NET_SUMMARY`: the `ClientNetworkSummary` without its series, as one JSON line.

## Client: `ClientNetworkSummary` v1

One stable, versioned JSON object per match (`Turn::clientNetworkSummary`, defined in
`src/net/turn/TurnTelemetry.h`). It is written locally and nothing sends it; it is the
shape a later platform upload would carry. Its JSON Schema is `ClientNetworkSummary`
in `platform/packages/protocol` (`src/network.ts`, with fixtures), so a future upload
endpoint has a contract to validate against; no endpoint, table or upload exists. It contains no account ids,
addresses, names or free text. Adding optional fields keeps version 1; renaming,
removing or changing the meaning of a field requires version 2.

| Field | Meaning |
| --- | --- |
| `schema`, `schema_version` | `"ClientNetworkSummary"`, `1` |
| `match.sim_version` | `MatchSetup.simVersion` key |
| `match.platform` | SDL platform name (`Linux`, `Windows`, `Mac OS X`, `Android`, `iOS`, `Emscripten`) |
| `match.transport` | `"lan"` or `"online"` (`Engine::TurnMatchStart::networkKind`) |
| `match.relay_id`, `match.relay_region` | online relay, or `null` (LAN, or not provided by the caller) |
| `match.seat`, `match.human_seat_mask`, `match.players`, `match.tick_rate_millihz`, `match.final_tick` | match shape and the executed tick at the end |
| `elapsed_us` | session time covered |
| `rtt_us` | ping round trip (a ping every 500 ms), distribution |
| `jitter_us` | the session's jitter estimate (p95 − min of bundle arrival offsets), sampled at every pong |
| `input_delay_us`, `input_delay_unmatched` | local order submitted → the same order executed locally. Matched by content in submission order; voice is excluded; orders the relay dropped stay unmatched |
| `jitter_buffer.buffered_ticks`, `jitter_buffer.target_ticks` | authorized but unexecuted ticks and the buffer target, sampled on every tick executed outside catch-up |
| `tick_rate_nudge` | `live_ticks`; `ticks_faster`/`ticks_slower` (ticks run with the ±5% nudge above/below nominal); `mean` and `max_abs` of (multiplier − 1) |
| `stalls` | horizon starvation: the engine wanted a tick and the authorized horizon was used up. `count`, `total_us`, `longest_us`, `duration_us` distribution. The wait for every player to load before the first tick is not a stall; a stall during a link loss overlaps `reconnects.downtime_us` |
| `catch_up` | uncapped fast-forward episodes (including after a reload): `episodes`, `ticks`, `wall_us` |
| `reconnects` | link losses after the first Welcome: `count`, `downtime_us` (loss to the next Welcome), `longest_downtime_us`, `down_now` |
| `reloads` | in-place reloads of the initial state (relay resumed from tick 0, or told to rejoin): `count`, `load_us` (engine load time), `fast_forward_ticks`, `fast_forward_us` |
| `traffic` | turn-protocol payloads (frame bodies without the 2-byte length or WebSocket/TCP framing): `frames_sent`, `bytes_sent`, `frames_received`, `bytes_received`, `bundles_received`, `bundle_bytes`, `bundle_entries` |
| `orders` | `submitted` (accepted by `addLocalOrder`), `frames_sent` (OrderSubmit frames, resends included), `resent` (after a reconnect), `queued_offline` (submitted while the link was down), `dropped_local` (null, latency-adjust or oversized), `outstanding_max` (not yet acknowledged). The session has no order coalescing |
| `voice` | `sent`/`sent_bytes` (own voice packets), `received`/`received_bytes` (other seats') |
| `desync` | `rejoins` (this client told to rejoin), `flagged` (match flagged for the verifier), `resync_requests` |
| `presence` | other seats as the relay reports them: `transitions` total; per seat `final_state`, `transitions` and `time_us` per state (`not_connected`, `connected`, `lagging`, `reconnecting`, `resyncing`, `left`) |
| `ticks` | `executed` (all), `live` (outside catch-up) |
| `order_validation` | per human seat `accepted`, `stale`, `rejected`, `voice_rejected`, `rejected_by_reason` from the deterministic order check (`TurnLockstepSession::orderAudit()`, since the game last started from tick 0), or `null` when the engine installed no check. The same on every client of a match |
| `series` | `interval_us`, `dropped_points`, and `points`: per interval `start_us`, `end_us`, `start_tick`, `end_tick`, the distributions `rtt_us`, `jitter_us`, `input_delay_us`, `buffered_ticks`, `target_ticks`, `stall_us`, and the interval's `bytes_sent`, `bytes_received`, `frames_sent`, `frames_received`, `ticks_executed`, `live_ticks`, `ticks_faster`, `ticks_slower`, `mean_nudge`, `catch_up_ticks`, `reconnects`, `downtime_us`, `orders_submitted`, `voice_sent`, `voice_received`, `presence_transitions`. Left out of `GLOB2_NET_SUMMARY` |

Distributions are objects `{count, mean, p50, p95, max}`.

## Relay: `RelayNetworkSummary` v1

`TurnSequencer::networkSummary()` (`Turn::sequencerSummaryJson`). Per match:
`tick_rate_millihz`, `end_tick`, `duration_ms`; `bundles` (`broadcast`, `bytes`: each
distinct live bundle once); `arbitration` (`ticks` arbitrated, `unanimous`, `majority`
(minority told to rejoin), `flagged`, `timed_out` (arbitrated with partial reports after
the timeout)); `peak_backlog` (`pending_ticks`, `pending_entries`, `pending_bytes`:
sequenced but not yet broadcast); `rejected_peers`. Per human seat:

| Field | Meaning |
| --- | --- |
| `orders.sequenced`, `orders.bytes` | orders given an execution tick (voice excluded; the relay's own quit order included) |
| `orders.deferred`, `orders.defer_ticks` | orders (and voice) placed later than the earliest tick by the one-order-per-seat-per-tick rule or the tick byte budget, and the ticks that added |
| `orders.duplicates_ignored`, `orders.dropped`, `orders.flood_rejections` | resubmissions already sequenced; null/latency/forged-quit orders refused; floods |
| `orders.max_queued_ahead_ticks` | furthest a seat's next free tick ran ahead of the relay clock |
| `voice.sequenced`, `voice.bytes` | voice packets passed through (not in the record) |
| `traffic` | `frames_received`/`bytes_received` after admission; `bundles_sent`/`bundle_bytes_sent` live; `log_bundles_sent`/`log_bundle_bytes_sent` log replays (resume, rejoin) |
| `lag_ticks` | relay tick minus the client's executed tick, at each ping (arrival lateness of the client's progress) |
| `checksums` | `reports`; `lateness_ticks` (relay tick at arrival minus the reported tick); `told_to_rejoin`; `flagged`; `late_mismatches` |
| `connection` | `connects`, `disconnects`, `grace_used_ms` (time disconnected inside the grace period), `longest_absence_ms`, `left_by_grace`, `left_by_quit`, `left_tick` |
| `rtt_us` | optional: round trips the host measured itself (`TurnSequencer::transportRoundTrip`). The online relay pings each match connection over WebSocket every `GLOB2_RELAY_RTT_PING_MS` (2 s); the LAN host measures none and leaves it out |

The LAN host writes this file whenever it writes `lan-last.g2mr`.

`Turn::RelayNetworkTotals` accumulates finished matches for a relay process and writes
Prometheus text: counters `glob2_relay_net_{orders_sequenced,orders_deferred,
order_defer_ticks,orders_dropped,voice_sequenced,bundles_sent,bundle_bytes_sent,
log_bundles_sent,log_bundle_bytes_sent,checksum_reports,arbitrations,
arbitrations_unanimous,arbitrations_majority,arbitrations_flagged,
arbitrations_timed_out,disconnects,grace_expiries,grace_used_seconds,rejoins}_total`,
the gauge `glob2_relay_net_peak_pending_bytes`, and summaries (`quantile` 0.5/0.95/0.99,
`_sum`, `_count`) `glob2_relay_net_{lag_ticks,checksum_lateness_ticks,order_defer_ticks,rtt_us}`.

## Verifier: `RecordNetworkSummary`

`--verify-match` adds `network` to `result.json`: per human seat the facts the match
record proves, with no wall-clock values, so the same record verifies to the same bytes
everywhere: `orders`, `order_bytes` (quit excluded), `checksum_reports`, `connects`,
`disconnects`, `reconnects`, `disconnected_ticks`, `told_to_rejoin`, `resynced`,
`flagged`, and `left_tick`/`left_by` (`quit` or `grace`). The verifier's own session
replays a record and has no network to measure, so no `ClientNetworkSummary` is written
for it.

## Pacing: network sleep

`pacing.network_sleep` times a game loop's wait while `Engine::waitingOnNetwork()`:
for turn games, `TurnSession::waitingOnNetwork()` (the authorized horizon is used up,
not reloading, not ended); for legacy games, the previous tick was not ready. Every
other in-game wait is `pacing.sleep`, so a turn game's normal jitter-buffer pacing is no
longer counted as network time. Both the native `run()` loop and the screen-stack host
(`GameSessionScreen::executionWait`) time these waits; menus are not timed. The browser
host schedules frames itself and records neither.

## Relay and platform integration

- **Relay:** `glob2-relay` keeps one `Turn::RelayNetworkTotals` per process
  (`RelayMetrics::matchNetwork`), adds each match's `SequencerTelemetry` when the match
  ends, and appends its Prometheus text to `/metrics`. `RelayMatchEnded` carries
  `sequencer.networkSummary()` as `network`. The `G2MR` record format is unchanged.
- **Contract:** `platform/packages/protocol/src/network.ts` defines
  `RelayNetworkSummary` and `ClientNetworkSummary` (JSON Schemas and fixtures under
  `fixtures/`). Both are open objects: a newer engine may add fields within version 1.
  `RelayMatchEnded.network` is optional, and the platform drops a summary that does
  not validate rather than refuse the end report.
- **Platform:** the match-end intake stores each seat's entry in
  `match_participants.network` (migration 0009); `MatchDetail.network` condenses it
  for the match page's connection panel
  ([history and web](../multiplayer/history-and-web.md)). Its words and thresholds
  (Ping, Behind, Good/Fair/Poor) are the shared table that the in-game panel uses too
  ([connection quality](../multiplayer/connection-quality.md)).
- **Live, in game:** the relay also sends each seat's smoothed round trip to version-2
  clients (`SeatLatency`) for the connection panel. That is a live display, not
  telemetry. The quick-match card's region probe round trip is an estimate made before
  the relay is chosen.
- **Client context:** online clients take `networkKind = "online"`, `relayId` and
  `relayRegion` from `MatchAssignment` (`relayId`/`relayRegion` are optional there;
  `glob2 --turn-client` reads them).
- **Client upload:** none. Whether and how to collect `ClientNetworkSummary` is
  undecided; the file next to the replay is the only output.

## Verifying

```sh
scons -j3 CCACHE=1 release=1 server=0 tests
python3 test/run_tests.py --binary unit --filter 'TurnTelemetry/*' --filter 'TurnHarness/*'
python3 test/run_tests.py --binary engine --filter 'TurnEngineHarness/*'
```

`TurnHarness` "network telemetry follows injected latency, jitter, loss, an outage and
a rejoin" and the `TurnEngineHarness` case above write sample outputs under
`artifacts/tests/`.
