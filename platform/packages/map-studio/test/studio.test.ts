import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import { beforeAll, afterAll, it, expect } from 'vitest';
import { createTestDatabase, type TestDatabase } from '../../db/test/support.ts';
import { Credits } from '@glob2/billing';
import { Studio } from '../src/studio.ts';
let database: TestDatabase, studio: Studio;
beforeAll(async () => {
  database = await createTestDatabase();
  studio = new Studio(database.db);
});
afterAll(async () => {
  await database?.drop();
});
async function fixture(credits = 3) {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  await new Credits(database.db, 'maps').adjust(account, randomUUID(), credits, 'grant');
  const thread = (await studio.create(account, 'Pond meadows')).id;
  await sql`INSERT INTO studio_messages(id,thread_id,role,text) VALUES(${randomUUID()},${thread},'user','Four homes beside ponds')`.execute(
    database.db,
  );
  return { account, thread };
}
const settings = { width: 256, height: 256, players: 4 } as const;
it('isolates map and Hive wallets and requires prepaid map credits', async () => {
  const { account, thread } = await fixture(0);
  await new Credits(database.db).adjust(account, randomUUID(), 50000, 'grant');
  await expect(
    studio.submit(account, thread, 'generate', { id: randomUUID(), settings }, 'v1'),
  ).rejects.toThrow('Buy map credits');
  expect((await studio.credits.balance(account)).balance).toBe(0);
});
it('reserves exactly once across concurrent duplicate submissions', async () => {
  const { account, thread } = await fixture();
  const input = { id: randomUUID(), settings };
  await Promise.all(
    Array.from({ length: 5 }, () => studio.submit(account, thread, 'generate', input, 'v1')),
  );
  expect(await studio.credits.balance(account)).toEqual({ balance: 3, reserved: 1, available: 2 });
  expect((await studio.get(account, thread)).requests).toHaveLength(1);
  await expect(
    studio.submit(
      account,
      thread,
      'generate',
      { ...input, settings: { ...settings, players: 3 } },
      'v1',
    ),
  ).rejects.toThrow('retry identifier');
});
it('permits only one active request per account across threads', async () => {
  const { account, thread } = await fixture(),
    other = (await studio.create(account, 'Other')).id;
  await sql`INSERT INTO studio_messages(id,thread_id,role,text) VALUES(${randomUUID()},${other},'user','River')`.execute(
    database.db,
  );
  const results = await Promise.allSettled([
    studio.submit(account, thread, 'generate', { id: randomUUID(), settings }, 'v1'),
    studio.submit(account, other, 'generate', { id: randomUUID(), settings }, 'v1'),
  ]);
  expect(results.filter((r) => r.status === 'fulfilled')).toHaveLength(1);
  expect((await studio.credits.balance(account)).reserved).toBe(1);
});
it('returns a failed generation credit once and retains failure history', async () => {
  const { account, thread } = await fixture();
  const { id } = await studio.submit(
    account,
    thread,
    'generate',
    { id: randomUUID(), settings },
    'v1',
  );
  const row = await studio.request(id);
  await Promise.all([
    studio.finish(row!, undefined, 'Invalid starts'),
    studio.finish(row!, undefined, 'Invalid starts'),
  ]);
  expect(await studio.credits.balance(account)).toEqual({ balance: 3, reserved: 0, available: 3 });
  expect((await studio.request(id))?.status).toBe('failed');
});
it('charges once only after creating an immutable private catalog artifact', async () => {
  const { account, thread } = await fixture();
  const { id } = await studio.submit(
    account,
    thread,
    'generate',
    { id: randomUUID(), settings },
    'v1',
  );
  const row = (await studio.request(id))!;
  const mapHash = randomUUID().replaceAll('-', '').repeat(2),
    previewHash = randomUUID().replaceAll('-', '').repeat(2);
  await database.db
    .insertInto('blobs')
    .values([
      {
        sha256: mapHash,
        size: 42,
        content_type: 'application/x-glob2-map',
        storage_key: 'map',
        visibility: 'private',
      },
      {
        sha256: previewHash,
        size: 42,
        content_type: 'image/png',
        storage_key: 'preview',
        visibility: 'private',
      },
    ])
    .execute();
  const result = {
    mapHash,
    previewHash,
    width: 256,
    height: 256,
    players: 4,
    size: 42,
    previewWidth: 512,
    previewHeight: 512,
    simVersion: `125-49-${'a'.repeat(64)}`,
    provenance: { contract: 'test' },
  };
  await Promise.all([studio.finish(row, result), studio.finish(row, result)]);
  expect(await studio.credits.balance(account)).toEqual({ balance: 2, reserved: 0, available: 2 });
  const ready = (await studio.request(id))!;
  expect(ready.charged).toBe(true);
  expect(
    await database.db
      .selectFrom('maps')
      .select('visibility')
      .where('id', '=', ready.map_id!)
      .executeTakeFirst(),
  ).toEqual({ visibility: 'private' });
  await expect(
    studio.submit(
      account,
      thread,
      'generate',
      { id: randomUUID(), settings: { ...settings, width: 128 }, parent: id },
      'v1',
    ),
  ).rejects.toThrow('Start fresh');
});
it('discussion costs no map credit and persists the accumulated brief', async () => {
  const { account, thread } = await fixture();
  const id = randomUUID();
  await studio.submit(account, thread, 'chat', { id, text: 'Make the river wider' }, 'v1');
  const row = (await studio.request(id))!;
  await studio.finish(row, {
    text: 'I will preserve the homes and widen the river.',
    brief: 'Four homes beside ponds; a wider river with walking bridges.',
  });
  expect((await studio.credits.balance(account)).balance).toBe(3);
  await studio.submit(account, thread, 'generate', { id: randomUUID(), settings }, 'v1');
  const latest = (await studio.get(account, thread)).requests.at(-1);
  expect((await studio.request(latest!.id))?.input.brief).toContain('wider river');
  expect(latest?.input.messages).toEqual([]);
});
it('does not expose another account’s thread or allow branching across threads', async () => {
  const a = await fixture(),
    b = await fixture();
  await expect(studio.get(b.account, a.thread)).rejects.toThrow('No such');
  await expect(
    studio.submit(
      b.account,
      b.thread,
      'generate',
      { id: randomUUID(), settings, parent: randomUUID() },
      'v1',
    ),
  ).rejects.toThrow('Select a delivered');
});
it('fences expired worker leases and keeps uncertain credits reserved', async () => {
  const { account, thread } = await fixture();
  const id = randomUUID();
  await studio.submit(account, thread, 'generate', { id, settings }, 'v1');
  const old = (await studio.request(id))!;
  await sql`UPDATE studio_requests SET lease=${randomUUID()},status='dispatched',lease_until=now()-interval '1 minute' WHERE id=${id}`.execute(
    database.db,
  );
  await studio.recoverUncertain();
  expect((await studio.request(id))?.status).toBe('uncertain');
  expect((await studio.credits.balance(account)).reserved).toBe(1);
  await expect(studio.finish(old, undefined, 'late failure')).rejects.toThrow('lease expired');
  const current = (await studio.request(id))!;
  await studio.finish(current, undefined, 'Reconciled');
  expect((await studio.credits.balance(account)).reserved).toBe(0);
});

it('bounds history, omits conversation snapshots and retrieves older pages privately', async () => {
  const { account, thread } = await fixture();
  for (let i = 0; i < 205; i++)
    await sql`INSERT INTO studio_messages(id,thread_id,role,text,created_at) VALUES(${randomUUID()},${thread},'user',${String(i)},now()+${i}*interval '1 second')`.execute(
      database.db,
    );
  const page = await studio.get(account, thread);
  expect(page.messages).toHaveLength(200);
  expect(page.history?.messagesBefore).toBeDefined();
  const older = await studio.get(account, thread, page.history);
  expect(older.messages).toHaveLength(6);
  expect(new Set([...older.messages, ...page.messages].map((m) => m.id)).size).toBe(206);
});
