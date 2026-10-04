import { randomUUID } from 'node:crypto';
import { beforeAll, afterAll, it, expect, vi } from 'vitest';
import type Stripe from 'stripe';
import { createTestDatabase, type TestDatabase } from '../../db/test/support.ts';
import { Checkout } from '../src/checkout.ts';
import { Credits } from '../src/credits.ts';
let database: TestDatabase;
beforeAll(async () => {
  database = await createTestDatabase();
});
afterAll(async () => {
  await database?.drop();
});
async function purchase() {
  const account = (
    await database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  const pack = {
    id: 'small',
    priceId: 'price_test',
    credits: 10000,
    amount: 1000,
    currency: 'usd',
  };
  const checkout = new Checkout(
    database.db,
    'sk_test_not_real',
    'whsec_test',
    'https://test.glob2.invalid',
    [pack],
  );
  const id = randomUUID();
  await database.db
    .insertInto('hive_purchases')
    .values({ id, account_id: account, pack })
    .execute();
  const session = {
    id: 'cs_' + id,
    mode: 'payment',
    payment_status: 'paid',
    client_reference_id: id,
    payment_intent: 'pi_' + id,
    amount_total: 1000,
    currency: 'usd',
    metadata: { purchaseId: id },
  } as unknown as Stripe.Checkout.Session;
  return { account, checkout, session };
}
it('fulfills a paid purchase once under duplicate concurrent delivery', async () => {
  const { account, checkout, session } = await purchase();
  await Promise.all(Array.from({ length: 5 }, () => checkout.fulfill(session)));
  expect((await new Credits(database.db).balance(account)).balance).toBe(10000);
  expect(
    await database.db
      .selectFrom('entitlements')
      .select('id')
      .where('account_id', '=', account)
      .execute(),
  ).toHaveLength(1);
});
it('does not credit delayed unpaid checkouts or mismatched amounts', async () => {
  const { account, checkout, session } = await purchase();
  await checkout.fulfill({ ...session, payment_status: 'unpaid' });
  expect((await new Credits(database.db).balance(account)).balance).toBe(0);
  await expect(checkout.fulfill({ ...session, amount_total: 100 })).rejects.toThrow('match');
  await checkout.fulfill(session);
  expect((await new Credits(database.db).balance(account)).balance).toBe(10000);
});
it('handles partial refunds, disputes, duplicate reversal and dispute wins', async () => {
  const { account, checkout, session } = await purchase();
  await checkout.fulfill(session);
  const payment = session.payment_intent as string;
  await checkout.reverse(payment, 250, false, 'refund1');
  await checkout.reverse(payment, 250, false, 'refund1');
  expect((await new Credits(database.db).balance(account)).balance).toBe(7500);
  await checkout.reverse(payment, 250, true, 'dispute1');
  expect((await new Credits(database.db).balance(account)).balance).toBe(0);
  await checkout.reverse(payment, 250, false, 'won1');
  expect((await new Credits(database.db).balance(account)).balance).toBe(7500);
});
it('rejects forged webhooks before reading payment state', async () => {
  const { checkout } = await purchase();
  await expect(checkout.webhook(Buffer.from('{}'), 'invalid')).rejects.toThrow();
});

it('ignores skin payments rather than treating them as legacy Hive purchases', async () => {
  const { checkout } = await purchase();
  const payload = JSON.stringify({
    id: 'evt_skin_foreign',
    object: 'event',
    type: 'checkout.session.completed',
    livemode: false,
    data: { object: { id: 'cs_skin_foreign' } },
  });
  const signature = checkout.stripe.webhooks.generateTestHeaderString({
    payload,
    secret: 'whsec_test',
  });
  const session = vi.spyOn(checkout.stripe.checkout.sessions, 'retrieve').mockResolvedValue({
    id: 'cs_skin_foreign',
    payment_intent: 'pi_skin_foreign',
  } as unknown as Stripe.Response<Stripe.Checkout.Session>);
  const intent = vi.spyOn(checkout.stripe.paymentIntents, 'retrieve').mockResolvedValue({
    id: 'pi_skin_foreign',
    metadata: { purchaseId: randomUUID(), accountId: randomUUID() },
  } as unknown as Stripe.Response<Stripe.PaymentIntent>);
  try {
    await expect(checkout.webhook(Buffer.from(payload), signature)).resolves.toBeUndefined();
    expect(intent).toHaveBeenCalledOnce();
  } finally {
    session.mockRestore();
    intent.mockRestore();
  }
});
