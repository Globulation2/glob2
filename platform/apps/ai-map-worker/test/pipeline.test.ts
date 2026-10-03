// Real native integration; provider calls are replaced with deterministic reference-image delivery.
// GLOB2_BINARY and MAP_PYTHON enable the suite. No provider credentials or payment calls.
import { randomUUID } from 'node:crypto';
import { mkdir, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { afterAll, beforeAll, describe, expect, it, vi } from 'vitest';
import { sql } from 'kysely';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { createLogger, prepareJobQueue } from '@glob2/core';
import { Studio } from '@glob2/map-studio';
import type { EngineJob } from '@glob2/protocol';
import { createRunner, SIM, type RunnerHarness } from '../../engine-agent/test/support.ts';
import { Pipeline } from '../src/pipeline.ts';
import type { MapProvider } from '../src/provider.ts';
const repo = fileURLToPath(new URL('../../../../', import.meta.url));
const binary = process.env['GLOB2_BINARY'] ? resolve(repo, process.env['GLOB2_BINARY']) : undefined;
describe.runIf(binary)('native AI map delivery', () => {
  let database: TestDatabase, h: RunnerHarness, studio: Studio, pipeline: Pipeline;
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
    database = await createTestDatabase();
    await prepareJobQueue(database.pool, createLogger('studio-native-test', 'silent'));
    h = await createRunner(database.db, { binary: binary!, workdir: repo });
    studio = new Studio(database.db);
    pipeline = new Pipeline({
      studio,
      blobs: h.blobs,
      provider: { text, image },
      binary: binary!,
      source: repo,
      simVersion: SIM,
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
    await database?.drop();
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
            await h.blobs.read(String(row.checkpoints['preparationFailureHash']), 8 * 1024 * 1024),
          ).toString()
        : '';
      expect(row.status, (row.error ?? '') + diagnostics).toBe('importing');
      const jobId = String(row.checkpoints['importJob']);
      const job = await database.db
        .selectFrom('engine_jobs')
        .selectAll()
        .where('id', '=', jobId)
        .executeTakeFirstOrThrow();
      const result = await h.runner.run(
        { jobId, kind: 'import-ai-map', simVersion: SIM, payload: job.payload } as EngineJob,
        new AbortController().signal,
      );
      await sql`UPDATE engine_jobs SET status='succeeded',result=${JSON.stringify(result)}::jsonb WHERE id=${jobId}`.execute(
        database.db,
      );
      await sql`UPDATE studio_requests SET lease_until=NULL WHERE id=${id}`.execute(database.db);
      await pipeline.tick();
      const ready = (await studio.request(id))!;
      expect(ready.status, ready.error ?? '').toBe('ready');
      expect(ready.charged).toBe(true);
      expect(ready.checkpoints['validation']).toMatchObject({ passed: true });
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
      const evidence = process.env['GLOB2_EVIDENCE_DIR'];
      if (evidence) {
        await mkdir(evidence, { recursive: true });
        const prefix = join(evidence, parent ? 'revision' : 'initial');
        await writeFile(prefix + '.map', await h.blobs.read(ready.map_hash!, 64 * 1024 * 1024));
        const delivered = await database.db
          .selectFrom('map_versions')
          .select('preview_hash')
          .where('map_id', '=', ready.map_id!)
          .executeTakeFirstOrThrow();
        await writeFile(
          prefix + '.png',
          await h.blobs.read(delivered.preview_hash!, 16 * 1024 * 1024),
        );
        await writeFile(prefix + '.json', JSON.stringify(ready, null, 2));
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
