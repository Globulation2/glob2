// Verified results into history: team statistics and timelines, match
// artifacts, and the aggregate views of migration 0004.
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import type { TeamTimelinePoint, VerifyVerdict } from '@glob2/protocol';
import { handleEngineJobResult } from '../src/ratings/apply.ts';
import { HASH, createAccount, createMatch, createVerifyJob, resultPayload } from './support.ts';

let database: TestDatabase;

beforeAll(async () => {
  database = await createTestDatabase({ role: 'worker' });
  await database.db
    .insertInto('blobs')
    .values({ sha256: HASH, size: 10, content_type: 'application/octet-stream', storage_key: 'k' })
    .execute();
});

afterAll(async () => {
  await database?.drop();
});

function timeline(scale: number, samples = 3): TeamTimelinePoint[] {
  return Array.from({ length: samples }, (_, i) => ({
    tick: i * 512,
    units: (4 + i) * scale,
    buildings: (1 + i) * scale,
    prestige: i * scale,
    hp: 1000 * scale,
    attack: 10 * i,
    defense: 0,
  }));
}

function verdictWithHistory(
  outcomes: ('won' | 'lost')[],
  scale: number,
  finalTick: number,
): VerifyVerdict {
  return {
    verdict: 'verified',
    outcome: {
      finalTick,
      teams: outcomes.map((outcome, team) => ({
        team,
        outcome,
        prestige: 5 * team,
        ...(outcome === 'lost' ? { eliminatedTick: finalTick - 10 } : {}),
        statistics: {
          units: 10 * scale + team,
          workers: 6,
          buildings: 3,
          alive: outcome === 'won' ? 1 : 0,
        },
        timeline: timeline(scale + team),
      })),
      resultHash: HASH,
      replayHash: HASH,
    },
  };
}

