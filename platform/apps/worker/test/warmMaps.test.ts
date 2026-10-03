// Warm map pool: refill per queue, entry and served sim version; completion
// through the engine-job result path; takeWarmMap; backoff and housekeeping.
import { afterAll, beforeAll, beforeEach, describe, expect, it } from 'vitest';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { createLogger, defaultMapPool, resolveQueue, type ResolvedQueue } from '@glob2/core';
import { handleEngineJobResult } from '../src/ratings/apply.ts';
import {
  WARM_MAP_FAILURE_LIMIT,
  WarmMapPool,
  poolEntryKey,
  servedSimVersions,
  takeWarmMap,
} from '../src/warmMaps.ts';
import { SIM_A, SIM_B } from './support.ts';

const logger = createLogger('warm-maps-test', 'silent');
let database: TestDatabase;

const twoEntries = (): ResolvedQueue =>
  resolveQueue({
    id: 'casual-1v1',
    name: 'Casual 1v1',
    mode: '1v1',
    rated: false,
    mapPool: defaultMapPool('1v1').filter((e) =>
      ['symmetric-arena', 'even-ground'].includes(e.generatorId),
    ),
  });

beforeAll(async () => {
  database = await createTestDatabase({ role: 'worker' });
});

afterAll(async () => {
  await database?.drop();
});

beforeEach(async () => {
  await database.db.deleteFrom('warm_maps').execute();
  await database.db.deleteFrom('engine_jobs').execute();
  await database.db.deleteFrom('engine_agents').execute();
});

async function agent(id: string, simVersion: string, seenSecondsAgo = 0) {
  await database.db
    .insertInto('engine_agents')
    .values({
      id,
      sim_version: simVersion,
      kinds: ['generate-map', 'validate-map', 'render-preview', 'verify-match'],
      build: 'test',
      last_seen_at: new Date(Date.now() - seenSecondsAgo * 1000),
    })
    .execute();
}

/** Engine jobs waiting for an agent, as `<kind>:<sim version>`. */
async function queuedJobs(): Promise<string[]> {
  const rows = await database.db
    .selectFrom('engine_jobs')
    .select(['kind', 'sim_version'])
    .where('status', '=', 'queued')
    .orderBy('created_at')
    .execute();
  return rows.map((r) => `${r.kind}:${r.sim_version}`);
}

/** Completes every generating warm map as the engine agent and worker would. */
async function completeAll(ok: (index: number) => boolean = () => true) {
  const rows = await database.db
    .selectFrom('warm_maps')
    .select(['job_id', 'generator'])
    .where('status', '=', 'generating')
    .orderBy('created_at')
    .execute();
  let index = 0;
  for (const row of rows) {
    const success = ok(index++);
    const hash = index.toString(16).padStart(2, '0').repeat(32);
    expect(
      await handleEngineJobResult(
        database.db,
        success
          ? {
              jobId: row.job_id,
              kind: 'generate-map',
              ok: true,
              result: {
                mapHash: hash,
                size: 1003791,
                map: { width: 128, height: 128, teamCount: 2 },
                chosenSeed: 99,
              },
              agent: 'agent-test',
            }
          : {
              jobId: row.job_id,
              kind: 'generate-map',
              ok: false,
              error: {
                code: 'bad_request',
                message: 'generator is at revision 3 in this engine, not 2',
              },
              agent: 'agent-test',
            },
      ),
    ).toBe(true);
  }
}

