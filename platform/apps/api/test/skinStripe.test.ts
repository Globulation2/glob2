import { createServer, type Server } from 'node:http';
import type { AddressInfo } from 'node:net';
import Stripe from 'stripe';
import { afterAll, beforeAll, expect, it } from 'vitest';
import { StripePayments } from '../src/skins/billing/stripe.ts';
let server: Server, payments: StripePayments;
let created = 0,
  checkoutBody = new URLSearchParams(),
  idempotency = '';
const price = {
  id: 'price_skin',
  object: 'price',
  active: true,
  type: 'one_time',
  livemode: false,
  unit_amount: 500,
};
const charge = { id: 'ch_skin', object: 'charge', amount_refunded: 0, disputed: false };
const session = {
  id: 'cs_skin',
  object: 'checkout.session',
  mode: 'payment',
  livemode: false,
  status: 'open',
  payment_status: 'unpaid',
  amount_total: 500,
  currency: 'usd',
  created: 1720000000,
  url: 'https://checkout.stripe.com/c/pay/skin-fixture',
  client_reference_id: 'purchase-fixture',
  metadata: { purchaseId: 'purchase-fixture', accountId: 'account-fixture' },
  line_items: { object: 'list', has_more: false, data: [{ id: 'li_skin', quantity: 1, price }] },
  payment_intent: { id: 'pi_skin', object: 'payment_intent', latest_charge: charge },
};
let dispute = 'needs_response';
const recoveryPages = new Map<string, { object: string; data: unknown[]; has_more: boolean }>();
const recoveryQueries: URLSearchParams[] = [];
const expiresAt = Math.floor(Date.now() / 1000) + 23 * 3600;
beforeAll(async () => {
  server = createServer(async (request, response) => {
    const url = new URL(request.url ?? '/', 'http://localhost');
    const pathname = url.pathname;
    let value: unknown;
    if (pathname === '/v1/prices/price_skin') value = price;
    else if (pathname === '/v1/checkout/sessions' && request.method === 'POST') {
      let body = '';
      for await (const chunk of request) body += String(chunk);
      checkoutBody = new URLSearchParams(body);
      idempotency = String(request.headers['idempotency-key']);
      created++;
      value = session;
    } else if (pathname === '/v1/checkout/sessions') {
      recoveryQueries.push(url.searchParams);
      value = recoveryPages.get(url.searchParams.get('starting_after') ?? '') ?? {
        object: 'list',
        data: [],
        has_more: false,
      };
    } else if (pathname === '/v1/checkout/sessions/cs_skin') value = session;
    else if (pathname === '/v1/refunds')
      value = {
        object: 'list',
        has_more: false,
        data: [
          {
            id: 're_skin',
            amount: charge.amount_refunded,
            status: 'succeeded',
            created: 1720000000,
          },
        ],
      };
    else if (pathname === '/v1/disputes')
      value = {
        object: 'list',
        has_more: false,
        data: [
          { id: 'dp_skin', status: dispute, amount: 250, currency: 'usd', created: 1720000000 },
        ],
      };
    else {
      response.statusCode = 404;
      value = { error: { message: 'Unexpected mock Stripe endpoint' } };
    }
    response.setHeader('content-type', 'application/json');
    response.end(JSON.stringify(value));
  });
  await new Promise<void>((resolve) => server.listen(0, '127.0.0.1', resolve));
  payments = new StripePayments(
    new Stripe('sk_test_fixture', {
      host: '127.0.0.1',
      port: (server.address() as AddressInfo).port,
      protocol: 'http',
      maxNetworkRetries: 0,
    }),
    false,
  );
});
afterAll(async () => {
  await new Promise<void>((resolve, reject) =>
    server.close((error) => (error ? reject(error) : resolve())),
  );
});
it('uses hosted checkout metadata and resumes its recorded session without another creation', async () => {
  expect(
    await payments.checkout(
      'purchase-fixture',
      'account-fixture',
      'price_skin',
      'https://play.test',
      expiresAt,
    ),
  ).toEqual({ id: session.id, url: session.url });
  expect(checkoutBody.get('line_items[0][price]')).toBe('price_skin');
  expect(checkoutBody.get('line_items[0][quantity]')).toBe('1');
  expect(checkoutBody.get('payment_intent_data[metadata][accountId]')).toBe('account-fixture');
  expect(checkoutBody.get('success_url')).toBe('https://play.test/skins?purchase=purchase-fixture');
  expect(idempotency).toBe('colony-skin:purchase-fixture');
  expect(checkoutBody.get('expires_at')).toBe(String(expiresAt));
  await payments.checkout(
    'purchase-fixture',
    'account-fixture',
    'price_skin',
    'https://play.test',
    expiresAt,
    session.id,
  );
  expect(created).toBe(1);
});
it('reads current payment, refund, and dispute state through the real Stripe SDK', async () => {
  expect((await payments.inspect(session.id)).state).toBe('pending');
  session.payment_status = 'paid';
  expect((await payments.inspect(session.id)).state).toBe('paid');
  charge.amount_refunded = 100;
  expect((await payments.inspect(session.id)).state).toBe('refunded');
  charge.amount_refunded = 0;
  charge.disputed = true;
  expect((await payments.inspect(session.id)).state).toBe('disputed');
  dispute = 'won';
  expect((await payments.inspect(session.id)).state).toBe('paid');
  session.livemode = true;
  await expect(payments.inspect(session.id)).rejects.toThrow('Unexpected');
  session.livemode = false;
  price.type = 'recurring';
  await expect(
    payments.checkout(
      'purchase-fixture',
      'account-fixture',
      'price_skin',
      'https://play.test',
      expiresAt,
    ),
  ).rejects.toThrow('one-time');
});

