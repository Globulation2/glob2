// Network telemetry of turn games (docs/development/network-telemetry.md): what the
// relay measured per seat (RelayNetworkSummary, carried by RelayMatchEnded.network)
// and what a client measured about its own session (ClientNetworkSummary).
//
// Both are produced by the engine (src/net/turn/TurnTelemetry.cpp) and versioned by
// `schema_version`. Adding an optional field keeps version 1; renaming, removing or
// changing the meaning of a field requires version 2. The objects are Open (unknown
// properties allowed) even where a relay sends them: telemetry must never be the
// reason an end-of-match report is refused, and a newer engine may add fields.
//
// ClientNetworkSummary is a foundation only: the game writes it next to its replay
// and nothing uploads it. No endpoint, table or upload exists until collection (and
// its privacy default) is decided.
import { Type, type Static } from 'typebox';
import { Open } from './common.ts';

const Count = Type.Integer({ minimum: 0 });
/** Turn-protocol seat (TurnProtocol.h MAX_SEATS = 32). */
const TurnSeat = Type.Integer({ minimum: 0, maximum: 31 });

/** A histogram summary: `{count, mean, p50, p95, max}`; all 0 when count is 0. */
export const NetworkDistribution = Open(
  {
    count: Count,
    mean: Type.Number({ minimum: 0 }),
    p50: Count,
    p95: Count,
    max: Count,
  },
  { description: 'Turn::Histogram summary. Quantiles are within about 3% of a recorded value.' },
);
export type NetworkDistribution = Static<typeof NetworkDistribution>;

// ------------------------------------------------------------------ relay

export const RelayNetworkSeat = Open(
  {
    seat: TurnSeat,
    orders: Open({
      sequenced: Count,
      bytes: Count,
      deferred: Type.Integer({
        minimum: 0,
        description:
          'Orders (and voice) placed later than the earliest tick by the one-order-per-seat-per-tick rule or the tick byte budget.',
      }),
      defer_ticks: NetworkDistribution,
      duplicates_ignored: Count,
      dropped: Count,
      flood_rejections: Count,
      max_queued_ahead_ticks: Count,
    }),
    voice: Open({ sequenced: Count, bytes: Count }),
    traffic: Open({
      frames_received: Count,
      bytes_received: Count,
      bundles_sent: Count,
      bundle_bytes_sent: Count,
      log_bundles_sent: Count,
      log_bundle_bytes_sent: Count,
    }),
    lag_ticks: Type.Union([NetworkDistribution], {
      description:
        "Relay tick minus the client's executed tick at each ping: how far behind the relay clock the client runs.",
    }),
    checksums: Open({
      reports: Count,
      lateness_ticks: NetworkDistribution,
      told_to_rejoin: Count,
      flagged: Count,
      late_mismatches: Count,
    }),
    connection: Open({
      connects: Count,
      disconnects: Count,
      grace_used_ms: Type.Integer({
        minimum: 0,
        description: 'Time disconnected inside the reconnect grace period.',
      }),
      longest_absence_ms: Count,
      left_by_grace: Type.Boolean(),
      left_by_quit: Type.Boolean(),
      left_tick: Type.Optional(Count),
    }),
    rtt_us: Type.Optional(
      Type.Union([NetworkDistribution], {
        description:
          'Round trip the relay measured itself (WebSocket ping every 2 s by default). Absent where the host measures none (LAN).',
      }),
    ),
  },
  { description: 'One human seat as the relay saw it.' },
);
export type RelayNetworkSeat = Static<typeof RelayNetworkSeat>;

export const RelayNetworkSummary = Open(
  {
    schema: Type.Literal('RelayNetworkSummary'),
    schema_version: Type.Literal(1),
    tick_rate_millihz: Type.Integer({ minimum: 1 }),
    end_tick: Count,
    duration_ms: Count,
    bundles: Open({ broadcast: Count, bytes: Count }),
    arbitration: Open({
      ticks: Count,
      unanimous: Count,
      majority: Count,
      flagged: Count,
      timed_out: Count,
    }),
    peak_backlog: Open({ pending_ticks: Count, pending_entries: Count, pending_bytes: Count }),
    rejected_peers: Count,
    seats: Type.Array(RelayNetworkSeat, { maxItems: 32 }),
  },
  {
    description:
      "The relay's per-seat network summary of one match (TurnSequencer::networkSummary), version 1.",
  },
);
export type RelayNetworkSummary = Static<typeof RelayNetworkSummary>;

// ----------------------------------------------------------------- client

const PresenceStateCounts = Type.Record(Type.String({ maxLength: 32 }), Count, {
  description:
    'Per presence state (not_connected, connected, lagging, reconnecting, resyncing, left).',
});

