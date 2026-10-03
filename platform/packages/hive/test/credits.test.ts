import { randomUUID } from 'node:crypto';
import { beforeAll, afterAll, describe, it, expect } from 'vitest';
import { createTestDatabase, type TestDatabase } from '../../db/test/support.ts';
import { Credits, price, type RateCard } from '../src/credits.ts';
let database: TestDatabase;
let credits: Credits;
const rate: RateCard = {
  version: 'test/1',
  model: 'test',
  input: 1000000,
  cachedInput: 100000,
  output: 2000000,
};
beforeAll(async () => {
  database = await createTestDatabase();
  credits = new Credits(database.db);
});
afterAll(async () => {
  await database?.drop();
});
async function account() {
  return (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
}
describe('credit ledger', () => {
  it('accounts for cached input and reasoning-inclusive output once', () => {
    expect(price(rate, { input: 100, cachedInput: 50, output: 10 })).toBe(75);
    expect(
      price(
        { ...rate, cacheWrite: 1250000 },
        { input: 100, cachedInput: 50, cacheWrite: 20, output: 10 },
      ),
    ).toBe(80);
    expect(() => price(rate, { input: 1, cachedInput: 2, output: 0 })).toThrow();
    expect(() =>
      price(rate, { input: Number.MAX_SAFE_INTEGER, cachedInput: 0, output: 1 }),
    ).toThrow();
  });
  it('serializes simultaneous reservations against one balance', async () => {
    const a = await account();
    await credits.adjust(a, 'grant:' + a, 100, 'grant');
    const results = await Promise.allSettled([
      credits.reserve(a, randomUUID(), 80, rate),
      credits.reserve(a, randomUUID(), 80, rate),
    ]);
    expect(results.filter((r) => r.status === 'fulfilled')).toHaveLength(1);
    expect(await credits.balance(a)).toEqual({ balance: 100, reserved: 80, available: 20 });
  });
  it('settles exactly once and releases unused reservation', async () => {
    const a = await account(),
      call = randomUUID();
    await credits.adjust(a, 'grant:' + a, 100, 'grant');
    expect(await credits.reserve(a, call, 100, rate)).toBe(true);
    expect(await credits.reserve(a, call, 100, rate)).toBe(false);
    expect(await credits.dispatch(call)).toBe(true);
    expect(await credits.dispatch(call)).toBe(false);
    const usage = { input: 20, cachedInput: 0, output: 10 };
    await Promise.all([credits.settle(a, call, usage), credits.settle(a, call, usage)]);
    expect(await credits.balance(a)).toEqual({ balance: 60, reserved: 0, available: 60 });
    await expect(credits.settle(a, call, { ...usage, output: 11 })).rejects.toThrow();
  });
  it('retains uncertain reservations and refuses overspend', async () => {
    const a = await account(),
      call = randomUUID();
    await credits.adjust(a, 'grant:' + a, 100, 'grant');
    await credits.reserve(a, call, 100, rate);
    await credits.dispatch(call);
    await credits.uncertain(call);
    await expect(credits.reserve(a, randomUUID(), 1, rate)).rejects.toThrow('more credits');
    await credits.settle(a, call, { input: 0, cachedInput: 0, output: 0 });
    expect((await credits.balance(a)).available).toBe(100);
  });
  it('deduplicates fulfillment and permits reversal after credits were spent', async () => {
    const a = await account();
    await credits.adjust(a, 'purchase:' + a, 100, 'purchase');
    expect(await credits.adjust(a, 'purchase:' + a, 100, 'purchase')).toBe(false);
    const id = randomUUID();
    await credits.reserve(a, id, 100, rate);
    await credits.dispatch(id);
    await credits.settle(a, id, { input: 100, cachedInput: 0, output: 0 });
    await credits.adjust(a, 'refund:' + a, -100, 'refund');
    expect((await credits.balance(a)).available).toBe(0);
    await expect(credits.reserve(a, randomUUID(), 1, rate)).rejects.toThrow();
  });
});
