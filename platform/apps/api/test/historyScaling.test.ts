// The public match page and match lists on seeded history: their cost must
// follow what they show, not how much history the instance holds. Checked
// with the planner (EXPLAIN) and with Postgres' own row counters, plus the
// result against the original view-based definition.
import { randomUUID } from 'node:crypto';
import pg from 'pg';
import { sql } from 'kysely';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { HistoryService } from '../src/history/service.ts';

const SIM = `125-49-${'ab'.repeat(32)}`;
const HASH = 'cd'.repeat(32);
const ACCOUNTS = 400;
const MATCHES = 600;
const POINTS = 40;

let database: TestDatabase;
let client: pg.Client;
const accountIds: string[] = [];
const matchIds: string[] = [];

function timeline(seed: number) {
  return JSON.stringify(
    Array.from({ length: POINTS }, (_, i) => ({
      tick: i * 512,
      units: (seed + i) % 50,
      buildings: (seed * 3 + i) % 20,
      prestige: seed + i,
    })),
  );
}

beforeAll(async () => {
  database = await createTestDatabase();
  const db = database.db;
  for (let i = 0; i < ACCOUNTS; i++) accountIds.push(randomUUID());
  await db
    .insertInto('accounts')
    .values(accountIds.map((id, i) => ({ id, kind: 'registered', display_name: `P${i}` })))
    .execute();
  for (let i = 0; i < MATCHES; i++) matchIds.push(randomUUID());
  await db
    .insertInto('matches')
    .values(
      matchIds.map((id, i) => ({
        id,
        sim_version: SIM,
        origin: 'queue' as const,
        queue_id: 'ranked-1v1',
        status: 'ended' as const,
        verification: 'verified' as const,
        setup: '{}',
        seed: 1,
        map_hash: HASH,
        final_tick: 20_000,
        ended_at: new Date(Date.now() - (i + 1) * 60_000),
      })),
    )
    .execute();
  // Match i: players i and i+1 (mod ACCOUNTS), so everyone has 2 to 4 matches.
  await db
    .insertInto('match_participants')
    .values(
      matchIds.flatMap((match_id, i) =>
        [0, 1].map((seat) => ({
          match_id,
          seat,
          team: seat,
          kind: 'human' as const,
          account_id: accountIds[(i + seat) % ACCOUNTS]!,
          display_name: `P${(i + seat) % ACCOUNTS}`,
          outcome: seat === 0 ? ('won' as const) : ('lost' as const),
        })),
      ),
    )
    .execute();
  await db
    .insertInto('match_team_stats')
    .values(
      matchIds.flatMap((match_id, i) =>
        [0, 1].map((team) => ({
          match_id,
          team,
          outcome: team === 0 ? ('won' as const) : ('lost' as const),
          timeline: timeline(i * 2 + team),
        })),
      ),
    )
    .execute();
  // Verify jobs (one per match) and plenty of unrelated engine jobs.
  await db
    .insertInto('engine_jobs')
    .values([
      ...matchIds.map((match_id) => ({
        kind: 'verify-match' as const,
        sim_version: SIM,
        payload: JSON.stringify({ matchId: match_id }),
        match_id,
        status: 'succeeded' as const,
        result: JSON.stringify({ verdict: 'verified', clients: [1] }),
        completed_at: new Date(),
      })),
      ...Array.from({ length: 4000 }, () => ({
        kind: 'generate-map' as const,
        sim_version: SIM,
        payload: '{}',
        status: 'succeeded' as const,
        completed_at: new Date(),
      })),
    ])
    .execute();
  // Queue tickets: many finished, few matched in the last day.
  await db
    .insertInto('queue_tickets')
    .values(
      Array.from({ length: 4000 }, (_, i) => ({
        queue_id: i % 2 === 0 ? 'ranked-1v1' : 'casual-1v1',
        account_id: accountIds[i % ACCOUNTS]!,
        sim_version: SIM,
        status: i % 50 === 0 ? ('matched' as const) : ('expired' as const),
        created_at: new Date(Date.now() - (i + 10) * 3600_000),
        updated_at: new Date(Date.now() - i * 3600_000),
      })),
    )
    .execute();
  await sql`ANALYZE`.execute(db);
  // One backend for the counters below, reading them without a cached snapshot.
  client = new pg.Client({ connectionString: database.url });
  await client.connect();
  await client.query(`SET stats_fetch_consistency = none`);
}, 60_000);

afterAll(async () => {
  await client?.end();
  await database?.drop();
});

/** Rows of match_team_stats that `query` reads, from Postgres' table counters. */
async function teamStatRowsRead(query: string, params: unknown[]): Promise<number> {
  const counters = async () => {
    await client.query('SELECT pg_stat_force_next_flush()');
    // A statement in its own transaction flushes this backend's counters.
    await client.query('SELECT 1');
    const row = await client.query<{ n: string }>(
      `SELECT (COALESCE(seq_tup_read, 0) + COALESCE(idx_tup_fetch, 0))::text AS n
       FROM pg_stat_user_tables WHERE relname = 'match_team_stats'`,
    );
    return Number(row.rows[0]?.n ?? 0);
  };
  const before = await counters();
  await client.query(query, params);
  return (await counters()) - before;
}

