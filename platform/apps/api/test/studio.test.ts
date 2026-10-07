import sharp from 'sharp';
import { randomUUID } from 'node:crypto';
import { beforeAll, afterAll, it, expect, vi } from 'vitest';
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
    const thread = randomUUID();
    const creation = { id: thread, title: 'Private pond country' };
    for (let attempt = 0; attempt < 2; attempt++) {
      const response = await call(
        'POST',
        '/api/v1/map-studio/threads',
        owner.accessToken,
        creation,
      );
      expect(response.status).toBe(200);
      expect(await response.json()).toEqual({ id: thread });
    }
    expect(
      (await call('POST', '/api/v1/map-studio/threads', other.accessToken, creation)).status,
    ).toBe(409);
    expect(
      (
        await call('POST', '/api/v1/map-studio/threads', owner.accessToken, {
          ...creation,
          title: 'Different title',
        })
      ).status,
    ).toBe(409);
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
    const previewHash = await blobs.write(
      await sharp({ create: { width: 512, height: 256, channels: 3, background: '#214355' } })
        .png()
        .toBuffer(),
      'image/png',
    );
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
    expect((await call('GET', path + '/preview.webp')).status).toBe(404);
    expect((await call('GET', path + '/preview.webp', other.accessToken)).status).toBe(404);
    expect((await call('GET', path + '/preview.webp', owner.accessToken)).status).toBe(200);
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
    expect((await call('GET', path + '/preview.webp')).status).toBe(200);
    expect((await call('GET', `/api/v1/blobs/maps/${mapHash}`)).status).toBe(200);
    expect(
      (await call('GET', `/api/v1/map-studio/threads/${thread}`, other.accessToken)).status,
    ).toBe(404);
  } finally {
    owner.client.close();
    other.client.close();
  }
});

it('retains the signed payment webhook after disabling generation and removing sale packs', async () => {
  vi.stubEnv('MAP_STRIPE_SECRET_KEY', 'sk_test_fixture');
  vi.stubEnv('MAP_STRIPE_WEBHOOK_SECRET', 'whsec_fixture');
  try {
    const paused = await harness.start({ origin: 'https://studio.example.test' });
    const response = await fetch(paused.url + '/api/v1/map-studio/stripe', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: '{}',
    });
    expect(response.status).toBe(400);
    expect(await response.json()).toMatchObject({
      code: 'bad_request',
      message: 'Missing payment signature.',
    });
  } finally {
    vi.unstubAllEnvs();
  }
});

