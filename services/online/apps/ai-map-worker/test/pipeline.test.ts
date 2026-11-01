// Real native integration; provider calls are replaced with deterministic reference-image delivery.
// GLOB2_BINARY and MAP_PYTHON enable the suite. No provider credentials or payment calls.
import { randomUUID } from 'node:crypto';
import { mkdir, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { afterAll, beforeAll, describe, expect, it, vi } from 'vitest';
import { sql } from 'kysely';
import type { TestDatabase } from '@glob2/db/testing';
import { createHarness, type Harness } from '@glob2/api/testing';
import { createLogger, startJobRunner, applyEngineJobResult } from '@glob2/core';
import { Studio } from '@glob2/map-studio';
import { ENGINE_RESULT_TASK } from '@glob2/protocol';
import { EngineAgent } from '../../engine-agent/src/agent.ts';
import { HeadlessEngineRunner } from '../../engine-agent/src/runners.ts';
import { PlatformClient } from '../../engine-agent/src/platform.ts';
import { detectSimVersion } from '@glob2/engine/simVersion';
import { createRunner, type RunnerHarness } from '../../engine-agent/test/support.ts';
import { AgentBlobs } from '@glob2/engine/blobs';
import { Pipeline } from '../src/pipeline.ts';
import type { MapProvider } from '../src/provider.ts';
const repo = fileURLToPath(new URL('../../../../', import.meta.url));
const binary = process.env['GLOB2_BINARY'] ? resolve(repo, process.env['GLOB2_BINARY']) : undefined;
describe.runIf(binary)('native AI map delivery', () => {
  let database: TestDatabase, h: RunnerHarness, studio: Studio, pipeline: Pipeline;
  let harness: Harness, agent: EngineAgent, client: PlatformClient;
  let results: Awaited<ReturnType<typeof startJobRunner>> | undefined;
  const referenceId = 'symmetric-arena';
  const text = vi.fn<MapProvider['text']>(async (_model, _prompt, schema) => {
    const shape = schema as {
      properties: {
        reply?: unknown;
        examples?: { items: { properties: { generator_id: { enum: string[] } } } };
      };
    };
    if (shape.properties.reply)
      return {
        text: JSON.stringify({
          reply: 'Ponds with broad walking routes and renewable food.',
          brief: 'Four homes beside ponds with open routes.',
        }),
        usage: {},
      };
    const allowed = shape.properties.examples!.items.properties.generator_id.enum;
    const names = [referenceId, ...allowed.filter((id) => id !== referenceId)].slice(0, 6);
    return {
      text: JSON.stringify({
        style: 'landscape',
        concept_features: ['ponds'],
        examples: names.map((generator_id) => ({
          generator_id,
          reason: 'Native landscape reference',
          borrow: ['opening economy'],
          avoid: ['unrelated terrain'],
        })),
      }),
      usage: {},
    };
  });
  const image = vi.fn<MapProvider['image']>(async (_model, _prompt, images) => ({
    bytes: images[0]!.bytes,
    usage: {},
  }));
  beforeAll(async () => {
    harness = await createHarness();
    database = harness.database;
    const worker = database.as('worker');
    h = await createRunner({ binary: binary!, workdir: repo });
    const catalog = await h.engine.catalog();
    const { simVersion } = await detectSimVersion(h.engine, catalog, {});
    const api = await harness.start({ engineAgentKeys: [{ key: 'k'.repeat(40) }] });
    client = new PlatformClient({ baseUrl: api.url, key: 'k'.repeat(40) });
    const logger = createLogger('studio-native-test', 'silent');
    agent = new EngineAgent({
      id: 'studio-native-agent',
      simVersion,
      build: 'test',
      platform: client,
      logger,
      runner: new HeadlessEngineRunner({ engine: h.engine, catalog, simVersion }),
    });
    await agent.heartbeat();
    results = await startJobRunner({
      pool: worker.pool,
      logger,
      pollIntervalMs: 50,
      tasks: {
        [ENGINE_RESULT_TASK]: async (payload) => {
          await applyEngineJobResult(worker.db, payload);
        },
      },
    });
    studio = new Studio(worker.db);
    pipeline = new Pipeline({
      studio,
      blobs: new AgentBlobs(harness.blobs, worker.db),
      provider: { text, image },
      binary: binary!,
      source: repo,
      simVersion,
      python: process.env['MAP_PYTHON'],
      config: {
        enabled: true,
        salesEnabled: false,
        textModel: 'mock-text',
        imageModel: 'mock-image',
        pipelineVersion: 'test-v1',
        providerCallsPerDay: 20,
      },
    });
  }, 120000);
  afterAll(async () => {
    await h?.close();
    await results?.stop();
    await harness?.close();
  });
  it('discusses, imports, validates and privately delivers a map and revision with exactly one credit each', async () => {
    const account = (
      await database.db
        .insertInto('accounts')
        .values({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
        .returning('id')
        .executeTakeFirstOrThrow()
    ).id;
    await studio.credits.adjust(account, randomUUID(), 4, 'grant');
    const thread = (await studio.create(account, 'Pond country')).id;
    await studio.submit(
      account,
      thread,
      'chat',
      { id: randomUUID(), text: 'Four sustainable homes beside ponds' },
      'test-v1',
    );
    await pipeline.tick();
    expect((await studio.credits.balance(account)).balance).toBe(4);
    async function generate(parent?: string) {
      const id = randomUUID();
      await studio.submit(
        account,
        thread,
        'generate',
        { id, settings: { width: 256, height: 256, players: 4 }, ...(parent ? { parent } : {}) },
        'test-v1',
      );
      await pipeline.tick();
      const row = (await studio.request(id))!;
      const diagnostics = row.checkpoints['preparationFailureHash']
        ? Buffer.from(
            await pipeline.options.blobs.read(
              String(row.checkpoints['preparationFailureHash']),
              8 * 1024 * 1024,
            ),
          ).toString()
        : '';
      expect(row.status, (row.error ?? '') + diagnostics).toBe('importing');
      const jobId = String(row.checkpoints['importJob']);
      const lease = await client.lease({
        agentId: agent.id,
        simVersion: pipeline.options.simVersion,
        kinds: ['import-ai-map'],
        leaseSeconds: 120,
      });
      expect(lease?.job.jobId).toBe(jobId);
      await agent.handle(lease!);
      const deadline = Date.now() + 15000;
      while (true) {
        const job = await database.db
          .selectFrom('engine_jobs')
          .select('status')
          .where('id', '=', jobId)
          .executeTakeFirstOrThrow();
        if (job.status !== 'queued') {
          expect(job.status).toBe('succeeded');
          break;
        }
        if (Date.now() > deadline) throw new Error('Native import result was not applied.');
        await new Promise((resolve) => setTimeout(resolve, 50));
      }
      await sql`UPDATE studio_requests SET lease_until=NULL WHERE id=${id}`.execute(database.db);
      await pipeline.tick();
      const ready = (await studio.request(id))!;
      expect(ready.status, ready.error ?? '').toBe('ready');
      expect(ready.charged).toBe(true);
      expect(ready.checkpoints['validation']).toMatchObject({ passed: true });
      const progress = await studio.progress(account, thread, id);
      expect(progress.stages.map((stage) => [stage.id, stage.status])).toEqual([
        ['prepare', 'complete'],
        ['terrain', 'complete'],
        ['build', 'complete'],
        ['checks', 'complete'],
        ['ready', 'complete'],
      ]);
      expect(progress.artifacts.filter((artifact) => artifact.kind === 'reference')).toHaveLength(
        6,
      );
      expect(new Set(progress.artifacts.map((artifact) => artifact.kind))).toEqual(
        new Set(['reference', 'generated', 'crop', 'categorical', 'preview']),
      );
      expect(progress.checks).toHaveLength(21);
      expect(progress.checks.every((check) => check.status === 'passed')).toBe(true);
      for (const artifact of progress.artifacts)
        expect(await studio.artifactBlob(account, thread, artifact.id)).toBeDefined();
      expect(await pipeline.tick()).toBe(false);
      expect(await studio.progress(account, thread, id)).toEqual(progress);
      const catalog = await database.db
        .selectFrom('maps')
        .selectAll()
        .where('id', '=', ready.map_id!)
        .executeTakeFirstOrThrow();
      expect(catalog.visibility).toBe('private');
      const blobs = await database.db
        .selectFrom('blobs')
        .select('visibility')
        .where('sha256', '=', ready.map_hash!)
        .executeTakeFirstOrThrow();
      expect(blobs.visibility).toBe('private');
      const evidence = process.env['STUDIO_EVIDENCE_DIR'] ?? process.env['GLOB2_EVIDENCE_DIR'];
      if (evidence) {
        await mkdir(evidence, { recursive: true });
        const prefix = join(evidence, parent ? 'revision' : 'initial');
        await writeFile(
          prefix + '.map',
          await pipeline.options.blobs.read(ready.map_hash!, 64 * 1024 * 1024),
        );
        const delivered = await database.db
          .selectFrom('map_versions')
          .select('preview_hash')
          .where('map_id', '=', ready.map_id!)
          .executeTakeFirstOrThrow();
        await writeFile(
          prefix + '.png',
          await pipeline.options.blobs.read(delivered.preview_hash!, 16 * 1024 * 1024),
        );
        await writeFile(prefix + '.json', JSON.stringify(ready, null, 2));
        await writeFile(prefix + '-progress.json', JSON.stringify(progress, null, 2));
        for (const [index, artifact] of progress.artifacts.entries()) {
          const blob = await studio.artifactBlob(account, thread, artifact.id);
          const stream = await harness.blobs.get(blob.storage_key);
          if (!stream) throw new Error('Stage image is missing from the evidence store.');
          const chunks: Buffer[] = [];
          for await (const chunk of stream) chunks.push(Buffer.from(chunk));
          await writeFile(
            `${prefix}-${artifact.stage}-${artifact.kind}-${index}.png`,
            Buffer.concat(chunks),
          );
        }
      }
      return id;
    }
    const first = await generate();
    await generate(first);
    expect(await studio.credits.balance(account)).toEqual({
      balance: 2,
      reserved: 0,
      available: 2,
    });
    expect(image).toHaveBeenCalledTimes(2);
    expect(
      (await studio.get(account, thread)).requests.filter((r) => r.kind === 'generate'),
    ).toHaveLength(2);
  }, 600000);
});
