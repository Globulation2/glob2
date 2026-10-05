import { randomUUID } from 'node:crypto';
import { beforeAll, afterAll, it, expect } from 'vitest';
import type Stripe from 'stripe';
import { createTestDatabase, type TestDatabase } from '../../db/test/support.ts';
import { Checkout, Credits } from '../src/index.ts';
let database: TestDatabase;
beforeAll(async () => {
  database = await createTestDatabase();
});
afterAll(async () => {
  await database?.drop();
});
it('fulfills and reverses map purchases without modifying Hive or its entitlements', async () => {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  const id = randomUUID(),
    pack = { id: 'maps', priceId: 'price_maps', credits: 10, amount: 1000, currency: 'usd' };
  await database.db.insertInto('map_purchases').values({ id, account_id: account, pack }).execute();
  const checkout = new Checkout(
    database.db,
    'sk_test_fake',
    'whsec_fake',
    'https://test.invalid',
    [pack],
    'maps',
  );
  const session = {
    id: 'cs_' + id,
    mode: 'payment',
    payment_status: 'paid',
    client_reference_id: id,
    payment_intent: 'pi_' + id,
    amount_total: 1000,
    currency: 'usd',
    metadata: { purchaseId: id, creditProduct: 'maps' },
  } as unknown as Stripe.Checkout.Session;
  await Promise.all([checkout.fulfill(session), checkout.fulfill(session)]);
  expect((await new Credits(database.db, 'maps').balance(account)).balance).toBe(10);
  expect((await new Credits(database.db).balance(account)).balance).toBe(0);
  expect(
    await database.db
      .selectFrom('entitlements')
      .selectAll()
      .where('account_id', '=', account)
      .execute(),
  ).toHaveLength(0);
  await checkout.reverse('pi_' + id, 250, false, 'refund-map');
  await checkout.reverse('pi_' + id, 250, false, 'refund-map');
  expect((await new Credits(database.db, 'maps').balance(account)).balance).toBe(7);
  await checkout.reverse('pi_' + id, 250, true, 'dispute-map');
  expect((await new Credits(database.db, 'maps').balance(account)).balance).toBe(0);
  await checkout.reverse('pi_' + id, 250, false, 'won-map');
  expect((await new Credits(database.db, 'maps').balance(account)).balance).toBe(7);
});

it('fulfills and reverses music purchases without modifying Hive or its entitlements', async () => {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  const id = randomUUID(),
    pack = { id: 'music', priceId: 'price_music', credits: 10, amount: 1000, currency: 'usd' };
  await database.db
    .insertInto('music_purchases')
    .values({ id, account_id: account, pack })
    .execute();
  const checkout = new Checkout(
    database.db,
    'sk_test_fake',
    'whsec_fake',
    'https://test.invalid',
    [pack],
    'music',
  );
  const session = {
    id: 'cs_' + id,
    mode: 'payment',
    payment_status: 'paid',
    client_reference_id: id,
    payment_intent: 'pi_' + id,
    amount_total: 1000,
    currency: 'usd',
    metadata: { purchaseId: id, creditProduct: 'music' },
  } as unknown as Stripe.Checkout.Session;
  await Promise.all([checkout.fulfill(session), checkout.fulfill(session)]);
  expect((await new Credits(database.db, 'music').balance(account)).balance).toBe(10);
  expect((await new Credits(database.db).balance(account)).balance).toBe(0);
  expect(
    await database.db
      .selectFrom('entitlements')
      .selectAll()
      .where('account_id', '=', account)
      .execute(),
  ).toHaveLength(0);
  await checkout.reverse('pi_' + id, 250, false, 'refund-music');
  await checkout.reverse('pi_' + id, 250, false, 'refund-music');
  expect((await new Credits(database.db, 'music').balance(account)).balance).toBe(7);
  await checkout.reverse('pi_' + id, 250, true, 'dispute-music');
  expect((await new Credits(database.db, 'music').balance(account)).balance).toBe(0);
  await checkout.reverse('pi_' + id, 250, false, 'won-music');
  expect((await new Credits(database.db, 'music').balance(account)).balance).toBe(7);
});
