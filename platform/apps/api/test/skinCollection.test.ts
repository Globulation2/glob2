import sharp from 'sharp';
import { randomUUID } from 'node:crypto';
import { afterAll, beforeAll, expect, it } from 'vitest';
import { createHarness, type Harness, type Instance } from './support.ts';
import { registeredPlayer, type Player } from './playSupport.ts';
import { colourAtlas, materialMap } from './skinImages.ts';
import type { SkinDesign, SkinCollection } from '@glob2/protocol';
let harness: Harness, instance: Instance, owner: Player, other: Player;
let payload: {
  revision: string;
  name: string;
  buildingColor: number;
  imageBase64: string;
  materialBase64: string;
};
const collectionUrl = '/api/v1/skins/collection';
const url = (id: string) => `/api/v1/skins/designs/${id}`;
const headers = () => ({ authorization: `Bearer ${owner.accessToken}` });
beforeAll(async () => {
  harness = await createHarness();
  instance = await harness.start({
    instance: { auth: { providers: [], local: { enabled: true } } },
  });
  owner = await registeredPlayer(instance, 'CollectionOwner');
  other = await registeredPlayer(instance, 'CollectionOther');
  payload = {
    revision: randomUUID(),
    name: 'Painted colony',
    buildingColor: 0x123456,
    imageBase64: (await colourAtlas('#bb2266')).toString('base64'),
    materialBase64: (await materialMap()).toString('base64'),
  };
});
afterAll(async () => {
  owner?.client.close();
  other?.client.close();
  await harness?.close();
});
async function create(name: string, sourceSkinId?: string) {
  const id = randomUUID();
  const response = await instance.app.inject({
    method: 'POST',
    url: '/api/v1/skins/designs',
    headers: headers(),
    payload: { id, name, ...(sourceSkinId ? { sourceSkinId } : {}) },
  });
  expect(response.statusCode, response.body).toBe(200);
  return response.json().design as SkinDesign;
}
it('saves independent private designs without an unlock or creating match snapshots', async () => {
  expect((await instance.app.inject({ url: collectionUrl })).statusCode).toBe(401);
  const first = await create('First');
  const second = await create('Second');
  const saved = await instance.app.inject({
    method: 'PUT',
    url: url(first.skinId),
    headers: headers(),
    payload: { ...payload, revision: first.revision },
  });
  expect(saved.statusCode, saved.body).toBe(200);
  const collection = (
    await instance.app.inject({ url: collectionUrl, headers: headers() })
  ).json() as SkinCollection;
  expect(collection.designs.find((s) => s.skinId === first.skinId)).toMatchObject({
    name: payload.name,
    revision: saved.json().revision,
    appliedVersionId: null,
  });
  expect(collection.designs.find((s) => s.skinId === second.skinId)?.name).toBe('Second');
  expect(collection.canUseCustom).toBe(false);
  expect(
    await harness.database.db
      .selectFrom('colony_skin_versions')
      .select('id')
      .where('skin_id', '=', first.skinId)
      .execute(),
  ).toHaveLength(0);
  const outsider = { authorization: `Bearer ${other.accessToken}` };
  expect(
    (
      await instance.app.inject({
        method: 'PUT',
        url: url(first.skinId),
        headers: outsider,
        payload: { ...payload, revision: saved.json().revision },
      })
    ).statusCode,
  ).toBe(404);
  expect(
    (await instance.app.inject({ url: collectionUrl, headers: outsider })).json().designs,
  ).toHaveLength(0);
  expect(
    (
      await instance.app.inject({
        method: 'POST',
        url: `${url(first.skinId)}/use`,
        headers: headers(),
        payload: { revision: saved.json().revision },
      })
    ).statusCode,
  ).toBe(403);
});
it('creates idempotently and duplicates a working design independently', async () => {
  const first = await create('Original');
  const repeated = await instance.app.inject({
    method: 'POST',
    url: '/api/v1/skins/designs',
    headers: headers(),
    payload: { id: first.skinId, name: 'Original' },
  });
  expect(repeated.json().design.skinId).toBe(first.skinId);
  const copy = await create('Copy', first.skinId);
  expect(copy.skinId).not.toBe(first.skinId);
  expect(copy.imageBase64).toBe(first.imageBase64);
  expect(copy.appliedVersionId).toBeNull();
});
it('rejects simultaneous saves without losing either design or changing equipment', async () => {
  const first = await create('Concurrent');
  const responses = await Promise.all(
    ['Device A', 'Device B'].map((name) =>
      instance.app.inject({
        method: 'PUT',
        url: url(first.skinId),
        headers: headers(),
        payload: { ...payload, name, revision: first.revision },
      }),
    ),
  );
  expect(responses.map((r) => r.statusCode).sort()).toEqual([200, 409]);
  const collection = (
    await instance.app.inject({ url: collectionUrl, headers: headers() })
  ).json() as SkinCollection;
  expect(collection.designs.filter((s) => s.skinId === first.skinId)).toHaveLength(1);
  expect(collection.equippedVersionId).toBeNull();
});
it('applies atomically, deduplicates snapshots, and keeps edits unapplied until requested', async () => {
  await harness.database.db
    .insertInto('entitlements')
    .values({ account_id: owner.accountId, entitlement: 'skins:designer', source: 'test' })
    .execute();
  const first = await create('Active');
  const apply = async (revision: string) =>
    instance.app.inject({
      method: 'POST',
      url: `${url(first.skinId)}/use`,
      headers: headers(),
      payload: { revision },
    });
  const original = await apply(first.revision);
  expect(original.statusCode, original.body).toBe(200);
  const version = original.json().version;
  expect((await apply(first.revision)).json().version.id).toBe(version.id);
  const update = await instance.app.inject({
    method: 'PUT',
    url: url(first.skinId),
    headers: headers(),
    payload: { ...payload, revision: first.revision },
  });
  expect(update.statusCode).toBe(200);
  expect((await apply(first.revision)).statusCode).toBe(409);
  let collection = (
    await instance.app.inject({ url: collectionUrl, headers: headers() })
  ).json() as SkinCollection;
  expect(collection.equippedVersionId).toBe(version.id);
  expect(collection.designs.find((s) => s.skinId === first.skinId)?.appliedRevision).toBe(
    first.revision,
  );
  const changed = await apply(update.json().revision);
  expect(changed.statusCode, changed.body).toBe(200);
  expect(changed.json().version.id).not.toBe(version.id);
  collection = (
    await instance.app.inject({ url: collectionUrl, headers: headers() })
  ).json() as SkinCollection;
  expect(collection.designs.filter((s) => s.skinId === first.skinId)).toHaveLength(1);
  expect(collection.equippedVersionId).toBe(changed.json().version.id);
  const deleted = await instance.app.inject({
    method: 'DELETE',
    url: url(first.skinId),
    headers: headers(),
  });
  expect(deleted.statusCode).toBe(200);
  collection = (
    await instance.app.inject({ url: collectionUrl, headers: headers() })
  ).json() as SkinCollection;
  expect(collection.equippedVersionId).toBeNull();
  expect(collection.designs.some((s) => s.skinId === first.skinId)).toBe(false);
  expect(
    (
      await instance.app.inject({
        method: 'PUT',
        url: '/api/v1/skins/equipped',
        headers: headers(),
        payload: { versionId: version.id },
      })
    ).statusCode,
  ).toBe(404);
  expect(
    await harness.database.db
      .selectFrom('colony_skin_versions')
      .select('id')
      .where('skin_id', '=', first.skinId)
      .execute(),
  ).toHaveLength(2);
  expect(
    (
      await instance.app.inject({
        url: `/api/v1/skins/versions/${version.id}/texture`,
        headers: headers(),
      })
    ).statusCode,
  ).toBe(200);
});