async function plan(query: string, params: unknown[] = []): Promise<string> {
  const result = await client.query<{ 'QUERY PLAN': unknown }>(
    `EXPLAIN (FORMAT JSON) ${query}`,
    params,
  );
  return JSON.stringify(result.rows[0]!['QUERY PLAN']);
}

describe('match page', () => {
  it('reads only the match players’ history for the economy curves', async () => {
    const target = matchIds[300]!;
    const viaFunction = await teamStatRowsRead('SELECT * FROM match_economy_curves($1::uuid)', [
      target,
    ]);
    const viaView = await teamStatRowsRead(
      'SELECT * FROM account_economy_curves_view WHERE match_id = $1',
      [target],
    );
    // Two players with up to four matches each: about 8 match/team rows (index
    // lookups may fetch a few more), against every row for the view.
    console.log(
      `match_team_stats rows read for one match's economy: function ${viaFunction}, view ${viaView} (${MATCHES * 2} stored)`,
    );
    expect(viaFunction).toBeGreaterThan(0);
    expect(viaFunction).toBeLessThanOrEqual(24);
    expect(viaView).toBeGreaterThanOrEqual(MATCHES * 2);
  });

  it('returns the same curves as the original view', async () => {
    const target = matchIds[123]!;
    const fromFunction = await client.query(
      `SELECT account_id, tick, units, buildings, prestige, average_units, average_buildings,
              average_prestige, games_at_tick
       FROM match_economy_curves($1::uuid) ORDER BY account_id, tick`,
      [target],
    );
    const fromView = await client.query(
      `SELECT account_id, tick, units, buildings, prestige, average_units, average_buildings,
              average_prestige, games_at_tick
       FROM account_economy_curves_view WHERE match_id = $1 ORDER BY account_id, tick`,
      [target],
    );
    expect(fromFunction.rows.length).toBe(2 * POINTS);
    expect(fromFunction.rows).toEqual(fromView.rows);
    // Both players have four recent matches (accounts below 200 play twice per
    // 400 matches), each sampled at every tick.
    expect(new Set(fromFunction.rows.map((r) => r.games_at_tick))).toEqual(new Set([4]));
  });

  it('finds the verify job by match through an index', async () => {
    const text = await plan(
      `SELECT result FROM engine_jobs
       WHERE match_id = $1 AND kind = 'verify-match' AND status = 'succeeded'
       ORDER BY completed_at DESC LIMIT 1`,
      [matchIds[5]],
    );
    expect(text).toMatch(/engine_jobs_(match|one_active_verify)_idx/);
    expect(text).not.toMatch(/"Seq Scan"/);
  });

  it('serves the whole match detail quickly', async () => {
    const history = new HistoryService({
      db: database.db,
      origin: 'http://history.test',
      queueNames: new Map([['ranked-1v1', 'Ranked']]),
      currentSimVersions: async () => [],
    });
    const target = matchIds[42]!;
    await history.matchDetail(target, undefined); // warm up
    const began = performance.now();
    const detail = await history.matchDetail(target, undefined);
    const ms = performance.now() - began;
    expect(detail.economy).toHaveLength(2);
    expect(detail.economy?.[0]?.points).toHaveLength(POINTS);
    expect(detail.verificationDetail).toMatchObject({ divergedSeats: [1] });
    expect(ms).toBeLessThan(500);
    console.log(`match detail on ${MATCHES} seeded matches: ${ms.toFixed(1)} ms`);
  });
});

describe('match lists', () => {
  it('walk the match-time index newest first instead of sorting every match', async () => {
    const text = await plan(
      `SELECT m.id FROM matches m WHERE m.status = 'ended'
       ORDER BY COALESCE(m.ended_at, m.started_at, m.created_at) DESC, m.id DESC LIMIT 21`,
    );
    expect(text).toMatch(/matches_time_idx/);
    expect(text).not.toMatch(/"Node Type": "Sort"/);
  });

  it('compute the typical queue wait from matched tickets only', async () => {
    const text = await plan(
      `SELECT percentile_cont(0.5) WITHIN GROUP (ORDER BY extract(epoch FROM updated_at - created_at))
       FROM queue_tickets WHERE queue_id = $1 AND status = 'matched' AND updated_at >= now() - interval '1 day'`,
      ['ranked-1v1'],
    );
    expect(text).toMatch(/queue_tickets_matched_idx/);
  });

  it('find a player’s latest economy without the per-tick window over their history', async () => {
    const history = new HistoryService({
      db: database.db,
      origin: 'http://history.test',
      queueNames: new Map(),
      currentSimVersions: async () => [],
    });
    const profile = await history.profile(accountIds[7]!, undefined);
    expect(profile.detail).toBe('full');
    const economy = profile.detail === 'full' ? profile.aggregates?.economy : undefined;
    expect(economy?.points).toHaveLength(POINTS);
    expect(economy?.accountId).toBe(accountIds[7]);
  });
});
