import Stripe from 'stripe';
import { beforeAll, afterAll, expect, it, vi } from 'vitest';
import { createHarness, type Harness, type Instance } from './support.ts';

let harness: Harness, instance: Instance;
const secret = 'whsec_disabled_fixture';
beforeAll(async () => {
  for (const prefix of ['HIVE', 'AI_STUDIO', 'GENERATOR_STUDIO']) {
    vi.stubEnv(prefix + '_STRIPE_SECRET_KEY', 'sk_test_disabled_fixture');
    vi.stubEnv(prefix + '_STRIPE_WEBHOOK_SECRET', secret);
  }
  harness = await createHarness();
  instance = await harness.start({ origin: 'https://disabled.example.org' });
});
afterAll(async () => {
  await harness?.close();
  vi.unstubAllEnvs();
});

it('advertises the free service and rejects direct disabled authoring requests', async () => {
  expect((await instance.app.inject('/api/v1/instance')).json().features).toEqual(['queue.multi']);
  for (const path of [
    'map-studio/threads',
    'music-studio/threads',
    'terrain-studio/threads',
    'ai-building-studio/threads',
    'ai-studio/projects',
    'generator-studio/projects',
    'skins/checkout',
    'skins/publish',
  ]) {
    const response = await instance.app.inject({
      method: 'POST',
      url: '/api/v1/' + path,
      payload: {},
    });
    expect(response.statusCode, path).toBe(503);
  }
  const products = (await instance.app.inject('/api/v1/skins/products')).json();
  expect(products.items.every((product: { available: boolean }) => !product.available)).toBe(true);
});

it('still verifies payment callbacks after the Commander and coding studios are disabled', async () => {
  const stripe = new Stripe('sk_test_disabled_fixture');
  const payload = JSON.stringify({
    id: 'evt_disabled',
    type: 'customer.created',
    livemode: false,
    data: { object: { id: 'cus_disabled' } },
  });
  const signature = stripe.webhooks.generateTestHeaderString({ payload, secret });
  for (const tool of ['hive', 'ai-studio', 'generator-studio']) {
    const response = await instance.app.inject({
      method: 'POST',
      url: '/api/v1/' + tool + '/stripe',
      headers: { 'content-type': 'application/json', 'stripe-signature': signature },
      payload,
    });
    expect(response.statusCode, tool + ': ' + response.body).toBe(200);
  }
});