it('shows only owned presets and copies them without changing their shared artwork', async () => {
  const preset = await harness.database.db
    .selectFrom('colony_skins')
    .select(['id', 'entitlement'])
    .where('kind', '=', 'preset')
    .executeTakeFirstOrThrow();
  const unowned = await instance.app.inject({
    method: 'POST',
    url: '/api/v1/skins/designs',
    headers: headers(),
    payload: { id: randomUUID(), name: 'Preset copy', sourceSkinId: preset.id },
  });
  expect(unowned.statusCode).toBe(403);
  await harness.database.db
    .insertInto('entitlements')
    .values({ account_id: owner.accountId, entitlement: preset.entitlement, source: 'test' })
    .execute();
  const collection = (
    await instance.app.inject({ url: collectionUrl, headers: headers() })
  ).json() as SkinCollection;
  expect(collection.presets.some((s) => s.skinId === preset.id)).toBe(true);
  const copy = await create('Customized preset', preset.id);
  expect(copy.skinId).not.toBe(preset.id);
  expect(
    await harness.database.db
      .selectFrom('colony_skin_designs')
      .select('skin_id')
      .where('skin_id', '=', preset.id)
      .execute(),
  ).toHaveLength(0);
});

it('canonicalizes migrated PNG drafts on apply and rejects malformed design identifiers', async () => {
  const draft = await create('Legacy PNG');
  await harness.database.db
    .updateTable('colony_skin_designs')
    .set({
      image: Buffer.from(payload.imageBase64, 'base64'),
      material: Buffer.from(payload.materialBase64, 'base64'),
    })
    .where('skin_id', '=', draft.skinId)
    .execute();
  const applied = await instance.app.inject({
    method: 'POST',
    url: `${url(draft.skinId)}/use`,
    headers: headers(),
    payload: { revision: draft.revision },
  });
  expect(applied.statusCode, applied.body).toBe(200);
  const version = applied.json().version;
  for (const sha of [version.textureSha256, version.materialSha256]) {
    const blob = await harness.database.db
      .selectFrom('blobs')
      .select('storage_key')
      .where('sha256', '=', sha)
      .executeTakeFirstOrThrow();
    const stream = await harness.blobs.get(blob.storage_key);
    const chunks: Buffer[] = [];
    if (!stream) throw new Error('Expected snapshot bytes');
    for await (const chunk of stream) chunks.push(Buffer.from(chunk));
    expect((await sharp(Buffer.concat(chunks)).metadata()).format).toBe('webp');
  }
  expect(
    (await instance.app.inject({ method: 'DELETE', url: url('a'.repeat(36)), headers: headers() }))
      .statusCode,
  ).toBe(404);
});
