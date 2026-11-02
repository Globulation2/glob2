// End to end over HTTP: the platform records engine jobs, an agent running
// HeadlessEngineRunner (fake binary) leases them from a real platform-api
// replica with only an agent key (no database access), moves blobs through
// the API, and the worker's result task completes the engine_jobs rows.
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import type { Runner } from 'graphile-worker';
import { createHarness, type Harness, type Instance } from '@glob2/api/testing';
import {
  applyEngineJobResult,
  contentKey,
  createLogger,
  defaultMapPool,
  failAbandonedEngineJobs,
  startJobRunner,
  submitEngineJob,
} from '@glob2/core';
import { ENGINE_RESULT_TASK, simVersionKey } from '@glob2/protocol';
import { EngineAgent } from '../src/agent.ts';
import { PlatformClient } from '../src/platform.ts';
import { createRunner, SIM, type RunnerHarness } from './support.ts';

const logger = createLogger('queue-test', 'silent');
const KEY = 'k'.repeat(40);
let harness: Harness;
let api: Instance;
let runnerHarness: RunnerHarness;
let agent: EngineAgent;
let results: Runner | undefined;

beforeAll(async () => {
  harness = await createHarness();
  api = await harness.start({ engineAgentKeys: [{ key: KEY }] });
  // The engine runner's own blob store is never used: blobs go through the API.
  runnerHarness = await createRunner();
  agent = new EngineAgent({
    id: 'agent-e2e',
    simVersion: SIM,
    build: 'test',
    runner: runnerHarness.runner,
    platform: new PlatformClient({ baseUrl: api.url, key: KEY }),
    logger,
    concurrency: 2,
    pollMs: 20,
  });
  await agent.heartbeat();
  agent.start();
  // apps/worker's result task, against the same database.
  results = await startJobRunner({
    pool: harness.database.pool,
    logger,
    pollIntervalMs: 50,
    concurrency: 2,
    tasks: {
      [ENGINE_RESULT_TASK]: async (payload) =>
        void (await applyEngineJobResult(harness.database.db, payload)),
    },
  });
});

afterAll(async () => {
  await agent?.stop();
  await results?.stop();
  await runnerHarness?.close();
  await harness?.close();
});

async function completed(jobId: string) {
  const deadline = Date.now() + 15_000;
  for (;;) {
    const row = await harness.database.db
      .selectFrom('engine_jobs')
      .selectAll()
      .where('id', '=', jobId)
      .executeTakeFirstOrThrow();
    if (row.status !== 'queued') return row;
    if (Date.now() > deadline) throw new Error(`job ${jobId} still queued`);
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
}

describe('engine jobs over the internal API', () => {
  it('registers the agent, then generates, validates and previews a pool map', async () => {
    const agents = await harness.database.db.selectFrom('engine_agents').selectAll().execute();
    expect(agents).toMatchObject([{ id: 'agent-e2e', sim_version: simVersionKey(SIM) }]);

    const db = harness.database.db;
    const entry = defaultMapPool('2v2').find((e) => e.generatorId === 'symmetric-arena')!;
    const generate = await completed(
      await submitEngineJob(db, {
        kind: 'generate-map',
        simVersion: SIM,
        payload: { generator: { ...entry, seed: 5 } },
      }),
    );
    expect(generate.status).toBe('succeeded');
    expect(generate.agent_id).toBe('agent-e2e');
    expect(generate.attempts).toBe(1);
    const mapHash = (generate.result as { mapHash: string }).mapHash;
    expect(generate.result).toMatchObject({ map: { width: 128, height: 128, teamCount: 4 } });
    // Stored through the API: in the platform's blob store and blobs table.
    expect(await harness.blobs.size(contentKey(mapHash))).toBeGreaterThan(0);
    expect(
      await db.selectFrom('blobs').selectAll().where('sha256', '=', mapHash).executeTakeFirst(),
    ).toMatchObject({ content_type: 'application/x-glob2-map', visibility: 'public' });

    const [validate, preview] = await Promise.all([
      submitEngineJob(db, {
        kind: 'validate-map',
        simVersion: SIM,
        payload: { blobHash: mapHash, format: 'map' },
      }).then(completed),
      submitEngineJob(db, {
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
      await submitEngineJob(harness.database.db, {
        kind: 'generate-map',
        simVersion: SIM,
        payload: { generator: { ...entry, revision: 77, seed: 1 } },
      }),
    );
    expect(failed.status).toBe('failed');
    expect(failed.attempts).toBe(1);
    expect(failed.error).toMatchObject({ code: 'bad_request' });
  });

  it('fails a job whose last lease ran out without a report', async () => {
    const db = harness.database.db;
    // Another sim version, so the running agent leaves it alone.
    const other = { ...SIM, dataHash: 'cd'.repeat(32) };
    const jobId = await submitEngineJob(db, {
      kind: 'render-preview',
      simVersion: other,
      payload: { mapHash: 'ef'.repeat(32), maxSizePx: 64 },
      maxAttempts: 1,
    });
    await db
      .updateTable('engine_jobs')
      .set({ attempts: 1, leased_by: 'gone', lease_expires_at: new Date(Date.now() - 1000) })
      .where('id', '=', jobId)
      .execute();
    expect(await failAbandonedEngineJobs(db)).toBe(1);
    expect(await failAbandonedEngineJobs(db)).toBe(0);
    const row = await completed(jobId);
    expect(row.status).toBe('failed');
    expect(row.error).toMatchObject({
      code: 'internal',
      message: expect.stringMatching(/no result/),
    });
  });
});
