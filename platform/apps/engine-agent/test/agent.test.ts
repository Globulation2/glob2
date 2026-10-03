import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import type { Runner } from 'graphile-worker';
import {
  JobQueue,
  applyEngineJobResult,
  createLogger,
  prepareJobQueue,
  startJobRunner,
} from '@glob2/core';
import {
  ENGINE_RESULT_TASK,
  engineTaskIdentifier,
  simVersionKey,
  type EngineJob,
} from '@glob2/protocol';
import { EngineAgent, EngineJobError, unsupportedRunner, type EngineRunner } from '../src/agent.ts';

const logger = createLogger('agent-test', 'silent');
const SIM = { versionMinor: 125, netProtocol: 49, dataHash: 'ab'.repeat(32) };
const OTHER = { versionMinor: 125, netProtocol: 49, dataHash: 'cd'.repeat(32) };
const HASH = 'ee'.repeat(32);

let database: TestDatabase;
let queue: JobQueue;
let resultRunner: Runner | undefined;

beforeAll(async () => {
  database = await createTestDatabase();
  await prepareJobQueue(database.pool, logger);
  queue = await JobQueue.create(database.pool, logger);
  resultRunner = await startJobRunner({
    pool: database.pool,
    logger,
    pollIntervalMs: 50,
    concurrency: 1,
    tasks: { [ENGINE_RESULT_TASK]: async (payload) => void reported.push(payload) },
  });
});

afterAll(async () => {
  await resultRunner?.stop();
  await queue?.close();
  await database?.drop();
});

const reported: unknown[] = [];

/** Waits for `count` result reports from the agent, as the worker would receive them. */
async function takeResults(count: number): Promise<unknown[]> {
  const deadline = Date.now() + 10_000;
  while (reported.length < count) {
    if (Date.now() > deadline) throw new Error(`expected ${count} results, got ${reported.length}`);
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
  return reported.splice(0, count);
}

function previewJob(simVersion = SIM): EngineJob {
  return {
    jobId: crypto.randomUUID(),
    kind: 'render-preview',
    simVersion,
    payload: { mapHash: HASH, maxSizePx: 256 },
  };
}

describe('EngineAgent', () => {
  it('registers itself and serves only its own sim version', async () => {
    const agent = new EngineAgent({
      id: 'agent-1',
      simVersion: SIM,
      build: 'test',
      runner: unsupportedRunner,
      queue,
      db: database.db,
      logger,
    });
    await agent.heartbeat();
    await agent.heartbeat();
    const row = await database.db.selectFrom('engine_agents').selectAll().executeTakeFirstOrThrow();
    expect(row.sim_version).toBe(simVersionKey(SIM));
    expect(Object.keys(agent.tasks()).sort()).toEqual(
      ['generate-map', 'render-preview', 'validate-map', 'verify-match']
        .map((kind) => engineTaskIdentifier(kind as never, SIM))
        .sort(),
    );
    await expect(agent.handle(previewJob(OTHER))).rejects.toThrow(/reached agent/);
    await agent.deregister();
    expect(await database.db.selectFrom('engine_agents').selectAll().execute()).toEqual([]);
  });

  it('reports results and deterministic failures, and lets other errors retry', async () => {
    let attempt = 0;
    const runner: EngineRunner = {
      kinds: ['render-preview'],
      run: async (job) => {
        attempt++;
        if (attempt === 1)
          return { previewHash: HASH, contentType: 'image/png', width: 256, height: 256 };
        if (attempt === 2) throw new EngineJobError('bad_request', `map ${job.kind} unreadable`);
        throw new Error('blob store unavailable');
      },
    };
    const agent = new EngineAgent({
      id: 'agent-2',
      simVersion: SIM,
      build: 'test',
      runner,
      queue,
      db: database.db,
      logger,
    });

    await agent.handle(previewJob());
    await agent.handle(previewJob());
    await expect(agent.handle(previewJob())).rejects.toThrow(/blob store/);

    const results = (await takeResults(2)) as { ok: boolean; agent: string }[];
    expect(results.map((r) => [r.ok, r.agent]).sort()).toEqual([
      [false, 'agent-2'],
      [true, 'agent-2'],
    ]);
    // The worker side accepts the agent's report shape.
    expect(await applyEngineJobResult(database.db, results[0])).toBe(false); // no engine_jobs row in this test
  });

  it('reports every kind unsupported by default', async () => {
    const agent = new EngineAgent({
      id: 'agent-3',
      simVersion: SIM,
      build: 'test',
      runner: unsupportedRunner,
      queue,
      db: database.db,
      logger,
    });
    await agent.handle(previewJob());
    const [result] = (await takeResults(1)) as { ok: boolean; error: { code: string } }[];
    expect(result?.ok).toBe(false);
    expect(result?.error.code).toBe('unsupported');
  });
});
