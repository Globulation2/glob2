// End to end through graphile-worker on a real Postgres: the platform submits
// a job for one sim version, an "agent" for that version runs it, a "worker"
// applies the result. An agent for another sim version never sees the job.
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import type { Runner } from 'graphile-worker';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import {
  ENGINE_RESULT_TASK,
  engineTaskIdentifier,
  parse,
  GenerateMapResult,
  type SimVersion,
} from '@glob2/protocol';
import {
  JobQueue,
  applyEngineJobResult,
  createLogger,
  parseEngineJob,
  prepareJobQueue,
  reportEngineJobResult,
  startJobRunner,
  submitEngineJob,
} from '../src/index.ts';

const logger = createLogger('test', 'silent');
const SIM_A: SimVersion = { versionMinor: 125, netProtocol: 49, dataHash: 'aa'.repeat(32) };
const SIM_B: SimVersion = { versionMinor: 126, netProtocol: 50, dataHash: 'bb'.repeat(32) };
const MAP_HASH = 'cd'.repeat(32);

let database: TestDatabase;
let queue: JobQueue;
const runners: Runner[] = [];

beforeAll(async () => {
  database = await createTestDatabase();
  await prepareJobQueue(database.pool, logger);
  queue = await JobQueue.create(database.pool, logger);
});

afterAll(async () => {
  for (const runner of runners) await runner.stop();
  await queue?.close();
  await database?.drop();
});

async function until(condition: () => Promise<boolean>, timeoutMs = 15_000): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (!(await condition())) {
    if (Date.now() > deadline) throw new Error('timed out');
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
}

describe('engine jobs', () => {
  it('run on an agent of the matching sim version and are applied by the worker', async () => {
    const seenByB: string[] = [];
    runners.push(
      await startJobRunner({
        pool: database.pool,
        logger,
        pollIntervalMs: 100,
        tasks: {
          [engineTaskIdentifier('generate-map', SIM_A)]: async (payload) => {
            const job = parseEngineJob(payload);
            expect(job.kind).toBe('generate-map');
            await reportEngineJobResult(queue, {
              jobId: job.jobId,
              kind: 'generate-map',
              ok: true,
              agent: 'agent-a',
              result: parse(GenerateMapResult, {
                mapHash: MAP_HASH,
                size: 4096,
                map: { width: 128, height: 128, teamCount: 2 },
                chosenSeed: 7,
              }),
            });
          },
        },
      }),
      await startJobRunner({
        pool: database.pool,
        logger,
        pollIntervalMs: 100,
        tasks: {
          [engineTaskIdentifier('generate-map', SIM_B)]: async (payload) => {
            seenByB.push(parseEngineJob(payload).jobId);
          },
        },
      }),
      await startJobRunner({
        pool: database.pool,
        logger,
        pollIntervalMs: 100,
        tasks: {
          [ENGINE_RESULT_TASK]: async (payload) => {
            await applyEngineJobResult(database.db, payload);
          },
        },
      }),
    );

    const jobId = await submitEngineJob(database.db, {
      kind: 'generate-map',
      simVersion: SIM_A,
      payload: {
        generator: {
          generatorId: 'even-ground',
          revision: 3,
          params: { width: 7, height: 7, teams: 2 },
          seed: 1,
          candidates: 5,
          startingUnitLevel: 0,
        },
      },
    });

    await until(async () => {
      const row = await database.db
        .selectFrom('engine_jobs')
        .select('status')
        .where('id', '=', jobId)
        .executeTakeFirstOrThrow();
      return row.status !== 'queued';
    });
    const row = await database.db
      .selectFrom('engine_jobs')
      .selectAll()
      .where('id', '=', jobId)
      .executeTakeFirstOrThrow();
    expect(row.status).toBe('succeeded');
    expect(row.agent_id).toBe('agent-a');
    expect((row.result as { mapHash: string }).mapHash).toBe(MAP_HASH);
    expect(seenByB).toEqual([]);
  });

  it('rejects invalid jobs before enqueueing and records contract-breaking results as failures', async () => {
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
    const applied = await applyEngineJobResult(database.db, {
      jobId,
      kind: 'render-preview',
      ok: true,
      agent: 'agent-b',
      result: { previewHash: 'not-a-hash' },
    });
    expect(applied).toBe(true);
    const row = await database.db
      .selectFrom('engine_jobs')
      .selectAll()
      .where('id', '=', jobId)
      .executeTakeFirstOrThrow();
    expect(row.status).toBe('failed');
    expect((row.error as { message: string }).message).toMatch(/contract/);
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
