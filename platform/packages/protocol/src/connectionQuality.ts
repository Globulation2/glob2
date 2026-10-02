// The one table of connection-quality thresholds (docs/multiplayer/connection-quality.md).
//
// Every place that rates a connection reads it: the in-game connection panel (C++,
// src/gui/ConnectionQuality.h, desktop and phone), the match-start checklist and the
// quick-match card in the game, the match page's Connection table and
// ParticipantNetwork.quality from the API. The fixture generator writes this table and
// its sample cases to fixtures/connection-quality.json; the C++ unit test checks its
// copy of the table against that file, so the two cannot drift apart silently.
//
// Presentation only: nothing simulated, rated or verified reads these numbers.

/** The quantities a player sees, all in milliseconds. */
export type ConnectionMetric = 'ping' | 'delay' | 'behind';
export type ConnectionRating = 'good' | 'fair' | 'poor';

export interface ConnectionMetricInfo {
  /** The word shown next to the number ("Ping 42 ms"). */
  label: string;
  /** How the number is written: milliseconds, or seconds with one decimal. */
  unit: 'ms' | 's';
  /** A value at or above `fair` is fair; at or above `poor` it is poor. */
  fairMs: number;
  poorMs: number;
  description: string;
}

export const CONNECTION_METRICS: Readonly<Record<ConnectionMetric, ConnectionMetricInfo>> = {
  ping: {
    label: 'Ping',
    unit: 'ms',
    fairMs: 150,
    poorMs: 300,
    description:
      'Round trip between a player and the relay. Before a match (quick match, start checklist) it is a probe of the region, an estimate.',
  },
  delay: {
    label: 'Delay',
    unit: 'ms',
    fairMs: 200,
    poorMs: 400,
    description:
      'Your input delay: from your click until it happens, for everyone. Half your ping plus the buffer your game holds against jitter.',
  },
  behind: {
    label: 'Behind',
    unit: 's',
    fairMs: 1000,
    poorMs: 2000,
    description:
      'How far a player’s game runs behind the match clock. Under a second is normal (it includes their delay); the relay marks a player slow from 2 s.',
  },
};

export const CONNECTION_RATING_LABELS: Readonly<Record<ConnectionRating, string>> = {
  good: 'Good',
  fair: 'Fair',
  poor: 'Poor',
};

/** Rates a value of a metric (milliseconds). */
export function rateConnection(metric: ConnectionMetric, valueMs: number): ConnectionRating {
  const t = CONNECTION_METRICS[metric];
  return valueMs >= t.poorMs ? 'poor' : valueMs >= t.fairMs ? 'fair' : 'good';
}

/** The worse of several ratings. */
export function worstRating(...ratings: ConnectionRating[]): ConnectionRating {
  return ratings.includes('poor') ? 'poor' : ratings.includes('fair') ? 'fair' : 'good';
}

/**
 * A value with its unit, the way every surface writes it: "42 ms", "1.4 s".
 * Seconds keep one decimal below 10 s and none above.
 */
export function formatConnectionValue(metric: ConnectionMetric, valueMs: number): string {
  if (CONNECTION_METRICS[metric].unit === 'ms') return `${Math.round(valueMs)} ms`;
  const s = valueMs / 1000;
  return s < 10 ? `${s.toFixed(1)} s` : `${Math.round(s)} s`;
}

/** Sample values and their expected ratings; both implementations must agree. */
export const CONNECTION_QUALITY_CASES: readonly {
  metric: ConnectionMetric;
  valueMs: number;
  rating: ConnectionRating;
  text: string;
}[] = (
  [
    ['ping', 0],
    ['ping', 42],
    ['ping', 149],
    ['ping', 150],
    ['ping', 299],
    ['ping', 300],
    ['ping', 1332],
    ['delay', 171],
    ['delay', 199],
    ['delay', 200],
    ['delay', 399],
    ['delay', 400],
    ['behind', 760],
    ['behind', 999],
    ['behind', 1000],
    ['behind', 1999],
    ['behind', 2000],
    ['behind', 12_400],
  ] as const
).map(([metric, valueMs]) => ({
  metric,
  valueMs,
  rating: rateConnection(metric, valueMs),
  text: formatConnectionValue(metric, valueMs),
}));