describe('verified history', () => {
  it('stores team statistics, timelines and artifacts, and aggregates them in views', async () => {
    const alice = await createAccount(database.db, 'Alice');
    const bob = await createAccount(database.db, 'Bob');

    const first = await createMatch(
      database.db,
      [
        { side: 0, accountId: alice },
        { side: 1, accountId: bob },
      ],
      {
        finalTick: 20_000,
      },
    );
    const second = await createMatch(
      database.db,
      [
        { side: 0, accountId: alice },
        { side: 1, ai: 'nicowar' },
      ],
      {
        finalTick: 40_000,
      },
    );
    const room = await createMatch(
      database.db,
      [
        { side: 0, accountId: alice },
        { side: 1, accountId: bob },
      ],
      {
        queueId: null,
        rated: false,
        finalTick: 30_000,
      },
    );
    for (const [matchId, outcomes, scale, ticks] of [
      [first, ['won', 'lost'], 1, 20_000],
      [second, ['lost', 'won'], 3, 40_000],
      [room, ['won', 'lost'], 2, 30_000],
    ] as const) {
      const jobId = await createVerifyJob(database.db, matchId);
      expect(
        await handleEngineJobResult(
          database.db,
          resultPayload(jobId, verdictWithHistory([...outcomes], scale, ticks)),
        ),
      ).toBe(true);
    }

    const stats = await database.db
      .selectFrom('match_team_stats')
      .selectAll()
      .where('match_id', '=', first)
      .orderBy('team')
      .execute();
    expect(stats.map((s) => [s.team, s.outcome, s.eliminated_tick])).toEqual([
      [0, 'won', null],
      [1, 'lost', 19_990],
    ]);
    expect(stats[0]!.statistics).toEqual({ units: 10, workers: 6, buildings: 3, alive: 1 });
    expect(stats[1]!.timeline).toEqual(timeline(2));

    const artifacts = await database.db
      .selectFrom('match_artifacts')
      .select(['kind', 'blob_sha256'])
      .where('match_id', '=', first)
      .orderBy('kind')
      .execute();
    expect(artifacts).toEqual([
      { kind: 'record', blob_sha256: HASH },
      { kind: 'replay', blob_sha256: HASH },
      { kind: 'result', blob_sha256: HASH },
    ]);

    const results = await database.db
      .selectFrom('match_results_view')
      .selectAll()
      .where('account_id', '=', alice)
      .orderBy('final_tick')
      .execute();
    expect(results.map((r) => [r.queue_id, r.generator_id, r.outcome, r.won])).toEqual([
      ['ranked-1v1', 'even-ground', 'won', true],
      [null, 'even-ground', 'won', true],
      ['ranked-1v1', 'even-ground', 'lost', false],
    ]);

    const rates = await database.db
      .selectFrom('recent_win_rates_view')
      .select(['dimension', 'key', 'games', 'wins', 'win_rate'])
      .where('account_id', '=', alice)
      .orderBy(['dimension', 'key'])
      .execute();
    expect(rates).toEqual([
      { dimension: 'generator', key: 'even-ground', games: 3, wins: 2, win_rate: 0.6667 },
      { dimension: 'map', key: HASH, games: 3, wins: 2, win_rate: 0.6667 },
      { dimension: 'queue', key: 'ranked-1v1', games: 2, wins: 1, win_rate: 0.5 },
      { dimension: 'queue', key: 'room', games: 1, wins: 1, win_rate: 1 },
    ]);
    // AI seats are aggregated per rating entity / AI too (account_id null).
    const aiRows = await database.db
      .selectFrom('recent_win_rates_view')
      .selectAll()
      .where('account_id', 'is', null)
      .where('dimension', '=', 'queue')
      .execute();
    expect(aiRows).toHaveLength(1);

    const lengths = await database.db
      .selectFrom('recent_game_lengths_view')
      .selectAll()
      .orderBy(['dimension', 'key'])
      .execute();
    expect(lengths.map((l) => [l.dimension, l.key, l.games, l.mean_ticks, l.median_ticks])).toEqual(
      [
        ['generator', 'even-ground', 3, 30_000, 30_000],
        ['queue', 'ranked-1v1', 2, 30_000, 30_000],
        ['queue', 'room', 1, 30_000, 30_000],
      ],
    );

    const points = await database.db
      .selectFrom('team_timeline_view')
      .selectAll()
      .where('match_id', '=', first)
      .where('team', '=', 0)
      .orderBy('tick')
      .execute();
    expect(points.map((p) => [p.tick, p.units, p.buildings])).toEqual([
      [0, 4, 1],
      [512, 5, 2],
      [1024, 6, 3],
    ]);

    // Alice's curve in each match against her own average at the same tick:
    // units at tick 512 were 5 (scale 1), 15 (scale 3) and 10 (scale 2).
    const curve = await database.db
      .selectFrom('account_economy_curves_view')
      .selectAll()
      .where('account_id', '=', alice)
      .where('tick', '=', 512)
      .orderBy('units')
      .execute();
    expect(curve.map((c) => [c.match_id, c.units, c.average_units, c.games_at_tick])).toEqual([
      [first, 5, 10, 3],
      [room, 10, 10, 3],
      [second, 15, 10, 3],
    ]);
  });

  it('keeps working with verdicts that carry no history (older agents)', async () => {
    const carol = await createAccount(database.db, 'Carol');
    const matchId = await createMatch(database.db, [
      { side: 0, accountId: carol },
      { side: 1, ai: 'cortex' },
    ]);
    const jobId = await createVerifyJob(database.db, matchId);
    const verdict: VerifyVerdict = {
      verdict: 'verified',
      outcome: {
        finalTick: 100,
        teams: [
          { team: 0, outcome: 'won', prestige: 0 },
          { team: 1, outcome: 'lost', prestige: 0 },
        ],
        resultHash: 'ee'.repeat(32), // not registered in blobs: skipped
        replayHash: HASH,
      },
    };
    expect(await handleEngineJobResult(database.db, resultPayload(jobId, verdict))).toBe(true);
    const stats = await database.db
      .selectFrom('match_team_stats')
      .select(['statistics', 'timeline'])
      .where('match_id', '=', matchId)
      .execute();
    expect(stats).toEqual([
      { statistics: {}, timeline: [] },
      { statistics: {}, timeline: [] },
    ]);
    const kinds = await database.db
      .selectFrom('match_artifacts')
      .select('kind')
      .where('match_id', '=', matchId)
      .orderBy('kind')
      .execute();
    expect(kinds.map((k) => k.kind)).toEqual(['record', 'replay']);
  });
});
