// Engine jobs on a real Postgres: submitted jobs are leased per sim version,
// at most once at a time, with a retry budget; reports are enqueued for the
// worker atomically and exactly once; the worker task applies them.
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { sql } from 'kysely';
import type { Runner } from 'graphile-worker';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { ENGINE_RESULT_TASK, type SimVersion } from '@glob2/protocol';
import {
  applyEngineJobResult,
  createLogger,
  extendEngineLease,
  failAbandonedEngineJobs,
  leaseEngineJob,
  leasedJob,
  releaseEngineLease,
  reportEngineJob,
  startJobRunner,
  submitEngineJob,
} from '../src/index.ts';

const logger = createLogger('test', 'silent');
const SIM_A: SimVersion = { versionMinor: 125, netProtocol: 49, dataHash: 'aa'.repeat(32) };
const SIM_B: SimVersion = { versionMinor: 126, netProtocol: 50, dataHash: 'bb'.repeat(32) };
const MAP_HASH = 'cd'.repeat(32);
const GENERATOR = {
  generatorId: 'even-ground',
  revision: 3,
  params: { width: 7, height: 7, teams: 2 },
  seed: 1,
  candidates: 5,
  startingUnitLevel: 0,
};

let database: TestDatabase;
let worker: Runner | undefined;
const applied: unknown[] = [];

beforeAll(async () => {
  database = await createTestDatabase();
  worker = await startJobRunner({
    pool: database.pool,
    logger,
    pollIntervalMs: 50,
    tasks: {
      [ENGINE_RESULT_TASK]: async (payload) => {
        applied.push(payload);
        await applyEngineJobResult(database.db, payload);
      },
    },
  });
});

afterAll(async () => {
  await worker?.stop();
  await database?.drop();
});

async function row(jobId: string) {
  return database.db
    .selectFrom('engine_jobs')
    .selectAll()
    .where('id', '=', jobId)
    .executeTakeFirstOrThrow();
}

async function until(condition: () => Promise<boolean>, timeoutMs = 15_000): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (!(await condition())) {
    if (Date.now() > deadline) throw new Error('timed out');
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
}

const leaseFor = (simVersion: SimVersion, agentId = 'agent-a', leaseSeconds = 60) =>
  leaseEngineJob(database.db, {
    agentId,
    simVersion,
    kinds: ['generate-map', 'render-preview'],
    leaseSeconds,
  });

