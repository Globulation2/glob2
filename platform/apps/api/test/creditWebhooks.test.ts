import { afterAll, beforeAll, expect, it, vi } from 'vitest';
import { createHarness, type Harness, type Instance } from './support.ts';
let harness: Harness, instance: Instance;
const credentials = {
  HIVE_OPENAI_API_KEY: 'test-provider-key',
  HIVE_STRIPE_SECRET_KEY: 'sk_test_fixture',
  HIVE_STRIPE_WEBHOOK_SECRET: 'whsec_fixture',
  MAP_STRIPE_SECRET_KEY: 'sk_test_fixture',
  MAP_STRIPE_WEBHOOK_SECRET: 'whsec_fixture',
};
beforeAll(async () => {
  for (const [key, value] of Object.entries(credentials)) vi.stubEnv(key, value);
  harness = await createHarness();
  instance = await harness.start({
    origin: 'https://test.glob2.invalid',
    instance: {
      hiveMind: {
        enabled: true,
        salesEnabled: true,
        model: 'test-model',
        rate: { version: 'test-1', input: 1000, cachedInput: 100, output: 5000 },
        packs: [{ id: 'test', priceId: 'price_test', amount: 500, credits: 10, currency: 'cad' }],
      },
    },
  });
});
afterAll(async () => {
  await harness?.close();
  vi.unstubAllEnvs();
});
it('returns a client error for invalid signatures on both credit webhook routes', async () => {
  for (const url of ['/api/v1/hive/stripe', '/api/v1/map-studio/stripe']) {
    const response = await instance.app.inject({
      method: 'POST',
      url,
      headers: { 'content-type': 'application/json', 'stripe-signature': 'invalid' },
      payload: '{}',
    });
    expect(response.statusCode).toBe(400);
  }
});
