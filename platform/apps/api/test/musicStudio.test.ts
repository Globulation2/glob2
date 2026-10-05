import { afterAll, beforeAll, expect, it } from 'vitest';
import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import { MusicStudio } from '@glob2/music-studio';
import { AgentBlobs } from '@glob2/engine/blobs';
import { registeredPlayer, type Player } from './playSupport.ts';
import { createHarness, type Harness, type Instance } from './support.ts';
let harness: Harness, instance: Instance, owner: Player, other: Player, studio: MusicStudio;
beforeAll(async () => {
  harness = await createHarness();
  studio = new MusicStudio(harness.database.db);
  instance = await harness.start({
    instance: {
      auth: { providers: [], local: { enabled: true } },
      musicStudio: {
        enabled: true,
        salesEnabled: false,
        textModel: 'test',
        pipelineVersion: 'music-v1',
        providerCallsPerDay: 100,
        maxCalls: 12,
        maxOutputTokens: 16000,
        maxTotalTokens: 250000,
        timeoutSeconds: 1800,
      },
    },
  });
  owner = await registeredPlayer(instance, 'MusicStudioOwner');
  other = await registeredPlayer(instance, 'MusicStudioOther');
  await studio.credits.adjust(owner.accountId, randomUUID(), 3, 'grant');
});
afterAll(async () => {
  owner?.client.close();
  other?.client.close();
  await harness?.close();
});
async function call(method: string, path: string, player?: Player, value?: unknown) {
  return fetch(instance.url + path, {
    method,
    headers: {
      ...(player ? { authorization: `Bearer ${player.accessToken}` } : {}),
      ...(value === undefined ? {} : { 'content-type': 'application/json' }),
    },
    ...(value === undefined ? {} : { body: JSON.stringify(value) }),
  });
}
it('keeps drafts and studio evidence private, publishes only a selected immutable release', async () => {
  const thread = (await studio.create(owner.accountId, 'Private acoustic piece')).id;
  await sql`INSERT INTO music_studio_messages(id,thread_id,role,text) VALUES(${randomUUID()},${thread},'user','Private composition request')`.execute(
    harness.database.db,
  );
  const { id } = await studio.submit(
    owner.accountId,
    thread,
    'generate',
    { id: randomUUID(), settings: { pipeline: 'acoustic-v1', seed: 0 } },
    'music-v1',
  );
  const row = (await studio.request(id))!,
    blobs = new AgentBlobs(harness.blobs, harness.database.db);
  const hash = await blobs.write(Buffer.from('fixture audio'), 'audio/ogg');
  const artifact = await studio.artifact(row, {
    stage: 'checks',
    kind: 'preview',
    label: 'Private candidate',
    hash,
  });
  await studio.finish(row, {
    metadata: {
      title: 'Private acoustic piece',
      artist: 'Music Studio',
      description: '',
      license: 'CC-BY-4.0',
      credits: 'AI-composed',
      sources: [],
      tags: [],
      aiGenerated: true,
    },
    result: { frames: 2880000, tracks: [], warnings: [] },
    assets: ['calm', 'building', 'combat'].map((kind) => ({ kind, hash })),
    checks: [],
  });
  expect((await call('GET', `/api/v1/music-studio/threads/${thread}`, other)).status).toBe(404);
  expect((await call('GET', artifact.url, other)).status).toBe(404);
  expect((await call('GET', artifact.url, owner)).status).toBe(200);
  expect((await call('GET', `/api/v1/music/${id}`, other)).status).toBe(404);
  expect(
    (await call('POST', `/api/v1/music/${id}/convert`, owner, { repair: 'none', master: true }))
      .status,
  ).toBe(409);
  expect((await call('POST', `/api/v1/music/${id}/publish`, owner, {})).status).toBe(400);
  expect(
    (await call('POST', `/api/v1/music/${id}/publish`, owner, { license: 'CC0-1.0' })).status,
  ).toBe(400);
  expect(
    (await call('POST', `/api/v1/music/${id}/publish`, owner, { license: 'CC-BY-4.0' })).status,
  ).toBe(200);
  const view = await call('GET', `/api/v1/music/${id}`, other);
  expect(view.status).toBe(200);
  expect(await view.json()).toMatchObject({ generated: true, metadata: { aiGenerated: true } });
  expect((await call('GET', artifact.url, other)).status).toBe(404);
  expect((await call('GET', `/api/v1/music-studio/threads/${thread}/events`, other)).status).toBe(
    404,
  );
  const exported = await call('GET', '/api/v1/accounts/me/export', owner);
  expect(exported.status).toBe(200);
  expect(await exported.json()).toMatchObject({
    musicStudio: { threads: expect.arrayContaining([expect.objectContaining({ id: thread })]) },
  });
  expect((await call('DELETE', `/api/v1/music-studio/threads/${thread}`, owner)).status).toBe(200);
  expect((await call('GET', artifact.url, owner)).status).toBe(404);
  expect((await call('GET', `/api/v1/music/${id}`, other)).status).toBe(200);
});
it('does not sell credits when sales are disabled', async () => {
  const response = await call('GET', '/api/v1/music-studio/account', owner);
  expect(response.status).toBe(200);
  expect(await response.json()).toMatchObject({ enabled: true, packs: [] });
  expect(
    (await call('POST', '/api/v1/music-studio/checkout', owner, { pack: 'music' })).status,
  ).toBe(403);
});

it('bounds concurrent SSE connections', async () => {
  const thread = (await studio.create(owner.accountId, 'Stream test')).id;
  const path = `/api/v1/music-studio/threads/${thread}/events`;
  const connections: Response[] = [];
  try {
    for (let i = 0; i < 3; i++) {
      const response = await call('GET', path, owner);
      expect(response.status).toBe(200);
      connections.push(response);
    }
    expect((await call('GET', path, owner)).status).toBe(429);
  } finally {
    await Promise.all(connections.map((r) => r.body?.cancel()));
  }
});
