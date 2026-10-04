// Exercise persisted worker progress and refunds without a native binary or provider calls.
import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import { afterAll, beforeAll, expect, it, vi } from 'vitest';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { FsBlobStore } from '@glob2/core';
import { AgentBlobs } from '@glob2/engine/blobs';
import { Studio } from '@glob2/map-studio';
import { simVersionKey } from '@glob2/protocol';
import { Pipeline } from '../src/pipeline.ts';
let database: TestDatabase,
  directory: string,
  studio: Studio,
  blobs: AgentBlobs,
  pipeline: Pipeline;
const provider = { text: vi.fn(), image: vi.fn() };
beforeAll(async () => {
  database = await createTestDatabase();
  directory = await mkdtemp(join(tmpdir(), 'studio-progress-'));
  const worker = database.as('worker');
  studio = new Studio(worker.db);
  blobs = new AgentBlobs(new FsBlobStore(directory), worker.db);
  pipeline = new Pipeline({
    studio,
    blobs,
    provider,
    binary: 'unused',
    source: '.',
    simVersion: { versionMinor: 1, netProtocol: 1, dataHash: 'a'.repeat(64) },
    config: {
      enabled: true,
      salesEnabled: false,
      textModel: 'mock-text',
      imageModel: 'mock-image',
      pipelineVersion: 'test-v1',
      providerCallsPerDay: 20,
    },
  });
});
afterAll(async () => {
  await database?.drop();
  if (directory) await rm(directory, { recursive: true, force: true });
});

it.each([
  'null',
  '{not valid JSON',
  '{"schema_version":2,"map":{"width":128,"height":128,"player_slots":2}}',
])(
  'retains failed native artifacts and streams rejected checks before refunding once: %s',
  async (report) => {
    const account = (
      await database.db
        .insertInto('accounts')
        .values({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
        .returning('id')
        .executeTakeFirstOrThrow()
    ).id;
    await studio.credits.adjust(account, randomUUID(), 1, 'grant');
    const thread = (await studio.create(account, 'Progress test')).id;
    await sql`INSERT INTO studio_messages(id,thread_id,role,text) VALUES(${randomUUID()},${thread},'user','Two homes beside a lake')`.execute(
      database.db,
    );
    const id = randomUUID(),
      job = randomUUID();
    await studio.submit(
      account,
      thread,
      'generate',
      { id, settings: { width: 128, height: 128, players: 2 } },
      'test-v1',
    );
    const imageHash = await blobs.write(
      Buffer.from(
        'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAACklEQVR4nGMAAQAABQABDQottAAAAABJRU5ErkJggg==',
        'base64',
      ),
      'image/png',
    );
    const reportHash = await blobs.write(Buffer.from(report), 'application/json');
    const mapHash = await blobs.write(Buffer.from('unusable-map'), 'application/x-glob2-map');
    await database.db
      .insertInto('engine_jobs')
      .values({
        id: job,
        kind: 'import-ai-map',
        status: 'succeeded',
        sim_version: simVersionKey(pipeline.options.simVersion),
        payload: {},
        result: {
          mapHash,
          previewHash: imageHash,
          categoricalHash: imageHash,
          reportHash,
          previewWidth: 1,
          previewHeight: 1,
          size: 12,
          map: { width: 128, height: 128, teamCount: 2 },
        },
      })
      .execute();
    await sql`UPDATE studio_requests SET status='importing',checkpoints=checkpoints || ${JSON.stringify({ importJob: job })}::jsonb WHERE id=${id}`.execute(
      database.db,
    );
    await pipeline.tick();
    const failed = (await studio.request(id))!;
    expect(failed.status).toBe('failed');
    expect(failed.charged).toBe(false);
    expect(failed.checkpoints).toMatchObject({
      reportHash,
      categoricalHash: imageHash,
      previewHash: imageHash,
      validation: { passed: false },
    });
    const progress = await studio.progress(account, thread, id);
    expect(progress.stages).toEqual(
      expect.arrayContaining([expect.objectContaining({ id: 'checks', status: 'failed' })]),
    );
    expect(progress.checks).toHaveLength(13);
    expect(progress.checks.some((check) => check.status === 'failed')).toBe(true);
    expect(progress.artifacts.map((artifact) => artifact.kind)).toEqual(
      expect.arrayContaining(['categorical', 'preview']),
    );
    for (const artifact of progress.artifacts)
      expect(await studio.artifactBlob(account, thread, artifact.id)).toBeDefined();
    expect(await studio.credits.balance(account)).toEqual({
      available: 1,
      balance: 1,
      reserved: 0,
    });
    expect(await pipeline.tick()).toBe(false);
    expect(await studio.progress(account, thread, id)).toEqual(progress);
    const events = (
      await sql<{
        type: string;
        payload: { status?: string };
      }>`SELECT type,payload FROM studio_events WHERE request_id=${id} ORDER BY cursor`.execute(
        database.db,
      )
    ).rows;
    expect(
      events.findIndex((event) => event.type === 'check' && event.payload.status === 'failed'),
    ).toBeLessThan(events.findIndex((event) => event.type === 'complete'));
    expect(provider.text).not.toHaveBeenCalled();
    expect(provider.image).not.toHaveBeenCalled();
  },
);

it.each([
  { response: 'provider secret: not JSON', expected: 'The designer returned an invalid brief.' },
  { response: null, expected: 'The designer returned an invalid brief.' },
  {
    response: new Error('/private/provider-secret diagnostic'),
    expected: 'This request could not be completed. Any reserved credit was returned.',
  },
])(
  'keeps malformed discussion output and internal diagnostics out of player events',
  async ({ response, expected }) => {
    const account = (
      await database.db
        .insertInto('accounts')
        .values({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
        .returning('id')
        .executeTakeFirstOrThrow()
    ).id;
    await studio.credits.adjust(account, randomUUID(), 1, 'grant');
    const thread = (await studio.create(account, 'Safe failures')).id,
      id = randomUUID();
    await studio.submit(account, thread, 'chat', { id, text: 'Design two islands' }, 'test-v1');
    if (response instanceof Error) provider.text.mockRejectedValueOnce(response);
    else
      provider.text.mockResolvedValueOnce({
        text: response === null ? 'null' : response,
        usage: {},
      });
    await pipeline.tick();
    const row = (await studio.request(id))!;
    expect(row.status).toBe('failed');
    expect(row.error).toBe(expected);
    if (response instanceof Error)
      expect(row.checkpoints['failureDiagnosticHash']).toBeTypeOf('string');
    const events = await sql`SELECT payload FROM studio_events WHERE request_id=${id}`.execute(
      database.db,
    );
    expect(JSON.stringify(events.rows)).not.toContain('provider secret');
    expect(JSON.stringify(events.rows)).not.toContain('/private/');
  },
);
