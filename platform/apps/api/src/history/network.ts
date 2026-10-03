// The match page's "connection" panel: each human player's entry of the relay's
// network summary (RelayNetworkSummary v1, match_participants.network since
// 0009) condensed into a few numbers and a rough quality label.
import {
  rateConnection,
  worstRating,
  type ConnectionRating,
  type ParticipantNetwork,
  type RelayNetworkSeat,
} from '@glob2/protocol';

/** GAME_TICKS_PER_SECOND at the default rate; reports carry their own rate. */
const DEFAULT_TICK_RATE_MILLIHZ = 25000;

/**
 * Thresholds behind ParticipantNetwork.quality beyond ping and behind, which
 * use the shared table (@glob2/protocol connectionQuality.ts, the same one the
 * in-game panel uses) on their typical (median) values. Presentation only:
 * nothing else (ratings, verification) reads the label. `fair` and `poor` are
 * reached by any one of their conditions.
 */
export const QUALITY_THRESHOLDS = {
  fair: { disconnects: 1, deferredShare: 0.05 },
  poor: { disconnects: 3, offlineMs: 30_000, rejoins: 1 },
} as const;

function ms(us: number): number {
  return Math.round(us / 1000);
}

function count(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) && value >= 0 ? Math.floor(value) : 0;
}

/**
 * Condenses one stored seat entry; undefined when it is not a readable
 * RelayNetworkSummary seat (the intake validated it, but rows outlive schemas).
 */
export function participantNetwork(
  seat: number,
  stored: unknown,
  tickRateMilliHz: number = DEFAULT_TICK_RATE_MILLIHZ,
): ParticipantNetwork | undefined {
  if (!stored || typeof stored !== 'object') return undefined;
  const s = stored as Partial<RelayNetworkSeat>;
  if (!s.connection || !s.orders) return undefined;
  const rate = tickRateMilliHz > 0 ? tickRateMilliHz : DEFAULT_TICK_RATE_MILLIHZ;
  const tickMs = (ticks: number) => Math.round((ticks * 1_000_000) / rate);
  const rttMs =
    s.rtt_us && count(s.rtt_us.count) > 0
      ? { p50: ms(count(s.rtt_us.p50)), p95: ms(count(s.rtt_us.p95)) }
      : undefined;
  const lagMs =
    s.lag_ticks && count(s.lag_ticks.count) > 0
      ? { p50: tickMs(count(s.lag_ticks.p50)), p95: tickMs(count(s.lag_ticks.p95)) }
      : undefined;
  const disconnects = count(s.connection.disconnects);
  const offlineMs = count(s.connection.grace_used_ms);
  const ordersSequenced = count(s.orders.sequenced);
  const ordersDeferred = count(s.orders.deferred);
  const rejoins = count(s.checksums?.told_to_rejoin);
  const leftBy = s.connection.left_by_quit
    ? ('quit' as const)
    : s.connection.left_by_grace
      ? ('grace' as const)
      : undefined;

  const poor = QUALITY_THRESHOLDS.poor;
  const fair = QUALITY_THRESHOLDS.fair;
  const deferredShare = ordersSequenced > 0 ? ordersDeferred / ordersSequenced : 0;
  const ratings: ConnectionRating[] = [];
  if (rttMs) ratings.push(rateConnection('ping', rttMs.p50));
  if (lagMs) ratings.push(rateConnection('behind', lagMs.p50));
  if (disconnects >= poor.disconnects || offlineMs >= poor.offlineMs || rejoins >= poor.rejoins)
    ratings.push('poor');
  else if (disconnects >= fair.disconnects || deferredShare > fair.deferredShare)
    ratings.push('fair');
  const quality = worstRating(...ratings);

  return {
    seat,
    quality,
    ...(rttMs ? { rttMs } : {}),
    ...(lagMs ? { lagMs } : {}),
    disconnects,
    offlineMs,
    ordersSequenced,
    ordersDeferred,
    rejoins,
    ...(leftBy ? { leftBy } : {}),
  };
}

/** The tick rate the report was measured at, from matches.end_report. */
export function reportTickRate(endReport: unknown): number {
  const network = (endReport as { network?: { tick_rate_millihz?: unknown } } | null)?.network;
  const rate = network?.tick_rate_millihz;
  return typeof rate === 'number' && rate > 0 ? rate : DEFAULT_TICK_RATE_MILLIHZ;
}
