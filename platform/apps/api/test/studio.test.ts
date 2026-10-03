import { randomUUID } from 'node:crypto';
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
it('requires a registered account and keeps generation and purchases disabled by default', async () => {
  expect((await fetch(api.url + '/api/v1/map-studio/account')).status).toBe(401);
  const response = await postJson(api.url + '/api/v1/auth/guest', { platform: 'desktop' }),
    session = (await response.json()) as {
      account: { id: string };
      tokens: { accessToken: string };
    };
  const headers = { authorization: 'Bearer ' + session.tokens.accessToken };
  expect((await fetch(api.url + '/api/v1/map-studio/account', { headers })).status).toBe(403);
  await harness.database.db
    .updateTable('accounts')
    .set({ kind: 'registered', display_name: randomUUID().slice(0, 24) })
    .where('id', '=', session.account.id)
    .execute();
  const wallet = await fetch(api.url + '/api/v1/map-studio/account', { headers });
  expect(wallet.status).toBe(200);
  expect(await wallet.json()).toMatchObject({ enabled: false, available: 0, packs: [] });
  expect(
    (
      await fetch(api.url + '/api/v1/map-studio/checkout', {
        method: 'POST',
        headers: { ...headers, 'content-type': 'application/json' },
        body: JSON.stringify({ pack: 'small' }),
      })
    ).status,
  ).toBe(403);
  expect(
    (
      await fetch(api.url + '/api/v1/map-studio/threads', {
        method: 'POST',
        headers: { ...headers, 'content-type': 'application/json' },
        body: JSON.stringify({ title: 'River' }),
      })
    ).status,
  ).toBe(403);
});

it('keeps studio history and previews private while supporting owner download, room play and selected publication', async () => {
  const { Studio } = await import('@glob2/map-studio');
  const { AgentBlobs } = await import('@glob2/engine/blobs');
  const { registeredPlayer, serveSim, fakeMapBytes } = await import('./playSupport.ts');
  const { SIM } = await import('./support.ts');
  const { simVersionKey } = await import('@glob2/protocol');
  await serveSim(harness.database.db);
  const enabled = await harness.start({
    instance: {
      auth: { providers: [], local: { enabled: true } },
      mapStudio: {
        enabled: true,
        salesEnabled: false,
        textModel: 'mock-text',
        imageModel: 'mock-image',
        pipelineVersion: 'v1',
        providerCallsPerDay: 20,
      },
    },
  });
  const owner = await registeredPlayer(enabled, 'StudioOwner');
  const other = await registeredPlayer(enabled, 'StudioOther');
  const call = (method: string, path: string, token?: string, payload?: unknown) =>
    fetch(enabled.url + path, {
      method,
      headers: {
        ...(token ? { authorization: 'Bearer ' + token } : {}),
        ...(payload ? { 'content-type': 'application/json' } : {}),
      },
      ...(payload ? { body: JSON.stringify(payload) } : {}),
    });
  try {
    const studio = new Studio(harness.database.db),
      blobs = new AgentBlobs(harness.blobs, harness.database.db);
    await studio.credits.adjust(owner.accountId, randomUUID(), 3, 'grant');
    const thread = (await studio.create(owner.accountId, 'Private pond country')).id;
    await harness.database.db
      .insertInto('studio_messages')
      .values({
        id: randomUUID(),
        thread_id: thread,
        role: 'user',
        text: 'Private design feedback',
      })
      .execute();
    const settings = { width: 256, height: 128, players: 4 } as const;
    const generation = await studio.submit(
      owner.accountId,
      thread,
      'generate',
      { id: randomUUID(), settings },
      'v1',
    );
    const row = (await studio.request(generation.id))!;
    const mapHash = await blobs.write(fakeMapBytes(4, 791), 'application/x-glob2-map');
    const previewHash = await blobs.write(Buffer.from('private-preview'), 'image/png');
    await studio.finish(row, {
      mapHash,
      previewHash,
      width: 256,
      height: 128,
      players: 4,
      simVersion: simVersionKey(SIM),
      size: 64,
      previewWidth: 512,
      previewHeight: 256,
      provenance: {},
    });
    const ready = (await studio.request(generation.id))!;
    const path = `/api/v1/maps/${ready.map_id}/versions/${ready.map_hash}`;
    expect(
      (await call('GET', `/api/v1/map-studio/threads/${thread}`, other.accessToken)).status,
    ).toBe(404);
    expect((await call('GET', path + '/preview.png')).status).toBe(404);
    expect((await call('GET', path + '/preview.png', other.accessToken)).status).toBe(404);
    expect((await call('GET', path + '/preview.png', owner.accessToken)).status).toBe(200);
    expect((await call('GET', path + '/file', owner.accessToken)).status).toBe(200);
    expect((await call('GET', `/api/v1/blobs/maps/${mapHash}`, other.accessToken)).status).toBe(
      404,
    );
    const upload = await fetch(enabled.url + `/api/v1/maps/${ready.map_id}/versions`, {
      method: 'POST',
      headers: {
        authorization: 'Bearer ' + owner.accessToken,
        'content-type': 'application/octet-stream',
      },
      body: fakeMapBytes(4, 792),
    });
    expect(upload.status).toBe(409);
    const hostPath = `/api/v1/map-studio/threads/${thread}/versions/${generation.id}/room`;
    expect((await call('POST', hostPath, other.accessToken, {})).status).toBe(404);
    const hosted = await call('POST', hostPath, owner.accessToken, {});
    expect(hosted.status).toBe(200);
    const room = (await hosted.json()) as { code: string };
    expect(await (await call('POST', hostPath, owner.accessToken, {})).json()).toEqual(room);
    await other.client.ok('room.join', { code: room.code });
    expect((await call('GET', `/api/v1/blobs/maps/${mapHash}`, other.accessToken)).status).toBe(
      200,
    );
    expect(
      (
        await call('PATCH', `/api/v1/maps/${ready.map_id}`, owner.accessToken, {
          visibility: 'public',
        })
      ).status,
    ).toBe(200);
    expect((await call('GET', path + '/preview.png')).status).toBe(200);
    expect((await call('GET', `/api/v1/blobs/maps/${mapHash}`)).status).toBe(200);
    expect(
      (await call('GET', `/api/v1/map-studio/threads/${thread}`, other.accessToken)).status,
    ).toBe(404);
  } finally {
    owner.client.close();
    other.client.close();
  }
});
