import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import { beforeAll, afterAll, it, expect } from 'vitest';
import { createTestDatabase, type TestDatabase } from '../../db/test/support.ts';
import { Commander, type ModelProvider, type ModelStep } from '../src/commander.ts';
import { Credits } from '../src/credits.ts';
import { fixture } from './support.ts';
let database: TestDatabase;
const rate = { version: 'test/1', model: 'test-model', input: 10, cachedInput: 1, output: 20 };
const response: ModelStep = {
  text: 'The colony is secure.',
  calls: [],
  messages: [],
  usage: { input: 100, cachedInput: 20, output: 20 },
};
beforeAll(async () => {
  database = await createTestDatabase();
});
afterAll(async () => {
  await database?.drop();
});
it('meters each call, settles actual usage and persists reports', async () => {
  const { s, account, sessions } = await fixture(database.db);
  await new Credits(database.db).adjust(account, randomUUID(), 100, 'grant');
  await sessions.command(s.id, randomUUID(), 'Report colony status', false);
  let calls = 0;
  const provider: ModelProvider = {
    step: async () => {
      calls++;
      return response;
    },
  };
  await new Commander(database.db, provider, rate, 'test documentation').run(s.id);
  expect(calls).toBe(1);
  expect((await new Credits(database.db).balance(account)).reserved).toBe(0);
  expect(
    (await sessions.events(s.id)).some((e) => (e.body as { text?: string }).text === response.text),
  ).toBe(true);
});
it('does not call a provider with exhausted credits', async () => {
  const { s, sessions } = await fixture(database.db);
  await sessions.command(s.id, randomUUID(), 'Report colony status', false);
  let calls = 0;
  await new Commander(
    database.db,
    {
      step: async () => {
        calls++;
        return response;
      },
    },
    rate,
    'test',
  ).run(s.id);
  expect(calls).toBe(0);
});
it('a database run lease fences processes even when a new instruction arrives', async () => {
  const { s, account, sessions } = await fixture(database.db);
  await new Credits(database.db).adjust(account, randomUUID(), 100, 'grant');
  await sessions.command(s.id, randomUUID(), 'Report colony status', false);
  let release: (value: ModelStep) => void = () => {};
  let entered: () => void = () => {};
  const started = new Promise<void>((r) => {
    entered = r;
  });
  const held = new Promise<ModelStep>((r) => {
    release = r;
  });
  let calls = 0;
  const provider: ModelProvider = {
    step: async () => {
      calls++;
      entered();
      return held;
    },
  };
  const first = new Commander(database.db, provider, rate, 'test');
  const running = first.run(s.id);
  await started;
  await sessions.command(s.id, randomUUID(), 'Inspect our food instead', true);
  await new Commander(database.db, provider, rate, 'test').run(s.id);
  expect(calls).toBe(1);
  release(response);
  await running;
  expect((await sessions.get(s.id)).pending_run).toBe(true);
  await new Commander(database.db, { step: async () => response }, rate, 'test').run(s.id);
  expect((await sessions.get(s.id)).pending_run).toBe(false);
});
it('uncertain provider outcomes retain reservations and never retry automatically', async () => {
  const { s, account, sessions } = await fixture(database.db);
  await new Credits(database.db).adjust(account, randomUUID(), 100, 'grant');
  await sessions.command(s.id, randomUUID(), 'Report colony status', false);
  let calls = 0;
  const commander = new Commander(
    database.db,
    {
      step: async () => {
        calls++;
        throw new Error('connection lost');
      },
    },
    rate,
    'test',
  );
  await commander.run(s.id);
  await commander.run(s.id);
  expect(calls).toBe(1);
  expect((await new Credits(database.db).balance(account)).reserved).toBeGreaterThan(0);
  expect(
    (
      await sql<{
        status: string;
      }>`SELECT status FROM hive_calls WHERE account_id=${account}`.execute(database.db)
    ).rows[0]?.status,
  ).toBe('uncertain');
});
