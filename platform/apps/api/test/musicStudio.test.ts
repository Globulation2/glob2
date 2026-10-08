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

it('deleting an account refunds uncertain work and fences its worker without restoring provider capacity', async () => {
  const creator = await registeredPlayer(instance, 'MusicDeletionOwner');
  try {
    await studio.credits.adjust(creator.accountId, randomUUID(), 1, 'grant');
    const thread = (await studio.create(creator.accountId, 'Private unfinished music')).id;
    await sql`INSERT INTO music_studio_messages(id,thread_id,role,text) VALUES(${randomUUID()},${thread},'user','Private request')`.execute(
      harness.database.db,
    );
    const { id } = await studio.submit(
      creator.accountId,
      thread,
      'generate',
      { id: randomUUID(), settings: { pipeline: 'acoustic-v1', seed: 0 } },
      'music-v1',
    );
    const leased = (await studio.claim())!;
    expect(leased.id).toBe(id);
    await sql`INSERT INTO music_studio_attempts(id,request_id,stage,model,status,input) VALUES(${randomUUID()},${id},'agent:0','test','uncertain','{}')`.execute(
      harness.database.db,
    );
    await studio.checkpoint(leased, 'uncertain', {});
    const usage = async () =>
      (
        await sql<{
          count: string;
        }>`SELECT sum(calls)::text AS count FROM music_studio_provider_usage`.execute(
          harness.database.db,
        )
      ).rows[0]?.count;
    const before = await usage();
    expect(
      (
        await call('DELETE', '/api/v1/accounts/me', creator, {
          confirmDisplayName: creator.displayName,
        })
      ).status,
    ).toBe(204);
    expect(await studio.request(id)).toBeUndefined();
    expect(await studio.list(creator.accountId)).toEqual([]);
    expect(await studio.credits.balance(creator.accountId)).toEqual({
      balance: 1,
      reserved: 0,
      available: 1,
    });
    expect(await usage()).toBe(before);
    expect(await studio.heartbeat(leased)).toBe(false);
    await expect(studio.finish(leased, undefined, 'Late worker result')).rejects.toThrow();
    const ledger = await harness.database.db
      .selectFrom('music_ledger')
      .selectAll()
      .where('id', '=', `generation:${id}`)
      .execute();
    expect(ledger).toHaveLength(1);
    expect(ledger[0]?.details).toMatchObject({ returned: true, accountDeleted: true });
  } finally {
    creator.client.close();
  }
});

it('accepts durable turns and retains legacy discussion and generation endpoints', async () => {
  const thread = (await studio.create(owner.accountId, 'Turn API')).id;
  const root = `/api/v1/music-studio/threads/${thread}`;
  const input = {
    id: randomUUID(),
    text: 'What instrument should lead?',
    settings: { pipeline: 'acoustic-v1', seed: 4 },
  };
  expect((await call('POST', root + '/turns', undefined, input)).status).toBe(401);
  expect(
    (await call('POST', root + '/turns', owner, { ...input, settings: undefined })).status,
  ).toBe(400);
  expect((await call('POST', root + '/turns', owner, input)).status).toBe(200);
  expect((await call('POST', root + '/turns', owner, input)).status).toBe(200);
  const row = (await studio.request(input.id))!;
  expect(row.input.turn).toBe(true);
  await studio.finish(row, { text: 'Try flute.', brief: 'Flute', action: 'discuss' });
  const legacy = { id: randomUUID(), text: 'A soft flute melody' };
  expect((await call('POST', root + '/messages', owner, legacy)).status).toBe(200);
  await studio.finish((await studio.request(legacy.id))!, {
    text: 'A gentle melody.',
    brief: 'Flute',
  });
  const build = { id: randomUUID(), settings: input.settings };
  expect((await call('POST', root + '/generate', owner, build)).status).toBe(200);
  await studio.cancel(owner.accountId, thread, build.id);
});
