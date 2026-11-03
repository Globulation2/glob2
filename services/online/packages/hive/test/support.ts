import { randomUUID } from 'node:crypto';
import type { TestDatabase } from '../../db/test/support.ts';
import { Sessions } from '../src/sessions.ts';
export async function fixture(db: TestDatabase['db']) {
  const sessions = new Sessions(db);
  const account = (
    await db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  await db
    .insertInto('entitlements')
    .values({ account_id: account, entitlement: 'hive-mind', source: 'test' })
    .execute();
  const match = (
    await db
      .insertInto('matches')
      .values({
        sim_version: `125-49-${'a'.repeat(64)}`,
        origin: 'queue',
        queue_id: 'ranked',
        rated: true,
        status: 'running',
        setup: {},
        seed: 1,
        map_hash: 'b'.repeat(64),
      })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  await db
    .insertInto('match_participants')
    .values({
      match_id: match,
      seat: 0,
      team: 1,
      kind: 'human',
      account_id: account,
      display_name: 'Commander',
    })
    .execute();
  const s = await sessions.open(account, match, 0),
    client = randomUUID();
  const poll = await sessions.poll(s.id, client, undefined, 100, true);
  return { s, client, lease: poll.lease, account, match, sessions };
}
