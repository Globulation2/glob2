// The match page's connection panel: condensing a stored RelayNetworkSummary
// seat entry (history/network.ts) into MatchDetail.network.
import { describe, expect, it } from 'vitest';
import { checkDocument } from '@glob2/protocol';
import { participantNetwork, reportTickRate } from '../src/history/network.ts';

const dist = (p50: number, p95: number, count = 50) => ({ count, mean: p50, p50, p95, max: p95 });

function seat(overrides: Record<string, unknown> = {}) {
  return {
    seat: 0,
    orders: { sequenced: 200, bytes: 3000, deferred: 2, defer_ticks: dist(1, 1, 2) },
    voice: { sequenced: 0, bytes: 0 },
    traffic: {},
    lag_ticks: dist(6, 8),
    checksums: { reports: 100, lateness_ticks: dist(6, 8), told_to_rejoin: 0 },
    connection: {
      connects: 1,
      disconnects: 0,
      grace_used_ms: 0,
      longest_absence_ms: 0,
      left_by_grace: false,
      left_by_quit: true,
    },
    rtt_us: dist(40_000, 60_000),
    ...overrides,
  };
}

describe('participantNetwork', () => {
  it('converts microseconds and ticks to milliseconds and labels a clean game good', () => {
    const n = participantNetwork(0, seat())!;
    expect(n).toEqual({
      seat: 0,
      quality: 'good',
      rttMs: { p50: 40, p95: 60 },
      lagMs: { p50: 240, p95: 320 },
      disconnects: 0,
      offlineMs: 0,
      ordersSequenced: 200,
      ordersDeferred: 2,
      rejoins: 0,
      leftBy: 'quit',
    });
    // The page document stays valid.
    const issues = checkDocument('MatchDetail', {
      match: {
        id: '7e3c1d2b-9a8f-4e6d-8c5b-4a3f2e1d0c9b',
        simVersion: { versionMinor: 125, netProtocol: 49, dataHash: 'a'.repeat(64) },
        origin: 'room',
        rated: false,
        status: 'ended',
        verification: 'verified',
        mapHash: 'a'.repeat(64),
        participants: [],
      },
      setup: {},
      teams: [],
      artifacts: [],
      network: [n],
    }).issues;
    expect(issues.filter((i) => !i.path.startsWith('/setup'))).toEqual([]);
  });

  it('uses the tick rate of the report', () => {
    expect(participantNetwork(0, seat(), 50_000)!.lagMs).toEqual({ p50: 120, p95: 160 });
    expect(reportTickRate({ network: { tick_rate_millihz: 50_000 } })).toBe(50_000);
    expect(reportTickRate(null)).toBe(25_000);
  });

  it('labels fair and poor connections by any one condition', () => {
    const quality = (overrides: Record<string, unknown>) =>
      participantNetwork(0, seat(overrides))!.quality;
    expect(quality({ rtt_us: dist(100_000, 250_000) })).toBe('fair');
    expect(quality({ rtt_us: dist(200_000, 450_000) })).toBe('poor');
    expect(quality({ lag_ticks: dist(10, 30) })).toBe('fair'); // 1.2 s behind
    expect(quality({ lag_ticks: dist(10, 60) })).toBe('poor');
    expect(quality({ orders: { sequenced: 100, deferred: 6, defer_ticks: dist(1, 2, 6) } })).toBe(
      'fair',
    );
    const connection = seat().connection;
    expect(quality({ connection: { ...connection, disconnects: 1, grace_used_ms: 900 } })).toBe(
      'fair',
    );
    expect(quality({ connection: { ...connection, disconnects: 3 } })).toBe('poor');
    expect(quality({ connection: { ...connection, disconnects: 1, grace_used_ms: 31_000 } })).toBe(
      'poor',
    );
    expect(quality({ checksums: { reports: 10, told_to_rejoin: 1 } })).toBe('poor');
  });

  it('leaves out what was not measured and skips unreadable rows', () => {
    const lan = participantNetwork(1, seat({ rtt_us: undefined, lag_ticks: dist(0, 0, 0) }))!;
    expect(lan.rttMs).toBeUndefined();
    expect(lan.lagMs).toBeUndefined();
    expect(
      participantNetwork(
        0,
        seat({ connection: { ...seat().connection, left_by_quit: false, left_by_grace: true } }),
      )!.leftBy,
    ).toBe('grace');
    expect(participantNetwork(0, null)).toBeUndefined();
    expect(participantNetwork(0, { schema_version: 9 })).toBeUndefined();
  });
});
