import { createHash, randomUUID } from 'node:crypto';
import { resolve } from 'node:path';
import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { beforeAll, afterAll, it, expect, vi } from 'vitest';
import { sql } from 'kysely';
import { createTestDatabase, type TestDatabase } from '../../../packages/db/test/support.ts';
import { MusicStudio } from '@glob2/music-studio';
import { AgentBlobs } from '@glob2/engine/blobs';
import { FsBlobStore } from '@glob2/core';
import type { MusicStudioConfig } from '@glob2/protocol';
import { Pipeline } from '../src/pipeline.ts';
import {
  Attempts,
  ProviderRejected,
  ProviderUncertain,
  type MusicProvider,
} from '../src/provider.ts';
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
      metadata: {
        title: 'Test melody',
        artist: 'Globulation 2 Music Studio',
        description: 'Original instrumental music',
        license: 'CC-BY-4.0',
        credits: 'AI composed',
        sources: [],
        tags: [],
        aiGenerated: true,
      },
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
      result: {
        frames: 2880000,
        tracks: (['calm', 'building', 'combat'] as const).map((mood, i) => {
          const bytes = Buffer.from(`a${i + 1}.opus`);
          return {
            mood,
            sha256: createHash('sha256').update(bytes).digest('hex'),
            bytes: bytes.length,
            url: '',
            waveform: [],
          };
        }),
        warnings: [],
      },
    },
  };
}
function worker(provider: MusicProvider, runner: MusicRunner, config = cfg) {
  return new Pipeline(
    studio,
    blobs,
    provider,
    runner,
    config,
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
  const row = (await studio.request(id))!;
  const dispatch = vi.fn();
  await expect(
    new Attempts(studio, 100).run(row, 'agent:0', 'changed-model', {}, dispatch),
  ).rejects.toBeInstanceOf(ProviderUncertain);
  expect(dispatch).not.toHaveBeenCalled();
});

it('reuses a completed provider response only for identical model and inputs', async () => {
  const { id } = await fixture();
  const row = (await studio.claim())!;
  const journal = new Attempts(studio, 100);
  const reply = { text: '{"action":"write","value":"# recovered source"}', usage: {} };
  await journal.run(row, 'agent:0', 'test', { promptHash: 'saved' }, async () => reply);
  const dispatch = vi.fn();
  expect(await journal.run(row, 'agent:0', 'test', { promptHash: 'saved' }, dispatch)).toEqual(
    reply,
  );
  await expect(
    journal.run(row, 'agent:0', 'test', { promptHash: 'changed' }, dispatch),
  ).rejects.toThrow(/different inputs/);
  expect(dispatch).not.toHaveBeenCalled();
  await studio.cancel(row.account_id, row.thread_id, id);
});

it('replays a known failed provider outcome as a terminal rejection', async () => {
  const { id } = await fixture();
  const row = (await studio.claim())!;
  const journal = new Attempts(studio, 100);
  await expect(
    journal.run(row, 'agent:0', 'test', {}, async () => {
      throw new ProviderRejected('Rejected');
    }),
  ).rejects.toBeInstanceOf(ProviderRejected);
  expect((await studio.request(id))?.status).toBe('processing');
  const dispatch = vi.fn();
  await expect(journal.run(row, 'agent:0', 'test', {}, dispatch)).rejects.toBeInstanceOf(
    ProviderRejected,
  );
  expect(dispatch).not.toHaveBeenCalled();
  await studio.cancel(row.account_id, row.thread_id, id);
});

it('recovers completed candidate bytes without repeating a render or provider call', async () => {
  const { id } = await fixture();
  let calls = 0;
  const provider: MusicProvider = {
    text: async () => ({
      text: JSON.stringify(
        calls++ === 0 ? { action: 'write', value: '# melody' } : { action: 'render' },
      ),
      usage: {},
    }),
  };
  const render = vi.fn(async () => candidate(true));
  const original = studio.checkpoint.bind(studio);
  const stopped = new AbortController();
  const checkpoint = vi.spyOn(studio, 'checkpoint').mockImplementation(async (...args) => {
    await original(...args);
    const state = args[2]['agent'] as { candidate?: { completed?: unknown } } | undefined;
    if (state?.candidate?.completed) {
      stopped.abort();
      throw Error('worker died');
    }
  });
  const finish = vi.spyOn(studio, 'finish').mockRejectedValue(Error('worker died'));
  await expect(worker(provider, { run: render }).tick(stopped.signal)).rejects.toThrow(
    'worker died',
  );
  checkpoint.mockRestore();
  finish.mockRestore();
  await sql`UPDATE music_studio_requests SET lease_until=now()-interval '1 second' WHERE id=${id}`.execute(
    database.db,
  );
  await worker(provider, { run: render }).tick();
  expect((await studio.request(id))?.status).toBe('ready');
  expect(render).toHaveBeenCalledTimes(1);
  expect(calls).toBe(2);
});

