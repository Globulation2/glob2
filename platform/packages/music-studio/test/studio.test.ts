import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import { beforeAll, afterAll, it, expect } from 'vitest';
import { createTestDatabase, type TestDatabase } from '../../db/test/support.ts';
import { Credits } from '@glob2/billing';
import { MusicStudio, type Delivery } from '../src/studio.ts';
let database: TestDatabase, studio: MusicStudio;
beforeAll(async () => {
  database = await createTestDatabase();
  studio = new MusicStudio(database.db);
});
afterAll(async () => {
  await database?.drop();
});
const settings = { pipeline: 'acoustic-v1', seed: 4 } as const;
async function fixture(credits = 3) {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  await studio.credits.adjust(account, randomUUID(), credits, 'grant');
  const thread = (await studio.create(account, 'Moss and morning')).id;
  await sql`INSERT INTO music_studio_messages(id,thread_id,role,text) VALUES(${randomUUID()},${thread},'user','Warm flute and harp')`.execute(
    database.db,
  );
  return { account, thread };
}
async function submit() {
  const f = await fixture();
  const { id } = await studio.submit(
    f.account,
    f.thread,
    'generate',
    { id: randomUUID(), settings },
    'music-v1',
  );
  return { ...f, row: (await studio.request(id))! };
}
async function delivery(): Promise<Delivery> {
  const hash = 'a'.repeat(64);
  await database.db
    .insertInto('blobs')
    .values({ sha256: hash, size: 10, storage_key: 'test', content_type: 'audio/ogg' })
    .onConflict((c) => c.doNothing())
    .execute();
  return {
    metadata: {
      title: 'Morning',
      artist: 'Composer',
      description: '',
      license: 'CC-BY-4.0',
      credits: 'AI-composed',
      sources: [],
      tags: [],
      aiGenerated: true,
    },
    result: { frames: 2880000, tracks: [], warnings: [] },
    checks: [],
    assets: ['calm', 'building', 'combat'].map((kind) => ({ kind, hash })),
  };
}
it('keeps music balances separate and requires available music credit', async () => {
  const { account, thread } = await fixture(0);
  await new Credits(database.db, 'maps').adjust(account, randomUUID(), 5, 'grant');
  await new Credits(database.db, 'hive').adjust(account, randomUUID(), 5, 'grant');
  await expect(
    studio.submit(account, thread, 'generate', { id: randomUUID(), settings }, 'music-v1'),
  ).rejects.toThrow('Buy music credits');
});
it('reserves once, rejects changed retry input, and refunds failure once', async () => {
  const { account, thread } = await fixture();
  const input = { id: randomUUID(), settings };
  await Promise.all(
    Array.from({ length: 5 }, () => studio.submit(account, thread, 'generate', input, 'music-v1')),
  );
  expect(await studio.credits.balance(account)).toEqual({ balance: 3, reserved: 1, available: 2 });
  await expect(
    studio.submit(
      account,
      thread,
      'generate',
      { ...input, settings: { ...settings, seed: 8 } },
      'music-v1',
    ),
  ).rejects.toThrow('retry identifier');
  const row = (await studio.request(input.id))!;
  await Promise.all([
    studio.finish(row, undefined, 'Failed QA'),
    studio.finish(row, undefined, 'Failed QA'),
  ]);
  expect(await studio.credits.balance(account)).toEqual({ balance: 3, reserved: 0, available: 3 });
});
it('atomically delivers an immutable private release and charges once', async () => {
  const { account, thread, row } = await submit(),
    result = await delivery();
  await Promise.all([studio.finish(row, result), studio.finish(row, result)]);
  expect(await studio.credits.balance(account)).toEqual({ balance: 2, reserved: 0, available: 2 });
  const saved = await database.db
    .selectFrom('music_releases')
    .selectAll()
    .where('id', '=', row.id)
    .executeTakeFirstOrThrow();
  expect(saved.status).toBe('ready');
  expect(saved.authoring).toMatchObject({ requestId: row.id });
  const { id } = await studio.submit(
    account,
    thread,
    'generate',
    { id: randomUUID(), settings, parent: row.id },
    'music-v1',
  );
  expect((await studio.request(id))?.input.parent).toBe(row.id);
  await studio.cancel(account, thread, id);
  await studio.cancel(account, thread, id);
  expect((await studio.credits.balance(account)).reserved).toBe(0);
});
it('fences cancelled/stale workers and keeps history owner-only', async () => {
  const { account, thread, row } = await submit(),
    other = await fixture();
  await expect(studio.get(other.account, thread)).rejects.toThrow('No such music thread');
  await studio.cancel(account, thread, row.id);
  await expect(studio.stage(row, 'render', 'running')).rejects.toThrow('lease expired');
  const request = await studio.request(row.id);
  expect(request?.status).toBe('failed');
});
it('orders events, records repair cycles, and protects artifacts', async () => {
  const { account, thread, row } = await submit();
  await studio.stage(row, 'render', 'running', 'Candidate 1');
  await studio.stage(row, 'render', 'complete', 'Candidate 1');
  await studio.stage(row, 'render', 'running', 'Candidate 2');
  await studio.check(row, {
    id: '1:format',
    label: 'format',
    attempt: 1,
    status: 'fail',
    measures: [],
  });
  await studio.check(row, {
    id: '2:format',
    label: 'format',
    attempt: 2,
    status: 'pass',
    measures: [],
  });
  await studio.text(row, 'Refining the bass line', 2);
  const progress = await studio.progress(account, thread, row.id);
  expect(progress.stages.find((s) => s.id === 'render')?.detail).toBe('Candidate 2');
  expect(progress.checks).toHaveLength(2);
  expect(progress.notes).toHaveLength(1);
  const snapshot = await studio.get(account, thread);
  await studio.stage(row, 'master', 'running');
  const next = await studio.events(account, thread, snapshot.cursor!);
  expect(next).toHaveLength(1);
  expect(BigInt(next[0]!.id)).toBeGreaterThan(BigInt(snapshot.cursor!));
  await studio.finish(row, undefined, 'Test completed.');
});
it('requires reconciliation after unknown provider outcomes and preserves daily usage after deletion', async () => {
  const { account, thread, row } = await submit();
  await sql`UPDATE music_studio_requests SET status='dispatched',lease_until=now()-interval '1 second' WHERE id=${row.id}`.execute(
    database.db,
  );
  await sql`INSERT INTO music_studio_attempts(id,request_id,stage,model,status,input) VALUES(${randomUUID()},${row.id},'call','test','dispatched','{}')`.execute(
    database.db,
  );
  await studio.recoverUncertain();
  expect((await studio.request(row.id))?.status).toBe('uncertain');
  await expect(studio.cancel(account, thread, row.id)).rejects.toThrow('provider outcome');
  expect((await studio.credits.balance(account)).reserved).toBe(1);
  await studio.finish((await studio.request(row.id))!, undefined, 'Reconciled');
  await database.db.deleteFrom('music_studio_threads').where('id', '=', thread).execute();
  expect(
    (
      await sql<{
        n: string;
      }>`SELECT sum(calls)::text AS n FROM music_studio_provider_usage`.execute(database.db)
    ).rows[0]?.n,
  ).toBe('1');
});

