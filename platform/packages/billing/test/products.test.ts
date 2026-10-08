import { randomUUID } from 'node:crypto';
import { beforeAll, afterAll, it, expect, vi } from 'vitest';
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
    created: Math.floor(Date.now() / 1000),
    livemode: false,
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
it('isolates Studio purchases, retry settlement and reversals from the other products', async () => {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  const id = randomUUID(),
    pack = { id: 'studio', priceId: 'price_studio', credits: 20, amount: 1000, currency: 'usd' };
  await database.db
    .insertInto('ai_studio_purchases')
    .values({ id, account_id: account, pack })
    .execute();
  const checkout = new Checkout(
    database.db,
    'sk_test_fake',
    'whsec_fake',
    'https://test.invalid',
    [pack],
    'aiStudio',
  );
  const session = {
    id: 'cs_' + id,
    mode: 'payment',
    created: Math.floor(Date.now() / 1000),
    livemode: false,
    payment_status: 'paid',
    client_reference_id: id,
    payment_intent: 'pi_' + id,
    amount_total: 1000,
    currency: 'usd',
    metadata: { purchaseId: id, creditProduct: 'aiStudio' },
  } as unknown as Stripe.Checkout.Session;
  await Promise.all([checkout.fulfill(session), checkout.fulfill(session)]);
  const credits = new Credits(database.db, 'aiStudio'),
    call = randomUUID(),
    rate = { version: 'v1', model: 'test', input: 1000000, cachedInput: 100000, output: 1000000 };
  await credits.reserve(account, call, 10, rate);
  await credits.dispatch(call);
  await Promise.all([
    credits.settle(account, call, { input: 2, cachedInput: 0, output: 1 }),
    credits.settle(account, call, { input: 2, cachedInput: 0, output: 1 }),
  ]);
  expect(await credits.balance(account)).toEqual({ balance: 17, reserved: 0, available: 17 });
  await expect(credits.reserve(account, randomUUID(), 18, rate)).rejects.toThrow('more credits');
  await checkout.reverse('pi_' + id, 500, false, 'refund-studio');
  await checkout.reverse('pi_' + id, 500, false, 'refund-studio');
  expect((await credits.balance(account)).balance).toBe(7);
  expect((await new Credits(database.db, 'music').balance(account)).balance).toBe(0);
  expect((await new Credits(database.db, 'maps').balance(account)).balance).toBe(0);
  expect((await new Credits(database.db).balance(account)).balance).toBe(0);
});

