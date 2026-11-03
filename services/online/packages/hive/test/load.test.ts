import { randomUUID } from 'node:crypto';
import { beforeAll, afterAll, it, expect } from 'vitest';
import { createTestDatabase, type TestDatabase } from '../../db/test/support.ts';
import { fixture } from './support.ts';
import { Credits } from '../src/credits.ts';
let database: TestDatabase;
beforeAll(async () => {
  database = await createTestDatabase();
});
afterAll(async () => {
  await database?.drop();
});
it('fences 50 competing clients and conserves credit under 100 reservations', async () => {
  const { s, account, sessions } = await fixture(database.db);
  const connections = await Promise.allSettled(
    Array.from({ length: 50 }, () => sessions.poll(s.id, randomUUID(), undefined, 100, true)),
  );
  expect(connections.every((r) => r.status === 'rejected')).toBe(true);
  const credits = new Credits(database.db);
  await credits.adjust(account, randomUUID(), 500, 'grant');
  const reservations = await Promise.allSettled(
    Array.from({ length: 100 }, () =>
      credits.reserve(account, randomUUID(), 10, {
        model: 'test',
        version: '1',
        input: 1,
        cachedInput: 0,
        output: 1,
      }),
    ),
  );
  expect(reservations.filter((r) => r.status === 'fulfilled')).toHaveLength(50);
  expect(await credits.balance(account)).toEqual({ balance: 500, reserved: 500, available: 0 });
});
