// End to end through the job queue: the platform submits engine jobs, an
// agent running HeadlessEngineRunner (fake binary) executes them under its
// sim version's task identifiers, and the results complete engine_jobs rows.
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import type { Runner } from 'graphile-worker';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import {
  JobQueue,
  applyEngineJobResult,
  createLogger,
  defaultMapPool,
  prepareJobQueue,
  startJobRunner,
  submitEngineJob,
} from '@glob2/core';
import { ENGINE_RESULT_TASK } from '@glob2/protocol';
import { EngineAgent } from '../src/agent.ts';
import { createRunner, SIM, type RunnerHarness } from './support.ts';

const logger = createLogger('queue-test', 'silent');
let database: TestDatabase;
let queue: JobQueue;
let harness: RunnerHarness;
let runner: Runner | undefined;

beforeAll(async () => {
  database = await createTestDatabase();
  await prepareJobQueue(database.pool, logger);
  queue = await JobQueue.create(database.pool, logger);
  harness = await createRunner(database.db);
  const agent = new EngineAgent({
    id: 'agent-e2e',
    simVersion: SIM,
    build: 'test',
    runner: harness.runner,
    queue,
    db: database.db,
    logger,
  });
  runner = await startJobRunner({
    pool: database.pool,
    logger,
    pollIntervalMs: 50,
    concurrency: 2,
    tasks: {
      ...agent.tasks(),
      [ENGINE_RESULT_TASK]: async (payload) =>
        void (await applyEngineJobResult(database.db, payload)),
    },
  });
});

afterAll(async () => {
  await runner?.stop();
  await queue?.close();
  await harness?.close();
  await database?.drop();
});

async function completed(jobId: string) {
  const deadline = Date.now() + 15_000;
  for (;;) {
    const row = await database.db
      .selectFrom('engine_jobs')
      .selectAll()
      .where('id', '=', jobId)
      .executeTakeFirstOrThrow();
    if (row.status !== 'queued') return row;
    if (Date.now() > deadline) throw new Error(`job ${jobId} still queued`);
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
}

describe('engine jobs through the queue', () => {
  it('generates, validates and previews a pool map', async () => {
    const entry = defaultMapPool('2v2').find((e) => e.generatorId === 'symmetric-arena')!;
    const generate = await completed(
      await submitEngineJob(database.db, {
        kind: 'generate-map',
        simVersion: SIM,
        payload: { generator: { ...entry, seed: 5 } },
      }),
    );
    expect(generate.status).toBe('succeeded');
    expect(generate.agent_id).toBe('agent-e2e');
    const mapHash = (generate.result as { mapHash: string }).mapHash;
    expect(generate.result).toMatchObject({ map: { width: 128, height: 128, teamCount: 4 } });

    const [validate, preview] = await Promise.all([
      submitEngineJob(database.db, {
        kind: 'validate-map',
        simVersion: SIM,
        payload: { blobHash: mapHash, format: 'map' },
      }).then(completed),
      submitEngineJob(database.db, {
        kind: 'render-preview',
        simVersion: SIM,
        payload: { mapHash, maxSizePx: 512 },
      }).then(completed),
    ]);
    expect(validate.result).toMatchObject({ valid: true, mapHash, versionMinor: 125 });
    expect(preview.result).toMatchObject({ width: 512, height: 512, contentType: 'image/png' });
  });

  it('records deterministic failures without retrying', async () => {
    const entry = defaultMapPool('1v1').find((e) => e.generatorId === 'symmetric-arena')!;
    const failed = await completed(
      await submitEngineJob(database.db, {
        kind: 'generate-map',
        simVersion: SIM,
        payload: { generator: { ...entry, revision: 77, seed: 1 } },
      }),
    );
    expect(failed.status).toBe('failed');
    expect(failed.error).toMatchObject({ code: 'bad_request' });
  });
});
