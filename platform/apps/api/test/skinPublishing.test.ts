import { createHash } from 'node:crypto';
import sharp from 'sharp';
import { afterAll, beforeAll, expect, it } from 'vitest';
import { createHarness, type Harness, type Instance } from './support.ts';
import { registeredPlayer, type Player } from './playSupport.ts';
let harness: Harness;
let instance: Instance;
let player: Player;
beforeAll(async () => {
  harness = await createHarness();
  instance = await harness.start({
    instance: { auth: { providers: [], local: { enabled: true } } },
  });
  player = await registeredPlayer(instance, 'Painter');
});
afterAll(async () => {
  player?.client.close();
  await harness?.close();
});
it('publishes canonical paint, retains old versions, equips and serves verified content', async () => {
  const app = instance.app;
  const headers = { authorization: `Bearer ${player.accessToken}` };
  const png = await sharp({
    create: {
      width: 256,
      height: 256,
      channels: 4,
      background: { r: 255, g: 0, b: 0, alpha: 0.5 },
    },
  })
    .png()
    .toBuffer();
  const payload = {
    name: 'My colony',
    imageBase64: png.toString('base64'),
    buildingColor: 0x123456,
  };
  expect(
    (await app.inject({ method: 'POST', url: '/api/v1/skins/publish', headers, payload }))
      .statusCode,
  ).toBe(403);
  await harness.database.db
    .insertInto('entitlements')
    .values({ account_id: player.accountId, entitlement: 'skins:designer', source: 'test' })
    .execute();
  const response = await app.inject({
    method: 'POST',
    url: '/api/v1/skins/publish',
    headers,
    payload,
  });
  expect(response.statusCode, response.body).toBe(200);
  const version = response.json();
  const repeated = await app.inject({
    method: 'POST',
    url: '/api/v1/skins/publish',
    headers,
    payload: { ...payload, skinId: version.skinId },
  });
  expect(repeated.json()).toEqual(version);
  const revised = await app.inject({
    method: 'POST',
    url: '/api/v1/skins/publish',
    headers,
    payload: { ...payload, name: 'Renamed design', skinId: version.skinId, buildingColor: 0 },
  });
  expect(revised.json().id).not.toBe(version.id);
  expect(revised.json().skinId).toBe(version.skinId);
  const original = await harness.database.db
    .selectFrom('colony_skin_versions')
    .selectAll()
    .where('id', '=', version.id)
    .executeTakeFirstOrThrow();
  expect(original.building_color).toBe(payload.buildingColor);
  expect(original.manifest_sha256).toBe(version.manifestSha256);
  expect(
    (
      await harness.database.db
        .selectFrom('colony_skins')
        .select('name')
        .where('id', '=', version.skinId)
        .executeTakeFirstOrThrow()
    ).name,
  ).toBe('Renamed design');
  const equip = await app.inject({
    method: 'PUT',
    url: '/api/v1/skins/equipped',
    headers,
    payload: { versionId: version.id },
  });
  expect(equip.statusCode).toBe(200);
  const list = await app.inject({ method: 'GET', url: '/api/v1/skins', headers });
  expect(list.json().equippedVersionId).toBe(version.id);
  const presets = list.json().items.filter((item: { kind: string }) => item.kind === 'preset');
  expect(presets).toHaveLength(2);
  const preset = presets[0];
  expect(
    (
      await app.inject({
        method: 'PUT',
        url: '/api/v1/skins/equipped',
        headers,
        payload: { versionId: preset.id, buildingColor: 0xabcdef },
      })
    ).statusCode,
  ).toBe(403);
  await harness.database.db
    .insertInto('entitlements')
    .values({
      account_id: player.accountId,
      entitlement: preset.entitlement,
      source: 'test-purchase',
    })
    .execute();
  expect(
    (
      await app.inject({
        method: 'PUT',
        url: '/api/v1/skins/equipped',
        headers,
        payload: { versionId: preset.id, buildingColor: 0xabcdef },
      })
    ).statusCode,
  ).toBe(200);
  expect(
    (
      await harness.database.db
        .selectFrom('colony_skin_equipment')
        .select('building_color')
        .where('account_id', '=', player.accountId)
        .executeTakeFirstOrThrow()
    ).building_color,
  ).toBe(0xabcdef);

  const texture = await app.inject({
    method: 'GET',
    url: `/api/v1/skins/versions/${version.id}/texture`,
  });
  expect(texture.statusCode).toBe(200);
  const metadata = await sharp(texture.rawPayload).metadata();
  expect(metadata.hasAlpha).toBe(false);
  expect(metadata.width).toBe(256);
  expect(
    (
      await app.inject({
        method: 'POST',
        url: '/api/v1/skins/publish',
        headers,
        payload: { ...payload, imageBase64: Buffer.from('<svg/>').toString('base64') },
      })
    ).statusCode,
  ).toBe(400);
  await harness.database.db
    .updateTable('colony_skins')
    .set({ disabled_at: new Date() })
    .where('id', '=', version.skinId)
    .execute();
  expect(
    (await app.inject({ method: 'GET', url: `/api/v1/skins/versions/${version.id}/texture` }))
      .statusCode,
  ).toBe(404);
});

it('publishes paint per swarm mesh, keeping classic manifests compatible', async () => {
  const app = instance.app;
  const headers = { authorization: `Bearer ${player.accessToken}` };
  const png = await sharp({
    create: { width: 256, height: 256, channels: 3, background: { r: 10, g: 200, b: 90 } },
  })
    .png()
    .toBuffer();
  const payload = { name: 'Shapes', imageBase64: png.toString('base64'), buildingColor: 0x445566 };
  await harness.database.db
    .insertInto('entitlements')
    .values({ account_id: player.accountId, entitlement: 'skins:designer', source: 'shapes' })
    .execute();
  const publish = (extra: object) =>
    app.inject({
      method: 'POST',
      url: '/api/v1/skins/publish',
      headers,
      payload: { ...payload, ...extra },
    });
  const classic = (await publish({})).json();
  expect(classic.swarmMesh).toBe('classic');
  // Game clients recompute this manifest: classic omits swarmMesh, as before mesh choice.
  const manifest = (content: object) =>
    createHash('sha256').update(JSON.stringify(content)).digest('hex');
  const base = {
    skinId: classic.skinId,
    textureSha256: classic.textureSha256,
    layout: 'colony-v1',
    buildingColor: 0x445566,
  };
  expect(classic.manifestSha256).toBe(manifest(base));
  const crown = (await publish({ skinId: classic.skinId, swarmMesh: 'crown' })).json();
  expect(crown).toMatchObject({ skinId: classic.skinId, swarmMesh: 'crown' });
  expect(crown.id).not.toBe(classic.id);
  expect(crown.manifestSha256).toBe(manifest({ ...base, swarmMesh: 'crown' }));
  expect((await publish({ skinId: classic.skinId, swarmMesh: 'crown' })).json()).toEqual(crown);
  expect((await publish({ swarmMesh: 'pyramid' })).statusCode).toBe(400);
  const items = (await app.inject({ method: 'GET', url: '/api/v1/skins', headers })).json().items;
  expect(
    items
      .filter((item: { skinId: string }) => item.skinId === classic.skinId)
      .map((item: { swarmMesh: string }) => item.swarmMesh)
      .sort(),
  ).toEqual(['classic', 'crown']);
});