describe('engine jobs', () => {
  it('are leased by an agent of their sim version only, and applied by the worker', async () => {
    const jobId = await submitEngineJob(database.db, {
      kind: 'generate-map',
      simVersion: SIM_A,
      payload: { generator: GENERATOR },
    });
    expect(await leaseFor(SIM_B, 'agent-b')).toBeUndefined();
    const lease = await leaseFor(SIM_A);
    expect(lease).toMatchObject({
      job: { jobId, kind: 'generate-map' },
      attempt: 1,
      maxAttempts: 3,
    });
    // Held: nobody else gets it.
    expect(await leaseFor(SIM_A, 'agent-c')).toBeUndefined();
    expect(await leasedJob(database.db, lease!.leaseToken)).toMatchObject({ id: jobId });
    expect(await extendEngineLease(database.db, jobId, lease!.leaseToken, 120)).toBe(true);
    expect(await extendEngineLease(database.db, jobId, 'x'.repeat(43), 120)).toBe(false);

    const result = {
      mapHash: MAP_HASH,
      size: 10,
      map: { width: 7, height: 7, teamCount: 2 },
      chosenSeed: 1,
    };
    expect(await reportEngineJob(database.db, jobId, lease!.leaseToken, { ok: true, result })).toBe(
      'accepted',
    );
    // A retried report is recognised; nothing is enqueued twice.
    expect(await reportEngineJob(database.db, jobId, lease!.leaseToken, { ok: true, result })).toBe(
      'duplicate',
    );
    expect(await leaseFor(SIM_A, 'agent-c')).toBeUndefined();
    await until(async () => (await row(jobId)).status !== 'queued');
    expect(await row(jobId)).toMatchObject({
      status: 'succeeded',
      agent_id: 'agent-a',
      result,
    });
    expect(applied.filter((p) => (p as { jobId: string }).jobId === jobId)).toHaveLength(1);
  });

  it('retries with a budget: released and expired leases come back, then the job fails', async () => {
    const jobId = await submitEngineJob(database.db, {
      kind: 'render-preview',
      simVersion: SIM_B,
      payload: { mapHash: MAP_HASH, maxSizePx: 64 },
      maxAttempts: 2,
    });
    const first = await leaseFor(SIM_B);
    expect(first?.attempt).toBe(1);
    expect(await releaseEngineLease(database.db, jobId, first!.leaseToken, 0)).toBe(true);
    // The released token is dead.
    expect(
      await reportEngineJob(database.db, jobId, first!.leaseToken, { ok: true, result: {} }),
    ).toBe('lost');
    const second = await leaseFor(SIM_B, 'agent-b', 10);
    expect(second?.attempt).toBe(2);
    // The agent dies: its lease runs out with the budget used up.
    await sql`UPDATE engine_jobs SET lease_expires_at = now() - interval '1 second' WHERE id = ${jobId}`.execute(
      database.as('migrator').db,
    );
    expect(await leaseFor(SIM_B, 'agent-c')).toBeUndefined();
    expect(await failAbandonedEngineJobs(database.db)).toBe(1);
    await until(async () => (await row(jobId)).status !== 'queued');
    expect(await row(jobId)).toMatchObject({
      status: 'failed',
      agent_id: 'agent-b',
      error: { code: 'internal' },
    });
  });

  it('fails a stored job that no longer reads as a job instead of handing it out', async () => {
    const db = database.as('migrator').db;
    const broken = '00000000-0000-4000-8000-0000000000aa';
    await db
      .insertInto('engine_jobs')
      .values({
        id: broken,
        kind: 'generate-map',
        sim_version: `125-49-${'ee'.repeat(32)}`,
        payload: '{"generator":{}}',
      })
      .execute();
    const lease = await leaseEngineJob(database.db, {
      agentId: 'agent-e',
      simVersion: { versionMinor: 125, netProtocol: 49, dataHash: 'ee'.repeat(32) },
      kinds: ['generate-map'],
      leaseSeconds: 60,
    });
    expect(lease).toBeUndefined();
    await until(async () => (await row(broken)).status !== 'queued');
    expect(await row(broken)).toMatchObject({ status: 'failed', error: { code: 'internal' } });
  });

  it('rejects invalid jobs and records contract-breaking results as failures', async () => {
    await expect(
      submitEngineJob(database.db, {
        kind: 'render-preview',
        simVersion: SIM_A,
        payload: { mapHash: 'nope', maxSizePx: 512 },
      }),
    ).rejects.toThrow(/invalid render-preview job/);

    const jobId = await submitEngineJob(database.db, {
      kind: 'render-preview',
      simVersion: SIM_B,
      payload: { mapHash: MAP_HASH, maxSizePx: 512 },
    });
    expect(
      await applyEngineJobResult(database.db, {
        jobId,
        kind: 'render-preview',
        ok: true,
        agent: 'agent-b',
        result: { previewHash: 'not-a-hash' },
      }),
    ).toBe(true);
    const failed = await row(jobId);
    expect(failed.status).toBe('failed');
    expect((failed.error as { message: string }).message).toMatch(/contract/);
    // A second report for a completed job changes nothing.
    expect(
      await applyEngineJobResult(database.db, {
        jobId,
        kind: 'render-preview',
        ok: false,
        agent: 'agent-b',
        error: { code: 'internal', message: 'late' },
      }),
    ).toBe(false);
  });
});
