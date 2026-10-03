import Stripe from 'stripe';
import { beforeAll, afterAll, expect, it } from 'vitest';
import { createHarness, type Harness, type Instance } from './support.ts';
let harness: Harness, instance: Instance;
const secret = 'whsec_skin_test';
beforeAll(async () => {
  harness = await createHarness();
  instance = await harness.start({
    secrets: { STRIPE_SECRET_KEY: 'sk_test_local_fixture', STRIPE_WEBHOOK_SECRET: secret },
  });
});
afterAll(async () => {
  await harness?.close();
});
it('verifies exact raw bytes, refuses mode mismatch, and records an event once', async () => {
  const sdk = new Stripe('sk_test_local_fixture');
  const payload = JSON.stringify({
    id: 'evt_skin_fixture',
    object: 'event',
    type: 'customer.created',
    livemode: false,
    data: { object: { id: 'cus_fixture', object: 'customer' } },
  });
  const signature = sdk.webhooks.generateTestHeaderString({ payload, secret });
  const send = (body: string, sig = signature) =>
    instance.app.inject({
      method: 'POST',
      url: '/api/v1/skins/stripe-webhook',
      headers: { 'content-type': 'application/json', 'stripe-signature': sig },
      payload: body,
    });
  expect((await send(payload + ' ')).statusCode).toBe(400);
  expect((await send(payload, 'invalid')).statusCode).toBe(400);
  expect((await send(payload)).statusCode).toBe(200);
  expect((await send(payload)).statusCode).toBe(200);
  expect(
    await harness.database.db.selectFrom('skin_payment_events').selectAll().execute(),
  ).toHaveLength(1);
  const live = payload.replace('false', 'true');
  expect(
    (await send(live, sdk.webhooks.generateTestHeaderString({ payload: live, secret }))).statusCode,
  ).toBe(400);
  expect(await harness.database.db.selectFrom('entitlements').selectAll().execute()).toHaveLength(
    0,
  );
});
