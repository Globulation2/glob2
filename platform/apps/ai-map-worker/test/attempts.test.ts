import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import { beforeAll, afterAll, it, expect, vi } from 'vitest';
import { createTestDatabase, type TestDatabase } from '../../../packages/db/test/support.ts';
import { Studio } from '@glob2/map-studio';
import { Attempts, ProviderUncertain } from '../src/provider.ts';
let database: TestDatabase, studio: Studio;
beforeAll(async () => {
  database = await createTestDatabase();
  studio = new Studio(database.db);
});
afterAll(async () => {
  await database?.drop();
});
async function request() {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  await studio.credits.adjust(account, randomUUID(), 3, 'grant');
  const thread = (await studio.create(account, 'River')).id,
    id = randomUUID();
  await studio.submit(account, thread, 'chat', { id, text: 'Widen the river' }, 'v1');
  await sql`UPDATE studio_requests SET lease=${randomUUID()},lease_until=now()+interval '15 minutes',status='preparing' WHERE id=${id}`.execute(
    database.db,
  );
  return (await studio.request(id))!;
}
it('replays completed provider results without making another paid call', async () => {
  const row = await request(),
    attempts = new Attempts(studio, 100);
  const call = vi.fn(async () => ({ text: 'Wider river', usage: { input: 5 } }));
  expect(await attempts.run(row, 'chat', 'model', {}, call)).toEqual({
    text: 'Wider river',
    usage: { input: 5 },
  });
  await attempts.run(row, 'chat', 'model', {}, call);
  expect(call).toHaveBeenCalledTimes(1);
});
it('retains uncertain outcomes and never blindly dispatches them again', async () => {
  const row = await request(),
    attempts = new Attempts(studio, 100),
    call = vi.fn(async () => {
      throw new ProviderUncertain('timeout');
    });
  await expect(attempts.run(row, 'chat', 'model', {}, call)).rejects.toThrow('timeout');
  await expect(attempts.run(row, 'chat', 'model', {}, call)).rejects.toThrow('reconciliation');
  expect(call).toHaveBeenCalledTimes(1);
  expect((await studio.request(row.id))?.status).toBe('uncertain');
});
it('enforces a shared daily budget before provider dispatch', async () => {
  const row = await request(),
    attempts = new Attempts(studio, 1),
    call = vi.fn(async () => ({ text: 'unused' }));
  await expect(attempts.run(row, 'image', 'model', {}, call)).rejects.toThrow(
    'daily service limit',
  );
  expect(call).not.toHaveBeenCalled();
});

it('keeps a late provider result uncertain after atomic lease recovery', async () => {
  const row = await request(),
    attempts = new Attempts(studio, 100);
  await expect(
    attempts.run(row, 'late', 'model', {}, async () => {
      await sql`UPDATE studio_requests SET lease_until=now()-interval '1 minute' WHERE id=${row.id}`.execute(
        database.db,
      );
      await studio.recoverUncertain();
      return { text: 'late result' };
    }),
  ).rejects.toBeInstanceOf(ProviderUncertain);
  expect((await studio.request(row.id))?.status).toBe('uncertain');
  const journal = await database.db
    .selectFrom('studio_attempts')
    .select(['status', 'output'])
    .where('request_id', '=', row.id)
    .executeTakeFirstOrThrow();
  expect(journal).toMatchObject({ status: 'uncertain', output: null });
});
