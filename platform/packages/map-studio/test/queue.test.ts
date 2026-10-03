import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import { beforeAll, afterAll, expect, it } from 'vitest';
import { createTestDatabase, type TestDatabase } from '../../db/test/support.ts';
import { Studio } from '../src/studio.ts';
let database: TestDatabase, studio: Studio;
beforeAll(async () => {
  database = await createTestDatabase();
  studio = new Studio(database.db);
});
afterAll(async () => {
  await database?.drop();
});
it('rotates recently polled imports behind waiting work from another account', async () => {
  const ids: string[] = [];
  for (let index = 0; index < 4; index++) {
    const account = (
      await database.db
        .insertInto('accounts')
        .values({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
        .returning('id')
        .executeTakeFirstOrThrow()
    ).id;
    await studio.credits.adjust(account, randomUUID(), 3, 'grant');
    const thread = (await studio.create(account, 'Pending map')).id;
    await database.db
      .insertInto('studio_messages')
      .values({ id: randomUUID(), thread_id: thread, role: 'user', text: 'Ponds' })
      .execute();
    const id = randomUUID();
    await studio.submit(
      account,
      thread,
      'generate',
      { id, settings: { width: 256, height: 256, players: 4 } },
      'v1',
    );
    ids.push(id);
    if (index < 3)
      await sql`UPDATE studio_requests SET status='importing',created_at=now()-interval '1 hour',lease_until=now()-interval '1 minute' WHERE id=${id}`.execute(
        database.db,
      );
    else
      await sql`UPDATE studio_requests SET created_at=now()-interval '5 minutes' WHERE id=${id}`.execute(
        database.db,
      );
  }
  expect((await studio.claim())?.id).toBe(ids[3]);
  for (const id of ids.slice(0, 3)) expect((await studio.request(id))?.status).toBe('importing');
});
