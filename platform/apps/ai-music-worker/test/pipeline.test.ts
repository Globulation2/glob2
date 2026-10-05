import { randomUUID } from 'node:crypto';
import { resolve } from 'node:path';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { beforeAll, afterAll, it, expect } from 'vitest';
import { sql } from 'kysely';
import { createTestDatabase, type TestDatabase } from '../../../packages/db/test/support.ts';
import { MusicStudio } from '@glob2/music-studio';
import { AgentBlobs } from '@glob2/engine/blobs';
import { FsBlobStore } from '@glob2/core';
import type { MusicStudioConfig } from '@glob2/protocol';
import { Pipeline } from '../src/pipeline.ts';
import { Attempts, ProviderUncertain, type MusicProvider } from '../src/provider.ts';
import type { MusicRunner, Rendered } from '../src/runner.ts';
let database: TestDatabase, studio: MusicStudio, blobs: AgentBlobs, directory: string;
const cfg: MusicStudioConfig = {
  enabled: true,
  salesEnabled: false,
  textModel: 'test',
  pipelineVersion: 'music-v1',
  providerCallsPerDay: 100,
  maxCalls: 12,
  maxOutputTokens: 16000,
  maxTotalTokens: 500000,
  timeoutSeconds: 1800,
};
beforeAll(async () => {
  database = await createTestDatabase();
  studio = new MusicStudio(database.db);
  directory = await mkdtemp(resolve(tmpdir(), 'music-test-'));
  blobs = new AgentBlobs(new FsBlobStore(directory), database.db);
});
afterAll(async () => {
  await database?.drop();
  if (directory) await rm(directory, { recursive: true, force: true });
});
async function fixture() {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: randomUUID().slice(0, 20) })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  await studio.credits.adjust(account, randomUUID(), 4, 'grant');
  const thread = (await studio.create(account, 'Flute theme')).id;
  await sql`INSERT INTO music_studio_messages(id,thread_id,role,text) VALUES(${randomUUID()},${thread},'user','Make a warm flute theme')`.execute(
    database.db,
  );
  const { id } = await studio.submit(
    account,
    thread,
    'generate',
    { id: randomUUID(), settings: { pipeline: 'acoustic-v1', seed: 0 } },
    'music-v1',
    30,
    cfg,
  );
  return { account, thread, id };
}
function candidate(pass: boolean): Rendered {
  return {
    score: '{}',
    files: Object.fromEntries(
      ['a1.opus', 'a2.opus', 'a3.opus', 'preview.opus', 'waveforms.json', 'set.zip'].map((n) => [
        n,
        Buffer.from(n),
      ]),
    ),
    report: {
      passed: pass,
      checks: [
        'score',
        'format',
        'loudness',
        'seam',
        'repetition',
        'alignment',
        'contrast',
        'noise',
        'balance',
        'audibility',
        'dropout',
      ].map((name) => ({
        name,
        status: pass ? 'pass' : 'fail',
        measures: [
          {
            name: name + '.test',
            status: pass ? 'pass' : 'fail',
            value: 0,
            threshold: 'fixture',
            detail: '',
          },
        ],
      })),
      result: { frames: 2880000, tracks: [], warnings: [] },
    },
  };
}
function worker(provider: MusicProvider, runner: MusicRunner) {
  return new Pipeline(
    studio,
    blobs,
    provider,
    runner,
    cfg,
    resolve('../tools/music'),
    'https://music.invalid',
  );
}
it('repairs failed candidates, retains evidence, and edits the exact parent source', async () => {
  const { account, thread, id } = await fixture();
  let calls = 0,
    renders = 0;
  const actions = [
    { action: 'write', value: '# initial melody' },
    { action: 'render' },
    { action: 'write', value: '# repaired bass line' },
    { action: 'render' },
  ];
  const provider: MusicProvider = {
    text: async () => ({
      text: JSON.stringify(actions[calls++]),
      usage: { input_tokens: 100, output_tokens: 100 },
    }),
  };
  const runner: MusicRunner = { run: async () => candidate(++renders > 1) };
  await worker(provider, runner).tick();
  expect((await studio.request(id))?.status).toBe('ready');
  expect(renders).toBe(2);
  expect((await studio.credits.balance(account)).balance).toBe(3);
  const progress = await studio.progress(account, thread, id);
  expect(progress.checks.some((c) => c.status === 'fail')).toBe(true);
  expect(progress.checks.some((c) => c.attempt === 2 && c.status === 'pass')).toBe(true);
  const { id: revision } = await studio.submit(
    account,
    thread,
    'generate',
    { id: randomUUID(), settings: { pipeline: 'acoustic-v1', seed: 0 }, parent: id },
    'music-v1',
    30,
    cfg,
  );
  const revisionProvider: MusicProvider = {
    text: async (_m, prompt) => {
      expect(prompt).toContain('# repaired bass line');
      return { text: '{"action":"render"}', usage: { input_tokens: 100, output_tokens: 100 } };
    },
  };
  await worker(revisionProvider, runner).tick();
  expect((await studio.request(revision))?.status).toBe('ready');
  expect((await studio.request(id))?.release_id).toBe(id);
});
it('stops after three failed renders and refunds the reservation', async () => {
  const { account, id } = await fixture();
  let calls = 0,
    renders = 0;
  await worker(
    {
      text: async () => ({
        text: JSON.stringify(
          calls++ === 0 ? { action: 'write', value: '# source' } : { action: 'render' },
        ),
        usage: { input_tokens: 1, output_tokens: 1 },
      }),
    },
    {
      run: async () => {
        renders++;
        return candidate(false);
      },
    },
  ).tick();
  expect(renders).toBe(3);
  expect((await studio.request(id))?.status).toBe('failed');
  expect(await studio.credits.balance(account)).toEqual({ balance: 4, reserved: 0, available: 4 });
});
it('refuses a passing headline with missing mandatory checks', async () => {
  const { id } = await fixture();
  let calls = 0;
  const bad = candidate(true);
  bad.report.checks = [];
  await worker(
    {
      text: async () => ({
        text: JSON.stringify(
          calls++ === 0 ? { action: 'write', value: '# source' } : { action: 'render' },
        ),
        usage: {},
      }),
    },
    { run: async () => bad },
  ).tick();
  expect((await studio.request(id))?.status).toBe('failed');
  expect((await studio.request(id))?.release_id).toBeNull();
});
it('never dispatches an uncertain provider request twice', async () => {
  const { account, id } = await fixture();
  let calls = 0;
  const pipeline = worker(
    {
      text: async () => {
        calls++;
        throw new ProviderUncertain('Lost response');
      },
    },
    { run: async () => candidate(true) },
  );
  await pipeline.tick();
  await pipeline.tick();
  expect(calls).toBe(1);
  expect((await studio.request(id))?.status).toBe('uncertain');
  expect((await studio.credits.balance(account)).reserved).toBe(1);
});

it('reuses a completed provider response after losing the worker lease', async () => {
  const { id } = await fixture();
  const row = (await studio.claim())!;
  expect(row.id).toBe(id);
  await new Attempts(studio, 100).run(row, 'agent:0', 'test', {}, async () => ({
    text: '{"action":"write","value":"# recovered source"}',
    usage: { input_tokens: 10, output_tokens: 10 },
  }));
  await sql`UPDATE music_studio_requests SET lease_until=now()-interval '1 second' WHERE id=${id}`.execute(
    database.db,
  );
  let calls = 0;
  await worker(
    {
      text: async (_model, prompt) => {
        calls++;
        expect(prompt).toContain('# recovered source');
        return { text: '{"action":"render"}', usage: { input_tokens: 10, output_tokens: 10 } };
      },
    },
    { run: async () => candidate(true) },
  ).tick();
  expect(calls).toBe(1);
  expect((await studio.request(id))?.status).toBe('ready');
});