it('deleting history preserves free-chat rate limits', async () => {
  const { account, thread } = await fixture();
  const { id } = await studio.submit(
    account,
    thread,
    'chat',
    { id: randomUUID(), text: 'A flute melody' },
    'music-v1',
    1,
  );
  const row = (await studio.request(id))!;
  await studio.finish(row, { text: 'A warm flute melody', brief: 'Flute' });
  await studio.remove(account, thread);
  const next = (await studio.create(account, 'Next project')).id;
  await expect(
    studio.submit(account, next, 'chat', { id, text: 'Reused identity' }, 'music-v1', 1),
  ).rejects.toThrow('deleted history');
  await expect(
    studio.submit(
      account,
      next,
      'chat',
      { id: randomUUID(), text: 'Another melody' },
      'music-v1',
      1,
    ),
  ).rejects.toThrow('Please wait');
});

it('recovers a journalled rejection without reconciliation and returns its reservation once', async () => {
  const { account, row } = await submit();
  await sql`UPDATE music_studio_requests SET status='dispatched',lease_until=now()-interval '1 second' WHERE id=${row.id}`.execute(
    database.db,
  );
  await sql`INSERT INTO music_studio_attempts(id,request_id,stage,model,status,input) VALUES(${randomUUID()},${row.id},'agent:0','test','failed','{}')`.execute(
    database.db,
  );
  await studio.recoverUncertain();
  expect((await studio.request(row.id))?.status).toBe('processing');
  const recovered = (await studio.claim())!;
  expect(recovered.id).toBe(row.id);
  await Promise.all([
    studio.finish(recovered, undefined, 'Provider rejected the request.'),
    studio.finish(recovered, undefined, 'Provider rejected the request.'),
  ]);
  expect(await studio.credits.balance(account)).toEqual({ balance: 3, reserved: 0, available: 3 });
  expect(
    await database.db
      .selectFrom('music_ledger')
      .select('id')
      .where('id', '=', `generation:${row.id}`)
      .execute(),
  ).toHaveLength(1);
});

