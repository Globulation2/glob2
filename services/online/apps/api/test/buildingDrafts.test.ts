import sharp from 'sharp';
import { afterAll, beforeAll, expect, it } from 'vitest';
import type { BuildingDraft } from '@glob2/protocol';
import { readBuildingArchive } from '@glob2/protocol/node';
import { createHarness, type Harness, type Instance } from './support.ts';
import { registeredPlayer, type Player } from './playSupport.ts';
let harness: Harness, instance: Instance, owner: Player, other: Player;
beforeAll(async () => {
  harness = await createHarness();
  instance = await harness.start({
    instance: { auth: { providers: [], local: { enabled: true } } },
  });
  owner = await registeredPlayer(instance, 'BuildingOwner');
  other = await registeredPlayer(instance, 'BuildingOther');
});
afterAll(async () => {
  owner?.client.close();
  other?.client.close();
  await harness?.close();
});
it('keeps drafts private and rejects concurrent or forged writes atomically', async () => {
  const app = instance.app,
    headers = { authorization: `Bearer ${owner.accessToken}` };
  expect((await app.inject({ method: 'POST', url: '/api/v1/building-drafts' })).statusCode).toBe(
    401,
  );
  const created = await app.inject({ method: 'POST', url: '/api/v1/building-drafts', headers });
  expect(created.statusCode, created.body).toBe(201);
  const draft = created.json<BuildingDraft>(),
    url = '/api/v1/building-drafts/' + draft.id;
  expect(
    (await app.inject({ url, headers: { authorization: `Bearer ${other.accessToken}` } }))
      .statusCode,
  ).toBe(404);
  const payload = { revision: draft.revision, name: 'Kitchen', package: draft.package };
  const results = await Promise.all(
    [0, 1].map(() => app.inject({ method: 'PUT', url, headers, payload })),
  );
  expect(results.map((r) => r.statusCode).sort()).toEqual([200, 409]);
  const current = results.find((r) => r.statusCode === 200)!.json<BuildingDraft>();
  expect(current.revision).not.toBe(draft.revision);
  const forged = structuredClone(current.package);
  forged.variants[0]!.key = 'swarm';
  expect(
    (
      await app.inject({
        method: 'PUT',
        url,
        headers,
        payload: { ...payload, revision: current.revision, package: forged },
      })
    ).statusCode,
  ).toBe(400);
  expect((await app.inject({ url, headers })).json()).toEqual(current);
});
it('normalizes artwork and exports portable archives without exposing private assets', async () => {
  const app = instance.app,
    headers = { authorization: `Bearer ${owner.accessToken}` };
  let draft = (
    await app.inject({ method: 'POST', url: '/api/v1/building-drafts', headers })
  ).json<BuildingDraft>();
  const url = '/api/v1/building-drafts/' + draft.id;
  const png = await sharp({
    create: { width: 16, height: 16, channels: 4, background: '#ab334488' },
  })
    .png()
    .toBuffer();
  const upload = await app.inject({
    method: 'PUT',
    url: `${url}/frame?revision=${draft.revision}&sprite=kitchen&frame=0`,
    headers: { ...headers, 'content-type': 'application/octet-stream' },
    payload: png,
  });
  expect(upload.statusCode, upload.body).toBe(200);
  draft = upload.json<BuildingDraft>();
  draft.package.variants[0]!.properties.gameSprite = 'package:kitchen';
  const saved = await app.inject({
    method: 'PUT',
    url,
    headers,
    payload: { revision: draft.revision, name: 'Artwork kitchen', package: draft.package },
  });
  expect(saved.statusCode, saved.body).toBe(200);
  draft = saved.json<BuildingDraft>();
  const frame = draft.package.sprites[0]!.frames[0]!;
  const art = await app.inject({ url: `${url}/assets/${frame.imageHash}`, headers });
  expect(art.statusCode).toBe(200);
  expect(art.headers['content-type']).toBe('image/webp');
  expect((await app.inject({ url: `${url}/assets/${frame.imageHash}` })).statusCode).toBe(401);
  expect(await sharp(art.rawPayload).raw().toBuffer()).toEqual(await sharp(png).raw().toBuffer());
  const mismatch = await sharp({
    create: { width: 15, height: 16, channels: 4, background: '#ffffff' },
  })
    .png()
    .toBuffer();
  const bad = await app.inject({
    method: 'PUT',
    url: `${url}/frame?revision=${draft.revision}&sprite=kitchen&frame=0&layer=team`,
    headers: { ...headers, 'content-type': 'application/octet-stream' },
    payload: mismatch,
  });
  expect(bad.statusCode, bad.body).toBe(400);
  const team = await app.inject({
    method: 'PUT',
    url: `${url}/frame?revision=${draft.revision}&sprite=kitchen&frame=0&layer=team`,
    headers: { ...headers, 'content-type': 'application/octet-stream' },
    payload: png,
  });
  expect(team.statusCode, team.body).toBe(200);
  draft = team.json<BuildingDraft>();
  const archive = await app.inject({ url: `${url}/archive`, headers });
  expect(readBuildingArchive(archive.rawPayload).package).toEqual(draft.package);
  const restored = await app.inject({
    method: 'PUT',
    url: `${url}/archive?revision=${draft.revision}`,
    headers: { ...headers, 'content-type': 'application/octet-stream' },
    payload: archive.rawPayload,
  });
  expect(restored.statusCode, restored.body).toBe(200);
  draft = restored.json<BuildingDraft>();
  const missing = structuredClone(draft.package);
  missing.sprites[0]!.frames[0]!.imageHash = 'a'.repeat(64);
  expect(
    (
      await app.inject({
        method: 'PUT',
        url,
        headers,
        payload: { revision: draft.revision, name: draft.name, package: missing },
      })
    ).statusCode,
  ).toBe(400);
  expect((await app.inject({ url, headers })).json()).toEqual(draft);
  expect((await app.inject({ method: 'DELETE', url, headers })).statusCode).toBe(204);
});
it('includes portable draft bytes in account exports and removes them on soft deletion', async () => {
  const app = instance.app,
    headers = { authorization: `Bearer ${other.accessToken}` };
  const created = await app.inject({ method: 'POST', url: '/api/v1/building-drafts', headers });
  expect(created.statusCode).toBe(201);
  const draft = created.json<BuildingDraft>();
  const exported = await app.inject({ url: '/api/v1/accounts/me/export', headers });
  expect(exported.statusCode, exported.body).toBe(200);
  const saved = exported.json().buildings.drafts;
  expect(saved).toHaveLength(1);
  expect(saved[0].id).toBe(draft.id);
  expect(readBuildingArchive(Buffer.from(saved[0].archiveBase64, 'base64')).package).toEqual(
    draft.package,
  );
  const deleted = await app.inject({
    method: 'DELETE',
    url: '/api/v1/accounts/me',
    headers,
    payload: { confirmDisplayName: other.displayName },
  });
  expect(deleted.statusCode, deleted.body).toBe(204);
  expect(
    await harness.database.db
      .selectFrom('building_drafts')
      .select('id')
      .where('owner_account_id', '=', other.accountId)
      .execute(),
  ).toEqual([]);
});
