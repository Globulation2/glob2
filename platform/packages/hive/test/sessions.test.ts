import { fixture } from './support.ts';
import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import { beforeAll, afterAll, it, expect } from 'vitest';
import { createTestDatabase, type TestDatabase } from '../../db/test/support.ts';
import { Sessions } from '../src/sessions.ts';
import { Credits } from '../src/credits.ts';
let database: TestDatabase, sessions: Sessions;
beforeAll(async () => {
  database = await createTestDatabase();
  sessions = new Sessions(database.db);
});
afterAll(async () => {
  await database?.drop();
});
it('binds permission to participant, team and live account rather than supplied team', async () => {
  const { account, match, s } = await fixture(database.db);
  expect(s.team).toBe(1);
  await expect(sessions.open(account, match, 1)).rejects.toThrow();
  await database.db
    .updateTable('matches')
    .set({ status: 'ended' })
    .where('id', '=', match)
    .execute();
  await expect(sessions.open(account, match, 0)).rejects.toThrow();
});
it('leases fence simultaneous clients and uncertain dispatch is not repeated', async () => {
  const { s, client, lease } = await fixture(database.db);
  await expect(sessions.poll(s.id, randomUUID(), undefined, 100, true)).rejects.toThrow(
    'another window',
  );
  const op = await sessions.enqueue(s.id, 0, {
    kind: 'execute',
    source: 'function step(ctx){return {output:ctx.tick};}',
  });
  expect((await sessions.poll(s.id, client, lease, 101, false)).operations).toHaveLength(0);
  expect((await sessions.poll(s.id, client, lease, 101, true)).operations[0]?.id).toBe(op);
  expect((await sessions.poll(s.id, client, lease, 102, true)).operations).toHaveLength(0);
  await sql`UPDATE hive_sessions SET lease_until=now()-interval '1 second' WHERE id=${s.id}`.execute(
    database.db,
  );
  const second = await sessions.poll(s.id, randomUUID(), undefined, 102, true);
  expect(second.operations).toHaveLength(0);
  expect(
    (
      await database.db
        .selectFrom('hive_operations')
        .select('status')
        .where('id', '=', op)
        .executeTakeFirstOrThrow()
    ).status,
  ).toBe('uncertain');
  await expect(sessions.result(s.id, op, lease, 'completed', '{}', 102)).rejects.toThrow();
});
it('deduplicates commands and prevents an old run enqueueing after stop', async () => {
  const { s } = await fixture(database.db);
  const id = randomUUID();
  expect(await sessions.command(s.id, id, 'Defend the colony', true)).toBe(true);
  expect(await sessions.command(s.id, id, 'Defend the colony', true)).toBe(false);
  await expect(sessions.enqueue(s.id, 0, { kind: 'list' })).rejects.toThrow();
  await sessions.stop(s.id);
  expect((await sessions.get(s.id)).supervision).toBe(false);
});
it('wakes only for installed current programs, supervised tasks and funded accounts', async () => {
  const { s, lease, account } = await fixture(database.db);
  const id = randomUUID();
  const wake = {
    eventId: randomUUID(),
    programId: id,
    revision: 1,
    tick: 100,
    key: 'attack',
    reason: 'Colony under attack',
    data: '{}',
  };
  expect(await sessions.wake(s.id, wake, lease)).toBe(false);
  await database.db
    .insertInto('hive_programs')
    .values({ session_id: s.id, id, revision: 1, definition: {}, status: 'active' })
    .execute();
  await sessions.command(s.id, randomUUID(), 'Guard the colony', true);
  expect(await sessions.wake(s.id, { ...wake, eventId: randomUUID() }, lease)).toBe(false);
  await new Credits(database.db).adjust(account, 'grant:' + account, 100, 'grant');
  await sql`UPDATE hive_sessions SET tick=125 WHERE id=${s.id}`.execute(database.db);
  const funded = { ...wake, tick: 125, eventId: randomUUID() };
  expect(await sessions.wake(s.id, funded, lease)).toBe(true);
  expect(await sessions.wake(s.id, funded, lease)).toBe(false);
  expect(await sessions.wake(s.id, { ...funded, eventId: randomUUID() }, lease)).toBe(false);
});
it('result retries cannot overwrite the first outcome or revive uncertain operations', async () => {
  const { s, client, lease } = await fixture(database.db);
  const op = await sessions.enqueue(s.id, 0, {
    kind: 'execute',
    source: 'function step(){return {}}',
  });
  await sessions.poll(s.id, client, lease, 100, true);
  expect(await sessions.result(s.id, op, lease, 'completed', '{}', 100)).toBe(true);
  expect(await sessions.result(s.id, op, lease, 'completed', '{}', 100)).toBe(true);
  await expect(
    sessions.result(s.id, op, lease, 'completed', '{"changed":true}', 100),
  ).rejects.toThrow('changed');
  const uncertain = await sessions.enqueue(s.id, 0, {
    kind: 'execute',
    source: 'function step(){return {}}',
  });
  await sessions.poll(s.id, client, lease, 100, true);
  await sql`UPDATE hive_operations SET status='uncertain' WHERE id=${uncertain}`.execute(
    database.db,
  );
  expect(await sessions.result(s.id, uncertain, lease, 'completed', '{}', 100)).toBe(false);
});
