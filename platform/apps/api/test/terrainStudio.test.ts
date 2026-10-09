import { newPackage, namespace, type SetDraft } from '@glob2/protocol';
import { AgentBlobs } from '@glob2/engine/blobs';
import { beforeAll, afterAll, it, expect } from 'vitest';
import { randomUUID } from 'node:crypto';
import sharp from 'sharp';
import { TerrainStudio } from '@glob2/terrain-studio';
import { registeredPlayer, type Player } from './playSupport.ts';
import { createHarness, type Harness, type Instance } from './support.ts';
let harness: Harness, instance: Instance, owner: Player, other: Player, studio: TerrainStudio;
beforeAll(async () => {
  harness = await createHarness();
  studio = new TerrainStudio(harness.database.db);
  instance = await harness.start({
    instance: {
      auth: { providers: [], local: { enabled: true } },
      terrainStudio: {
        enabled: true,
        salesEnabled: false,
        textModel: 'test',
        imageModel: 'test',
        pipelineVersion: 'terrain-v1',
        maxOutputTokens: 16000,
        providerCallsPerDay: 100,
        timeoutSeconds: 1800,
      },
    },
  });
  owner = await registeredPlayer(instance, 'TerrainOwner');
  other = await registeredPlayer(instance, 'TerrainOther');
  await studio.credits.adjust(owner.accountId, randomUUID(), 3, 'grant');
});
afterAll(async () => {
  owner?.client.close();
  other?.client.close();
  await harness?.close();
});
function call(method: string, path: string, player?: Player, value?: unknown) {
  return fetch(instance.url + path, {
    method,
    headers: {
      ...(player ? { authorization: 'Bearer ' + player.accessToken } : {}),
      ...(value === undefined
        ? {}
        : {
            'content-type':
              value instanceof Uint8Array ? 'application/octet-stream' : 'application/json',
          }),
    },
    ...(value === undefined
      ? {}
      : { body: value instanceof Uint8Array ? new Uint8Array(value) : JSON.stringify(value) }),
  });
}
it('creates a private ordinary set draft and exposes owned studio routes', async () => {
  const id = randomUUID(),
    created = await call('POST', '/api/v1/terrain-studio/threads', owner, {
      id,
      title: 'Fungal world',
    });
  expect(created.status).toBe(200);
  expect((await call('GET', '/api/v1/terrain-studio/threads/' + id, other)).status).toBe(404);
  const t = (await (await call('GET', '/api/v1/terrain-studio/threads/' + id, owner)).json()) as {
    draftId: string;
  };
  const d = (await (await call('GET', '/api/v1/set-drafts/' + t.draftId, owner)).json()) as {
    package: { title: string };
    revision: number;
  };
  expect(d.package.title).toBe('Fungal world');
  expect(d.revision).toBe(0);
  const submission = {
    id: randomUUID(),
    text: 'Make a fungal marsh',
    expectedRevision: 0,
    references: [],
  };
  expect(
    (await call('POST', `/api/v1/terrain-studio/threads/${id}/turns`, owner, submission)).status,
  ).toBe(200);
  expect(
    (await call('POST', `/api/v1/terrain-studio/threads/${id}/turns`, owner, submission)).status,
  ).toBe(200);
  expect(
    (
      await call('POST', `/api/v1/terrain-studio/threads/${id}/turns`, owner, {
        ...submission,
        text: 'Different',
      })
    ).status,
  ).toBe(409);
  expect(
    (
      await call(
        'POST',
        `/api/v1/terrain-studio/threads/${id}/requests/${submission.id}/cancel`,
        owner,
        {},
      )
    ).status,
  ).toBe(200);
});
it('keeps reference images private and rejects foreign references', async () => {
  const id = randomUUID();
  await call('POST', '/api/v1/terrain-studio/threads', owner, { id, title: 'References' });
  const bytes = await sharp({
    create: { width: 32, height: 32, channels: 4, background: '#608050' },
  })
    .png()
    .toBuffer();
  const upload = await call(
    'POST',
    `/api/v1/terrain-studio/threads/${id}/references`,
    owner,
    bytes,
  );
  expect(upload.status).toBe(200);
  const ref = (await upload.json()) as { url: string };
  expect((await call('GET', ref.url, owner)).status).toBe(200);
  expect((await call('GET', ref.url, other)).status).toBe(404);
  expect(
    (
      await call('POST', `/api/v1/terrain-studio/threads/${id}/turns`, owner, {
        id: randomUUID(),
        text: 'Use this',
        expectedRevision: 0,
        references: ['a'.repeat(64)],
      })
    ).status,
  ).toBe(400);
  expect(
    (
      await call(
        'POST',
        `/api/v1/terrain-studio/threads/${id}/references`,
        owner,
        Buffer.from('invalid image'),
      )
    ).status,
  ).toBe(400);
});
it('requires a registered account and keeps credit products independent', async () => {
  expect(
    (
      await call('POST', '/api/v1/terrain-studio/threads', undefined, {
        id: randomUUID(),
        title: 'No user',
      })
    ).status,
  ).toBe(401);
  const account = (await (await call('GET', '/api/v1/terrain-studio/account', owner)).json()) as {
    available: number;
    enabled: boolean;
  };
  expect(account.available).toBe(3);
  expect(account.enabled).toBe(true);
  expect(
    (await call('POST', '/api/v1/terrain-studio/checkout', owner, { pack: 'no-sales' })).status,
  ).toBe(403);
});