describe('WarmMapPool', () => {
  it('keeps N maps per entry for every served sim version and refills what is taken', async () => {
    await agent('a1', SIM_A);
    await agent('a2', SIM_A);
    await agent('b1', SIM_B);
    await agent('stale', `127-49-${'ee'.repeat(32)}`, 3600);
    expect(await servedSimVersions(database.db)).toEqual([SIM_A, SIM_B]);

    const q = twoEntries();
    let seed = 0;
    const pool = new WarmMapPool({
      db: database.db,
      queues: [q],
      perEntry: 2,
      logger,
      seed: () => ++seed,
    });
    expect((await pool.refill()).submitted).toBe(8); // 2 versions × 2 entries × 2
    expect((await pool.refill()).submitted).toBe(0); // generating counts as open
    const tasks = await queuedJobs();
    expect(tasks.filter((t) => t === `generate-map:${SIM_A}`)).toHaveLength(4);
    expect(tasks.filter((t) => t === `generate-map:${SIM_B}`)).toHaveLength(4);

    // Jobs carry a full descriptor with a fresh seed, recorded on engine_jobs.
    const jobs = await database.db.selectFrom('engine_jobs').select(['payload']).execute();
    const seeds = jobs.map((j) => (j.payload as { generator: { seed: number } }).generator.seed);
    expect(new Set(seeds).size).toBe(8);

    expect(await takeWarmMap(database.db, q.id, SIM_A)).toBeUndefined(); // nothing ready yet
    await completeAll();
    const arena = q.mapPool.find((e) => e.generatorId === 'symmetric-arena')!;
    const taken = await takeWarmMap(database.db, q.id, SIM_A, { entry: arena });
    expect(taken).toMatchObject({
      queueId: q.id,
      simVersion: SIM_A,
      generator: { ...arena },
      facts: { map: { width: 128, height: 128, teamCount: 2 }, chosenSeed: 99 },
    });
    expect(taken!.mapHash).toMatch(/^[0-9a-f]{64}$/);
    const row = await database.db
      .selectFrom('warm_maps')
      .selectAll()
      .where('id', '=', taken!.id)
      .executeTakeFirstOrThrow();
    expect(row.status).toBe('taken');
    expect(row.entry_key).toBe(poolEntryKey(arena));

    expect((await pool.refill()).submitted).toBe(1); // replaces the one taken
  });

  it('hands each ready map to exactly one concurrent taker', async () => {
    await agent('a1', SIM_A);
    const q = twoEntries();
    const pool = new WarmMapPool({ db: database.db, queues: [q], perEntry: 3, logger });
    await pool.refill();
    await completeAll();
    const takes = await Promise.all(
      Array.from({ length: 8 }, () => takeWarmMap(database.db, q.id, SIM_A)),
    );
    const ids = takes.filter((t) => t !== undefined).map((t) => t!.id);
    expect(ids).toHaveLength(6);
    expect(new Set(ids).size).toBe(6);
    expect(await takeWarmMap(database.db, q.id, SIM_B)).toBeUndefined();
  });

  it('backs off entries that keep failing and drops maps of entries no longer configured', async () => {
    await agent('a1', SIM_A);
    const q = twoEntries();
    const pool = new WarmMapPool({ db: database.db, queues: [q], perEntry: 1, logger });
    const arenaKey = poolEntryKey(q.mapPool.find((e) => e.generatorId === 'symmetric-arena')!);
    for (let round = 0; round < WARM_MAP_FAILURE_LIMIT; round++) {
      await pool.refill();
      // Arena always fails (e.g. a revision this sim version does not have).
      const rows = await database.db
        .selectFrom('warm_maps')
        .select(['entry_key'])
        .where('status', '=', 'generating')
        .orderBy('created_at')
        .execute();
      await completeAll((i) => rows[i]!.entry_key !== arenaKey);
    }
    const failed = await database.db
      .selectFrom('warm_maps')
      .select(['failure'])
      .where('status', '=', 'failed')
      .execute();
    expect(failed).toHaveLength(WARM_MAP_FAILURE_LIMIT);
    expect(failed[0]!.failure).toMatch(/revision 3/);
    expect((await pool.refill()).submitted).toBe(0); // arena backs off, even-ground is ready

    // Even Ground leaves the pool: its ready map is dropped.
    const arenaOnly = {
      ...q,
      mapPool: q.mapPool.filter((e) => e.generatorId === 'symmetric-arena'),
    };
    const shrunk = new WarmMapPool({
      db: database.db,
      queues: [arenaOnly],
      perEntry: 1,
      logger,
    });
    expect((await shrunk.refill()).deleted).toBe(1);
    expect(await takeWarmMap(database.db, q.id, SIM_A)).toBeUndefined();
  });

  it('expires generation jobs that never report', async () => {
    await agent('a1', SIM_A);
    const pool = new WarmMapPool({
      db: database.db,
      queues: [twoEntries()],
      perEntry: 1,
      logger,
    });
    await pool.refill();
    await database.db
      .updateTable('warm_maps')
      .set({ created_at: new Date(Date.now() - 2 * 3600 * 1000) })
      .execute();
    const result = await pool.refill();
    expect(result.expired).toBe(2);
    expect(result.submitted).toBe(2);
  });

  it('does nothing when disabled or when no agent serves a version', async () => {
    const q = twoEntries();
    expect(
      (await new WarmMapPool({ db: database.db, queues: [q], perEntry: 1, logger }).refill())
        .submitted,
    ).toBe(0);
    await agent('a1', SIM_A);
    expect(
      (await new WarmMapPool({ db: database.db, queues: [q], perEntry: 0, logger }).refill())
        .submitted,
    ).toBe(0);
  });
});