it('keeps the three-cycle limit across repeated worker deaths during rendering', async () => {
  const { id } = await fixture();
  let calls = 0,
    renders = 0;
  const provider: MusicProvider = {
    text: async () => ({
      text: JSON.stringify(
        calls++ === 0 ? { action: 'write', value: '# melody' } : { action: 'render' },
      ),
      usage: {},
    }),
  };
  for (let cycle = 1; cycle <= 3; cycle++) {
    const stopped = new AbortController();
    const finish = vi.spyOn(studio, 'finish').mockRejectedValue(Error('worker died'));
    await expect(
      worker(provider, {
        run: async () => {
          renders++;
          stopped.abort();
          throw Error('worker died');
        },
      }).tick(stopped.signal),
    ).rejects.toThrow('worker died');
    finish.mockRestore();
    expect(((await studio.request(id))?.checkpoints['agent'] as { renders: number }).renders).toBe(
      cycle,
    );
    await sql`UPDATE music_studio_requests SET lease_until=now()-interval '1 second' WHERE id=${id}`.execute(
      database.db,
    );
  }
  await worker(provider, {
    run: async () => {
      throw Error('must not render again');
    },
  }).tick();
  expect(renders).toBe(3);
  expect((await studio.request(id))?.status).toBe('failed');
  expect((await studio.request(id))?.charged).toBe(false);
});

it('fails expired recovered chat before dispatching a provider request', async () => {
  const { account, thread, id } = await fixture();
  await studio.cancel(account, thread, id);
  const chat = await studio.submit(
    account,
    thread,
    'chat',
    { id: randomUUID(), text: 'Discuss the melody' },
    'music-v1',
    30,
    cfg,
  );
  const row = (await studio.claim())!;
  await studio.checkpoint(row, 'processing', { deadline: Date.now() - 1000 });
  await sql`UPDATE music_studio_requests SET lease_until=now()-interval '1 second' WHERE id=${chat.id}`.execute(
    database.db,
  );
  const text = vi.fn();
  await worker({ text }, { run: vi.fn() }).tick();
  expect(text).not.toHaveBeenCalled();
  expect((await studio.request(chat.id))?.status).toBe('failed');
  expect(
    (
      await sql`SELECT id FROM music_studio_attempts WHERE request_id=${chat.id}`.execute(
        database.db,
      )
    ).rows,
  ).toHaveLength(0);
});

it('rejects inconsistent audio bytes and unfinished validation', async () => {
  for (const corrupt of [
    (value: Rendered) => {
      delete value.report.metadata;
    },
    (value: Rendered) => {
      value.report.result!.tracks[0]!.sha256 = '0'.repeat(64);
    },
    (value: Rendered) => {
      value.report.checks[0]!.status = 'running';
    },
    (value: Rendered) => {
      value.report.checks.push({ ...value.report.checks[0]!, status: 'fail' });
    },
  ]) {
    const { id } = await fixture();
    let calls = 0;
    const value = candidate(true);
    corrupt(value);
    await worker(
      {
        text: async () => ({
          text: JSON.stringify(
            calls++ === 0 ? { action: 'write', value: '# music' } : { action: 'render' },
          ),
          usage: {},
        }),
      },
      { run: async () => value },
    ).tick();
    expect((await studio.request(id))?.status).toBe('failed');
    expect((await studio.request(id))?.release_id).toBeNull();
  }
});

it.each([false, true])(
  'recovers a lost provider-journal COMMIT acknowledgement (rejected=%s)',
  async (rejected) => {
    const { id } = await fixture();
    const row = (await studio.claim())!;
    const transact = studio.db.transaction.bind(studio.db);
    let commits = 0;
    const spy = vi.spyOn(studio.db, 'transaction').mockImplementation(() => {
      const builder = transact();
      const execute = builder.execute.bind(builder);
      builder.execute = (async (callback: Parameters<typeof execute>[0]) => {
        const result = await execute(callback);
        if (++commits === 2) throw Error('lost commit acknowledgement');
        return result;
      }) as typeof builder.execute;
      return builder;
    });
    const reply = { text: 'saved reply', usage: {} };
    try {
      const result = new Attempts(studio, 100).run(row, 'agent:0', 'test', {}, async () => {
        if (rejected) throw new ProviderRejected('known rejection');
        return reply;
      });
      if (rejected) await expect(result).rejects.toBeInstanceOf(ProviderRejected);
      else expect(await result).toEqual(reply);
    } finally {
      spy.mockRestore();
    }
    expect((await studio.request(id))?.status).toBe('processing');
    await studio.cancel(row.account_id, row.thread_id, id);
  },
);

it('applies a lowered operator daily limit to requests with an older budget snapshot', async () => {
  const { id } = await fixture();
  await sql`INSERT INTO music_studio_provider_usage(day,calls) VALUES((now() AT TIME ZONE 'UTC')::date,1) ON CONFLICT(day) DO NOTHING`.execute(
    database.db,
  );
  const text = vi.fn(async () => ({ text: '{"action":"read","value":"guide"}', usage: {} }));
  await worker({ text }, { run: vi.fn() }, { ...cfg, providerCallsPerDay: 1 }).tick();
  expect(text).not.toHaveBeenCalled();
  expect((await studio.request(id))?.status).toBe('failed');
  expect((await studio.request(id))?.error).toContain('daily service limit');
  expect((await studio.request(id))?.charged).toBe(false);
});