it('reuses a Studio checkout after an ambiguous response without changing its pack or product', async () => {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: 'Checkout retry' })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  const pack = {
    id: 'studio-retry',
    priceId: 'price_original',
    credits: 20,
    amount: 1000,
    currency: 'usd',
  };
  const checkout = new Checkout(
    database.db,
    'sk_test_fake',
    'whsec_fake',
    'https://test.invalid',
    [pack],
    'aiStudio',
  );
  const id = randomUUID();
  const session = {
    id: 'cs_retry',
    url: 'https://checkout.stripe.test/retry',
    status: 'open',
  } as Stripe.Checkout.Session;
  const create = vi
    .spyOn(checkout.stripe.checkout.sessions, 'create')
    .mockRejectedValueOnce(new Error('response lost'))
    .mockResolvedValue(session as never);
  const retrieve = vi
    .spyOn(checkout.stripe.checkout.sessions, 'retrieve')
    .mockResolvedValue(session as never);
  await expect(checkout.begin(account, pack.id, id)).rejects.toThrow('response lost');
  const original = structuredClone(create.mock.calls[0]!);
  pack.priceId = 'price_reconfigured';
  // Operator removes the pack between dispatch and the caller's retry.
  (checkout.packs as (typeof pack)[]).length = 0;
  expect(await checkout.begin(account, pack.id, id)).toEqual({ url: session.url });
  expect(create.mock.calls[1]).toEqual(original);
  expect(original[1]).toEqual({ idempotencyKey: 'aiStudio:' + id });
  expect(await checkout.begin(account, pack.id, id)).toEqual({ url: session.url });
  expect(create).toHaveBeenCalledTimes(2);
  expect(retrieve).toHaveBeenCalledWith(session.id);
  expect(
    await database.db
      .selectFrom('ai_studio_purchases')
      .selectAll()
      .where('account_id', '=', account)
      .execute(),
  ).toHaveLength(1);
  await expect(checkout.begin(randomUUID(), pack.id, id)).rejects.toThrow('already used');
  await database.db
    .updateTable('ai_studio_purchases')
    .set({ paid: true })
    .where('id', '=', id)
    .execute();
  await expect(checkout.begin(account, pack.id, id)).rejects.toThrow('already complete');
  create.mockRestore();
  retrieve.mockRestore();
});
it('does not redispatch unknown checkout outcomes beyond the provider idempotency window', async () => {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: 'Old checkout' })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  const pack = { id: 'old', priceId: 'price_old', credits: 20, amount: 1000, currency: 'usd' },
    id = randomUUID();
  await database.db
    .insertInto('ai_studio_purchases')
    .values({ id, account_id: account, pack, created_at: new Date(Date.now() - 86400000) })
    .execute();
  const checkout = new Checkout(
    database.db,
    'sk_test_fake',
    'whsec_fake',
    'https://test.invalid',
    [pack],
    'aiStudio',
  );
  const create = vi.spyOn(checkout.stripe.checkout.sessions, 'create');
  await expect(checkout.begin(account, pack.id, id)).rejects.toThrow('reconciliation');
  expect(create).not.toHaveBeenCalled();
  create.mockRestore();
});

it('reconciles an overrun once at the spending cap while auditing the actual usage', async () => {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: 'Capped reconciliation' })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  const credits = new Credits(database.db, 'aiStudio'),
    id = randomUUID();
  const rate = { version: 'test', model: 'test', input: 1000000, cachedInput: 0, output: 1000000 };
  await credits.adjust(account, randomUUID(), 20, 'grant');
  await credits.reserve(account, id, 5, rate);
  await credits.dispatch(id);
  const usage = { input: 6, cachedInput: 0, output: 2 };
  await expect(credits.settle(account, id, usage)).rejects.toThrow('exceeded');
  await credits.uncertain(id);
  await Promise.all([
    credits.reconcile(account, id, usage, 'Verified provider response'),
    credits.reconcile(account, id, usage, 'Verified provider response'),
  ]);
  expect(await credits.balance(account)).toEqual({ balance: 15, reserved: 0, available: 15 });
  const call = await database.db
    .selectFrom('ai_studio_calls')
    .selectAll()
    .where('id', '=', id)
    .executeTakeFirstOrThrow();
  expect(call.usage).toEqual(usage);
  const audit = await database.db
    .selectFrom('ai_studio_ledger')
    .select('details')
    .where('id', '=', `usage:${id}`)
    .executeTakeFirstOrThrow();
  expect(audit.details).toMatchObject({
    usage,
    reconciliation: {
      measuredCharge: 8,
      absorbedCredits: 3,
      evidence: 'Verified provider response',
    },
  });
  await expect(
    credits.reconcile(account, id, { ...usage, input: 7 }, 'Changed usage'),
  ).rejects.toThrow('changed');
  expect((await new Credits(database.db, 'maps').balance(account)).balance).toBe(0);
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
    created: Math.floor(Date.now() / 1000),
    livemode: false,
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
it('settles terrain purchases and reversals independently of music and map credits', async () => {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: 'Terrain buyer' })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  const id = randomUUID(),
    pack = { id: 'terrain', priceId: 'price_terrain', credits: 10, amount: 1000, currency: 'usd' };
  await database.db
    .insertInto('terrain_purchases')
    .values({ id, account_id: account, pack })
    .execute();
  const checkout = new Checkout(
    database.db,
    'sk_test_fake',
    'whsec_fake',
    'https://test.invalid',
    [pack],
    'terrain',
  );
  const session = {
    id: 'cs_' + id,
    mode: 'payment',
    created: Math.floor(Date.now() / 1000),
    livemode: false,
    payment_status: 'paid',
    client_reference_id: id,
    payment_intent: 'pi_' + id,
    amount_total: 1000,
    currency: 'usd',
    metadata: { purchaseId: id, creditProduct: 'terrain' },
  } as unknown as Stripe.Checkout.Session;
  await Promise.all([checkout.fulfill(session), checkout.fulfill(session)]);
  const credits = new Credits(database.db, 'terrain');
  expect((await credits.balance(account)).balance).toBe(10);
  await checkout.reverse('pi_' + id, 500, false, 'refund-terrain');
  await checkout.reverse('pi_' + id, 500, false, 'refund-terrain');
  expect((await credits.balance(account)).balance).toBe(5);
  expect((await new Credits(database.db, 'music').balance(account)).balance).toBe(0);
  expect((await new Credits(database.db, 'maps').balance(account)).balance).toBe(0);
});