export const ClientNetworkPoint = Open({
  start_us: Count,
  end_us: Count,
  start_tick: Count,
  end_tick: Count,
  rtt_us: NetworkDistribution,
  jitter_us: NetworkDistribution,
  input_delay_us: NetworkDistribution,
  buffered_ticks: NetworkDistribution,
  target_ticks: NetworkDistribution,
  stall_us: NetworkDistribution,
  bytes_sent: Count,
  bytes_received: Count,
  frames_sent: Count,
  frames_received: Count,
  ticks_executed: Count,
  live_ticks: Count,
  ticks_faster: Count,
  ticks_slower: Count,
  mean_nudge: Type.Number(),
  catch_up_ticks: Count,
  reconnects: Count,
  downtime_us: Count,
  orders_submitted: Count,
  voice_sent: Count,
  voice_received: Count,
  presence_transitions: Count,
});
export type ClientNetworkPoint = Static<typeof ClientNetworkPoint>;

export const ClientNetworkSummary = Open(
  {
    schema: Type.Literal('ClientNetworkSummary'),
    schema_version: Type.Literal(1),
    match: Open({
      sim_version: Type.String({ maxLength: 128 }),
      platform: Type.String({ maxLength: 64, description: 'SDL platform name.' }),
      transport: Type.Union([Type.Literal('lan'), Type.Literal('online')]),
      relay_id: Type.Union([Type.String({ maxLength: 64 }), Type.Null()]),
      relay_region: Type.Union([Type.String({ maxLength: 32 }), Type.Null()]),
      seat: TurnSeat,
      human_seat_mask: Type.Integer({ minimum: 0, maximum: 4294967295 }),
      players: Count,
      tick_rate_millihz: Type.Integer({ minimum: 1 }),
      final_tick: Count,
    }),
    elapsed_us: Count,
    rtt_us: NetworkDistribution,
    jitter_us: NetworkDistribution,
    input_delay_us: NetworkDistribution,
    input_delay_unmatched: Count,
    jitter_buffer: Open({ buffered_ticks: NetworkDistribution, target_ticks: NetworkDistribution }),
    tick_rate_nudge: Open({
      live_ticks: Count,
      ticks_faster: Count,
      ticks_slower: Count,
      mean: Type.Number(),
      max_abs: Type.Number({ minimum: 0 }),
    }),
    stalls: Open({
      count: Count,
      total_us: Count,
      longest_us: Count,
      duration_us: NetworkDistribution,
    }),
    catch_up: Open({ episodes: Count, ticks: Count, wall_us: Count }),
    reconnects: Open({
      count: Count,
      downtime_us: Count,
      longest_downtime_us: Count,
      down_now: Type.Boolean(),
    }),
    reloads: Open({
      count: Count,
      load_us: Count,
      fast_forward_ticks: Count,
      fast_forward_us: Count,
    }),
    traffic: Open({
      frames_sent: Count,
      bytes_sent: Count,
      frames_received: Count,
      bytes_received: Count,
      bundles_received: Count,
      bundle_bytes: Count,
      bundle_entries: Count,
    }),
    orders: Open({
      submitted: Count,
      frames_sent: Count,
      resent: Count,
      queued_offline: Count,
      dropped_local: Count,
      outstanding_max: Count,
    }),
    voice: Open({ sent: Count, sent_bytes: Count, received: Count, received_bytes: Count }),
    desync: Open({ rejoins: Count, flagged: Count, resync_requests: Count }),
    presence: Open({
      transitions: Count,
      seats: Type.Array(
        Open({
          seat: TurnSeat,
          final_state: Type.String({ maxLength: 32 }),
          transitions: PresenceStateCounts,
          time_us: PresenceStateCounts,
        }),
        { maxItems: 32 },
      ),
    }),
    ticks: Open({ executed: Count, live: Count }),
    order_validation: Type.Union([
      Type.Null(),
      Open({
        seats: Type.Array(
          Open({
            seat: TurnSeat,
            accepted: Count,
            stale: Count,
            rejected: Count,
            voice_rejected: Count,
            rejected_by_reason: Type.Record(Type.String({ maxLength: 64 }), Count),
          }),
          { maxItems: 32 },
        ),
      }),
    ]),
    series: Type.Optional(
      Open({
        interval_us: Type.Integer({ minimum: 1 }),
        dropped_points: Count,
        points: Type.Array(ClientNetworkPoint, { maxItems: 4320 }),
      }),
    ),
  },
  {
    description:
      "A client's own measurements of one turn game (Turn::clientNetworkSummary), version 1. Written next to the replay; not uploaded.",
  },
);
export type ClientNetworkSummary = Static<typeof ClientNetworkSummary>;