it('recovers only matching checkout metadata through bounded stable pagination', async () => {
  recoveryPages.set('', {
    object: 'list',
    data: [{ id: 'cs_other', metadata: { purchaseId: 'another' } }],
    has_more: true,
  });
  recoveryPages.set('cs_other', { object: 'list', data: [session], has_more: false });
  const createdAt = new Date('2026-01-01T00:00:00Z');
  expect(await payments.recover('purchase-fixture', 'account-fixture', createdAt)).toEqual({
    sessionId: null,
    nextCursor: 'cs_other',
  });
  expect(
    await payments.recover('purchase-fixture', 'account-fixture', createdAt, 'cs_other'),
  ).toEqual({ sessionId: 'cs_skin', nextCursor: null });
  expect(recoveryQueries[0]?.get('limit')).toBe('100');
  expect(recoveryQueries[0]?.get('created[gte]')).toBe(String(createdAt.getTime() / 1000 - 300));
  expect(recoveryQueries[0]?.get('created[lte]')).toBe(String(createdAt.getTime() / 1000 + 86400));
  await expect(
    payments.recover('purchase-fixture', 'wrong-account', createdAt, 'cs_other'),
  ).rejects.toThrow('identity');
  recoveryPages.set('cs_other', { object: 'list', data: [], has_more: true });
  await expect(
    payments.recover('purchase-fixture', 'account-fixture', createdAt, 'cs_other'),
  ).rejects.toThrow('did not advance');
});

it('retains partial refund and dispute money independently from entitlement state', async () => {
  session.payment_status = 'paid';
  charge.amount_refunded = 100;
  charge.disputed = true;
  dispute = 'needs_response';
  const inspected = await payments.inspect(session.id);
  expect(inspected.state).toBe('refunded');
  expect(inspected.monetary).toMatchObject({
    paid: 500,
    refunded: 100,
    disputed: true,
    disputes: [{ id: 'dp_skin', amount: 250, active: true }],
  });
  dispute = 'won';
  expect((await payments.inspect(session.id)).monetary).toMatchObject({
    refunded: 100,
    disputed: false,
    disputes: [{ id: 'dp_skin', amount: 250, active: false }],
  });
});