it('attributes verified payments to the successful charge time rather than checkout creation', async () => {
  const account = await database.db
    .insertInto('accounts')
    .values({ kind: 'registered', display_name: 'Payment midnight' })
    .returning('id')
    .executeTakeFirstOrThrow();
  const id = randomUUID(),
    payment = 'pi_' + id,
    pack = { id: 'time', priceId: 'price_time', credits: 10, amount: 1000, currency: 'usd' };
  await database.db
    .insertInto('map_purchases')
    .values({ id, account_id: account.id, pack })
    .execute();
  const checkout = new Checkout(
    database.db,
    'sk_test_fake',
    'whsec_fake',
    'https://test.invalid',
    [pack],
    'maps',
  );
  const checkoutAt = Date.parse('2026-10-01T23:55:00Z') / 1000,
    chargeAt = Date.parse('2026-10-02T00:05:00Z') / 1000;
  const session = {
    id: 'cs_' + id,
    mode: 'payment',
    created: checkoutAt,
    livemode: false,
    payment_status: 'paid',
    client_reference_id: id,
    payment_intent: payment,
    amount_total: 1000,
    currency: 'usd',
    metadata: { purchaseId: id, creditProduct: 'maps' },
  } as unknown as Stripe.Checkout.Session;
  const mocks = [
    vi.spyOn(checkout.stripe.webhooks, 'constructEvent').mockReturnValue({
      type: 'checkout.session.completed',
      id: 'evt_midnight',
      created: chargeAt,
      data: { object: { id: session.id } },
    } as never),
    vi.spyOn(checkout.stripe.checkout.sessions, 'retrieve').mockResolvedValue(session as never),
    vi
      .spyOn(checkout.stripe.paymentIntents, 'retrieve')
      .mockResolvedValue({ metadata: { purchaseId: id, creditProduct: 'maps' } } as never),
    vi.spyOn(checkout.stripe.charges, 'list').mockResolvedValue({
      has_more: false,
      data: [{ paid: true, created: chargeAt, amount_refunded: 0 }],
    } as never),
    vi
      .spyOn(checkout.stripe.disputes, 'list')
      .mockResolvedValue({ has_more: false, data: [] } as never),
  ];
  try {
    await checkout.webhook(Buffer.from('{}'), 'mock verified event');
    const event = await database.db
      .selectFrom('admin_financial_events')
      .select('occurred_at')
      .where('purchase_id', '=', id)
      .where('kind', '=', 'payment')
      .executeTakeFirstOrThrow();
    expect(event.occurred_at.toISOString()).toBe('2026-10-02T00:05:00.000Z');
  } finally {
    for (const mock of mocks) mock.mockRestore();
  }
});