it('remixes only accessible releases and preserves source credits and stable retry identity', async () => {
  const pack = newPackage('Original artist');
  pack.title = 'Original marsh';
  pack.terrains.push({
    key: namespace(pack) + 'marsh',
    name: 'Marsh',
    base: 'marsh',
    properties: {},
  });
  const hash = await new AgentBlobs(harness.blobs, harness.database.db).write(
    Buffer.from(JSON.stringify(pack)),
    'application/json',
  );
  await harness.database.db
    .insertInto('asset_sets')
    .values({
      id: pack.setId,
      owner_account_id: owner.accountId,
      title: pack.title,
      visibility: 'private',
    })
    .execute();
  await harness.database.db
    .insertInto('set_versions')
    .values({
      id: pack.versionId,
      set_id: pack.setId,
      hash,
      label: 'v1',
      license: pack.license,
      credits: JSON.stringify(pack.credits),
      sim_version: '144-1-' + 'a'.repeat(64),
      min_version_minor: 144,
      report: {
        hash,
        suite: 1,
        valid: true,
        minVersionMinor: 144,
        terrainCount: 1,
        resourceCount: 0,
      },
    })
    .execute();
  const input = { id: randomUUID(), title: 'Remixed marsh', versionId: pack.versionId };
  expect((await call('POST', '/api/v1/terrain-studio/threads', other, input)).status).toBe(404);
  await harness.database.db
    .updateTable('asset_sets')
    .set({ visibility: 'public' })
    .where('id', '=', pack.setId)
    .execute();
  expect((await call('POST', '/api/v1/terrain-studio/threads', other, input)).status).toBe(200);
  expect((await call('POST', '/api/v1/terrain-studio/threads', other, input)).status).toBe(200);
  const thread = await studio.get(other.accountId, input.id);
  const draft = (await (
    await call('GET', '/api/v1/set-drafts/' + thread.draftId, other)
  ).json()) as SetDraft;
  expect(draft.package.credits).toContainEqual(pack.credits[0]);
  expect(draft.package.setId).not.toBe(pack.setId);
  expect(draft.package.terrains[0]?.['key']).toBe(namespace(draft.package) + 'marsh');
  expect(
    (
      await call('POST', '/api/v1/terrain-studio/threads', other, {
        ...input,
        versionId: randomUUID(),
      })
    ).status,
  ).toBe(404);
});
it('publishes a delivered generation through ordinary set publishing with its rendered preview', async () => {
  const pack = newPackage('TerrainOwner'),
    thread = randomUUID(),
    id = randomUUID();
  await studio.create(owner.accountId, 'Generated set', thread, pack);
  await studio.submit(
    owner.accountId,
    thread,
    { id, text: 'Create terrain', expectedRevision: 0, references: [] },
    { enabled: true, salesEnabled: false, providerCallsPerDay: 100 },
  );
  const row = await studio.claim();
  expect(row?.id).toBe(id);
  if (!row) throw Error('Missing claimed request');
  await studio.reserveBuild(row);
  const blobs = new AgentBlobs(harness.blobs, harness.database.db);
  const hash = await blobs.write(Buffer.from(JSON.stringify(pack)), 'application/json');
  const png = await sharp({ create: { width: 32, height: 32, channels: 4, background: '#608050' } })
    .png()
    .toBuffer();
  const previewHash = await blobs.write(png, 'image/png');
  await studio.finish(row, {
    package: pack,
    hash,
    report: {
      hash,
      suite: 1,
      valid: true,
      minVersionMinor: 144,
      terrainCount: 0,
      resourceCount: 0,
      previewHash,
    },
    simVersion: '144-1-' + 'a'.repeat(64),
    text: 'Ready',
  });
  const snapshot = await studio.get(owner.accountId, thread);
  const result = await call('POST', '/api/v1/set-drafts/' + snapshot.draftId + '/publish', owner, {
    revision: 1,
    label: 'v1',
    notes: 'Generated terrain',
    visibility: 'public',
  });
  expect(result.status).toBe(200);
  const release = await harness.database.db
    .selectFrom('set_versions')
    .select(['hash', 'preview_hash'])
    .where('id', '=', pack.versionId)
    .executeTakeFirstOrThrow();
  expect(release).toEqual({ hash, preview_hash: previewHash });
});
it('preserves reservations and journals when ordinary deletion targets active studio work', async () => {
  const pack = newPackage('TerrainOwner'),
    thread = randomUUID(),
    id = randomUUID();
  await studio.create(owner.accountId, 'Deletion protection', thread, pack);
  await studio.submit(
    owner.accountId,
    thread,
    {
      id,
      text: 'Create terrain',
      expectedRevision: 0,
      references: [],
    },
    { enabled: true, salesEnabled: false, providerCallsPerDay: 100 },
  );
  const row = await studio.claim();
  if (!row || row.id !== id) throw Error('Missing claimed request');
  await studio.reserveBuild(row);
  await harness.database.db
    .insertInto('terrain_studio_attempts')
    .values({
      id: randomUUID(),
      request_id: id,
      stage: 'image',
      model: 'test',
      status: 'uncertain',
      input: {},
    })
    .execute();
  await studio.checkpoint(row, 'uncertain', {});
  for (const path of ['/api/v1/set-drafts/' + pack.versionId, '/api/v1/sets/' + pack.setId]) {
    expect((await call('DELETE', path, other)).status).toBe(404);
    expect((await call('DELETE', path, owner)).status).toBe(409);
  }
  expect((await studio.credits.balance(owner.accountId)).reserved).toBe(1);
  expect(await studio.request(id)).toMatchObject({ status: 'uncertain' });
  expect(
    await harness.database.db
      .selectFrom('terrain_studio_attempts')
      .select('status')
      .where('request_id', '=', id)
      .executeTakeFirst(),
  ).toEqual({ status: 'uncertain' });
  await studio.finish(row, undefined, 'Reconciled failure');
  expect((await studio.credits.balance(owner.accountId)).reserved).toBe(0);
  expect((await call('DELETE', '/api/v1/set-drafts/' + pack.versionId, owner)).status).toBe(204);
  expect(await studio.request(id)).toBeUndefined();
  expect((await call('DELETE', '/api/v1/sets/' + pack.setId, owner)).status).toBe(204);
});