it('keeps an ambiguous later provider call uncertain even after a recorded rejection', async () => {
  const { account, row } = await submit();
  await sql`UPDATE music_studio_requests SET status='dispatched',lease_until=now()-interval '1 second' WHERE id=${row.id}`.execute(
    database.db,
  );
  for (const [stage, status] of [
    ['agent:0', 'failed'],
    ['agent:1', 'dispatched'],
  ] as const)
    await sql`INSERT INTO music_studio_attempts(id,request_id,stage,model,status,input) VALUES(${randomUUID()},${row.id},${stage},'test',${status},'{}')`.execute(
      database.db,
    );
  await studio.recoverUncertain();
  expect((await studio.request(row.id))?.status).toBe('uncertain');
  expect((await studio.credits.balance(account)).reserved).toBe(1);
  await studio.finish((await studio.request(row.id))!, undefined, 'Reconciled.');
});

it('rejects delivery and checkpoints from an old lease after another worker claims the request', async () => {
  const { account, row } = await submit();
  const stale = (await studio.claim())!;
  expect(stale.id).toBe(row.id);
  await sql`UPDATE music_studio_requests SET lease_until=now()-interval '1 second' WHERE id=${row.id}`.execute(
    database.db,
  );
  const current = (await studio.claim())!;
  expect(current.id).toBe(row.id);
  expect(current.lease).not.toBe(stale.lease);
  const result = await delivery();
  await expect(studio.finish(stale, result)).rejects.toThrow('lease expired');
  await expect(studio.checkpoint(stale, 'processing', { overwritten: true })).rejects.toThrow(
    'lease expired',
  );
  expect(await studio.heartbeat(stale)).toBe(false);
  expect(await studio.credits.balance(account)).toEqual({ balance: 3, reserved: 1, available: 2 });
  await studio.finish(current, result);
  expect(await studio.credits.balance(account)).toEqual({ balance: 2, reserved: 0, available: 2 });
});

it('a discussion turn completes without a generation or reservation', async () => {
  const { account, thread } = await fixture();
  const input = { id: randomUUID(), text: 'What instruments would suit this?', settings };
  await studio.submit(account, thread, 'chat', input, 'music-v1', 60, undefined, true);
  const row = (await studio.request(input.id))!;
  await studio.finish(row, {
    text: 'Consider flute and harp.',
    brief: 'Flute and harp',
    action: 'discuss',
  });
  expect(
    (await studio.get(account, thread)).requests.filter((r) => r.kind === 'generate'),
  ).toHaveLength(0);
  expect((await studio.credits.balance(account)).reserved).toBe(0);
});
it('retries and replayed turn completions enqueue exactly one paid build with its persisted identity', async () => {
  const { account, thread } = await fixture();
  const input = { id: randomUUID(), text: 'Compose a flute soundtrack', settings };
  await Promise.all(
    Array.from({ length: 5 }, () =>
      studio.submit(account, thread, 'chat', input, 'music-v1', 60, undefined, true),
    ),
  );
  const row = (await studio.request(input.id))!;
  const generationId = row.checkpoints['generationId'];
  await expect(
    studio.submit(
      account,
      thread,
      'chat',
      { ...input, settings: { ...settings, seed: 5 } },
      'music-v1',
      60,
      undefined,
      true,
    ),
  ).rejects.toThrow('retry identifier');
  await Promise.all(
    Array.from({ length: 5 }, () =>
      studio.finish(row, {
        text: 'Creating your flute soundtrack.',
        brief: 'Flute',
        action: 'build',
      }),
    ),
  );
  const builds = (await studio.get(account, thread)).requests.filter((r) => r.kind === 'generate');
  expect(builds.map((r) => r.id)).toEqual([generationId]);
  expect(await studio.credits.balance(account)).toEqual({ balance: 3, reserved: 1, available: 2 });
  const build = (await studio.request(String(generationId)))!;
  await Promise.all([
    studio.finish(build, await delivery()),
    studio.finish(build, await delivery()),
  ]);
  expect(await studio.credits.balance(account)).toEqual({ balance: 2, reserved: 0, available: 2 });
});
