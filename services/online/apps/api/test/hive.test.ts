import { beforeAll, afterAll, it, expect } from 'vitest';
import { createHarness, postJson, type Harness, type Instance } from './support.ts';
let harness: Harness, api: Instance;
beforeAll(async () => {
  harness = await createHarness();
  api = await harness.start();
});
afterAll(async () => {
  await harness?.close();
});
it('keeps the commander disabled by default and requires authentication for balance', async () => {
  expect((await fetch(api.url + '/api/v1/hive/account')).status).toBe(401);
  const response = await postJson(api.url + '/api/v1/auth/guest', { platform: 'desktop' });
  const session = (await response.json()) as { tokens: { accessToken: string } };
  const headers = { authorization: 'Bearer ' + session.tokens.accessToken };
  const account = await fetch(api.url + '/api/v1/hive/account', { headers });
  expect(account.status).toBe(200);
  expect(await account.json()).toMatchObject({ enabled: false, available: 0, packs: [] });
  const checkout = await fetch(api.url + '/api/v1/hive/checkout', {
    method: 'POST',
    headers: { ...headers, 'content-type': 'application/json' },
    body: JSON.stringify({ pack: 'small' }),
  });
  expect(checkout.status).toBe(403);
});