it('streams durable progress, resumes by event id and closes after authorization is revoked', async () => {
  const { Studio } = await import('@glob2/map-studio');
  const { AgentBlobs } = await import('@glob2/engine/blobs');
  const { registeredPlayer } = await import('./playSupport.ts');
  const { sql } = await import('kysely');
  const { notify } = await import('@glob2/map-studio');
  const { sessionCookieName } = await import('../src/identity.ts');
  const instance = await harness.start({
    instance: { auth: { providers: [], local: { enabled: true } } },
  });
  const owner = await registeredPlayer(instance, 'StreamOwner');
  const other = await registeredPlayer(instance, 'StreamOther');
  const controller = new AbortController();
  const resumedController = new AbortController();
  try {
    const studio = new Studio(harness.database.db);
    await studio.credits.adjust(owner.accountId, randomUUID(), 1, 'grant');
    const thread = (await studio.create(owner.accountId, 'Streamed terrain')).id;
    await studio.submit(
      owner.accountId,
      thread,
      'chat',
      { id: randomUUID(), text: 'A river valley' },
      'v1',
    );
    const row = (await studio.request(
      (await studio.get(owner.accountId, thread)).requests[0]!.id,
    ))!;
    const path = `/api/v1/map-studio/threads/${thread}`;
    const headers = { authorization: `Bearer ${owner.accessToken}` };
    expect(
      await (await fetch(instance.url + '/api/v1/map-studio/account', { headers })).json(),
    ).toMatchObject({ activeRequest: { id: row.id, threadId: thread, status: 'queued' } });
    expect(
      (
        await fetch(instance.url + path + '/events', {
          headers: { authorization: `Bearer ${other.accessToken}` },
        })
      ).status,
    ).toBe(404);
    const initial = await studio.get(owner.accountId, thread);
    const response = await fetch(instance.url + path + `/events?cursor=${initial.cursor}`, {
      headers,
      signal: controller.signal,
    });
    expect(response.headers.get('content-type')).toContain('text/event-stream');
    const reader = response.body!.getReader();
    const decoder = new TextDecoder();
    let text = '';
    const until = async (needle: string) => {
      while (!text.includes(needle)) {
        const chunk = await reader.read();
        if (chunk.done) throw new Error('Stream closed before ' + needle);
        text += decoder.decode(chunk.value);
      }
    };
    await until(': connected');
    await studio.stage(row, 'prepare', 'running');
    await until('Prepare the design');
    const event = (await studio.events(owner.accountId, thread, initial.cursor!)).at(-1)!;
    expect(text).toContain(`id: ${event.id}`);
    controller.abort();
    await studio.stage(row, 'prepare', 'complete');
    const secret = await instance.app.identity.webSessions.create(owner.accountId);
    const resumed = await fetch(instance.url + path + '/events', {
      headers: {
        cookie: `${sessionCookieName(instance.app.identity)}=${secret}`,
        'last-event-id': event.id,
      },
      signal: resumedController.signal,
    });
    const nextReader = resumed.body!.getReader();
    let next = '';
    while (!next.includes('"status":"complete"')) {
      const chunk = await nextReader.read();
      next += decoder.decode(chunk.value);
    }
    expect(next).not.toContain(`id: ${event.id}\n`);
    // Simulate a lost Postgres notification: commit an event without NOTIFY.
    // The heartbeat catch-up must still deliver it on this open connection.
    await harness.database.db.transaction().execute(async (db) => {
      const cursor = (
        await sql<{
          cursor: number;
        }>`UPDATE studio_threads SET event_cursor=event_cursor+1 WHERE id=${thread} RETURNING event_cursor AS cursor`.execute(
          db,
        )
      ).rows[0]!.cursor;
      await sql`INSERT INTO studio_events(thread_id,cursor,request_id,dedup,type,payload) VALUES(${thread},${cursor},${row.id},'lost-notify','stage','{"id":"terrain","label":"Recovered stage","status":"running"}')`.execute(
        db,
      );
    });
    while (!next.includes('Recovered stage')) {
      const chunk = await nextReader.read();
      if (chunk.done) throw new Error('Lost event');
      next += decoder.decode(chunk.value);
    }
    expect(next).toContain(': heartbeat');
    const blobs = new AgentBlobs(harness.blobs, harness.database.db);
    const hash = await blobs.write(Buffer.from('private-stage-image'), 'image/png');
    const artifact = await studio.artifact(row, {
      stage: 'prepare',
      kind: 'reference',
      label: 'Reference sheet',
      hash,
    });
    expect((await fetch(instance.url + artifact.url, { headers })).status).toBe(200);
    expect(
      (
        await fetch(instance.url + artifact.url, {
          headers: { authorization: `Bearer ${other.accessToken}` },
        })
      ).status,
    ).toBe(404);
    const progress = await fetch(instance.url + path + `/requests/${row.id}/progress`, { headers });
    expect(await progress.json()).toMatchObject({
      requestId: row.id,
      artifacts: [{ id: artifact.id }],
      historical: false,
    });
    await sql`UPDATE web_sessions SET expires_at=now()-interval '1 second' WHERE account_id=${owner.accountId}`.execute(
      harness.database.db,
    );
    await notify(harness.database.db, owner.accountId, thread);
    let ended = false;
    for (let n = 0; n < 10 && !ended; n++) ended = (await nextReader.read()).done;
    expect(ended).toBe(true);
  } finally {
    controller.abort();
    resumedController.abort();
    owner.client.close();
    other.client.close();
  }
});

it('accepts idempotent conversation turns with fixed settings while enforcing ownership and schema', async () => {
  const { Studio } = await import('@glob2/map-studio');
  const { registeredPlayer } = await import('./playSupport.ts');
  const enabled = await harness.start({
    instance: {
      auth: { providers: [], local: { enabled: true } },
      mapStudio: {
        enabled: true,
        salesEnabled: false,
        textModel: 'mock',
        imageModel: 'mock',
        pipelineVersion: 'v1',
        providerCallsPerDay: 20,
      },
    },
  });
  try {
    const owner = await registeredPlayer(enabled, 'TurnOwner');
    const other = await registeredPlayer(enabled, 'TurnOther');
    const studio = new Studio(harness.database.db);
    await studio.credits.adjust(owner.accountId, randomUUID(), 1, 'grant');
    const thread = (await studio.create(owner.accountId, 'Turn test')).id;
    const payload = {
      id: randomUUID(),
      text: 'Create islands',
      settings: { width: 128, height: 256, players: 2 },
    };
    const call = (token: string, value: unknown) =>
      fetch(`${enabled.url}/api/v1/map-studio/threads/${thread}/turns`, {
        method: 'POST',
        headers: { authorization: `Bearer ${token}`, 'content-type': 'application/json' },
        body: JSON.stringify(value),
      });
    expect((await call(other.accessToken, payload)).status).toBe(404);
    expect((await call(owner.accessToken, { ...payload, settings: undefined })).status).toBe(400);
    expect((await call(owner.accessToken, { ...payload, parent: randomUUID() })).status).toBe(400);
    for (let attempt = 0; attempt < 2; attempt++) {
      const response = await call(owner.accessToken, payload);
      expect(response.status).toBe(200);
      expect(await response.json()).toEqual({ id: payload.id });
    }
    expect((await studio.request(payload.id))?.input).toMatchObject({
      turn: true,
      settings: payload.settings,
    });
    expect((await studio.credits.balance(owner.accountId)).reserved).toBe(0);
  } finally {
    await enabled.close();
  }
});
