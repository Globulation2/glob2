import { createHash } from 'node:crypto';
import sharp from 'sharp';
import { afterAll, beforeAll, expect, it } from 'vitest';
import { createHarness, type Harness, type Instance } from './support.ts';
import { registeredPlayer, type Player } from './playSupport.ts';
import { colourAtlas, materialMap } from './skinImages.ts';
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
  const png = await colourAtlas({ r: 255, g: 0, b: 0, alpha: 0.5 });
  const material = await materialMap();
  const payload = {
    name: 'My colony',
    imageBase64: png.toString('base64'),
    materialBase64: material.toString('base64'),
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
  const rotated = await app.inject({
    method: 'POST',
    url: '/api/v1/skins/publish',
    headers,
    payload: { ...payload, skinId: version.skinId, swarmViewAngle: 127 },
  });
  expect(rotated.statusCode, rotated.body).toBe(200);
  expect(rotated.json().id).not.toBe(version.id);
  expect(rotated.json().swarmViewAngle).toBe(127);
  expect(rotated.json().textureSha256).toBe(version.textureSha256);
  expect(rotated.json().manifestSha256).not.toBe(version.manifestSha256);
  const zeroAngle = await app.inject({
    method: 'POST',
    url: '/api/v1/skins/publish',
    headers,
    payload: { ...payload, name: 'Renamed design', skinId: version.skinId, swarmViewAngle: 0 },
  });
  expect(zeroAngle.json().id).toBe(version.id);
  const original = await harness.database.db
    .selectFrom('colony_skin_versions')
    .selectAll()
    .where('id', '=', version.id)
    .executeTakeFirstOrThrow();
  expect(original.building_color).toBe(payload.buildingColor);
  expect(original.manifest_sha256).toBe(version.manifestSha256);
  expect(original.material_sha256).toBe(version.materialSha256);
  expect(version.layout).toBe('colony-v2');
  // The signed manifest names both images, in the order native clients hash it.
  expect(version.manifestSha256).toBe(
    createHash('sha256')
      .update(
        JSON.stringify({
          skinId: version.skinId,
          textureSha256: version.textureSha256,
          materialSha256: version.materialSha256,
          layout: 'colony-v2',
          buildingColor: payload.buildingColor,
        }),
      )
      .digest('hex'),
  );
  // A different material map alone is new content.
  const rematerialed = await app.inject({
    method: 'POST',
    url: '/api/v1/skins/publish',
    headers,
    payload: {
      ...payload,
      name: 'Renamed design',
      skinId: version.skinId,
      materialBase64: (await materialMap({ id: () => 3 })).toString('base64'),
    },
  });
  expect(rematerialed.statusCode, rematerialed.body).toBe(200);
  expect(rematerialed.json()).toMatchObject({
    skinId: version.skinId,
    textureSha256: version.textureSha256,
  });
  expect(rematerialed.json().id).not.toBe(version.id);
  expect(rematerialed.json().materialSha256).not.toBe(version.materialSha256);
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
    url: `/api/v1/skins/versions/${version.id}/texture?sha256=${version.textureSha256}`,
  });
  expect(texture.statusCode).toBe(200);
  const metadata = await sharp(texture.rawPayload).metadata();
  expect(metadata.hasAlpha).toBe(false);
  expect(metadata.width).toBe(512);
  expect(texture.headers['etag']).toBe(`"${version.textureSha256}"`);
  const materialUrl = `/api/v1/skins/versions/${version.id}/material?sha256=${version.materialSha256}`;
  const served = await app.inject({ method: 'GET', url: materialUrl });
  expect(served.statusCode).toBe(200);
  expect(served.headers['content-type']).toBe('image/webp');
  expect(served.headers['etag']).toBe(`"${version.materialSha256}"`);
  expect(served.headers['cache-control']).toBe('public, max-age=300');
  expect(createHash('sha256').update(served.rawPayload).digest('hex')).toBe(version.materialSha256);
  expect(await sharp(served.rawPayload).metadata()).toMatchObject({
    width: 512,
    height: 512,
    channels: 3,
  });
  const decoded = await sharp(served.rawPayload)
    .extractChannel(0)
    .raw()
    .toBuffer({ resolveWithObject: true });
  expect([0, 256, 512 * 300, 512 * 300 + 400].map((i) => decoded.data[i])).toEqual([0, 1, 2, 3]);
  expect(
    (await app.inject({ method: 'GET', url: '/api/v1/skins/versions/not-a-uuid/material' }))
      .statusCode,
  ).toBe(404);
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
  for (const route of ['texture', 'material'])
    expect(
      (await app.inject({ method: 'GET', url: `/api/v1/skins/versions/${version.id}/${route}` }))
        .statusCode,
    ).toBe(404);
});

it('validates and canonicalizes material maps', async () => {
  const app = instance.app;
  const painter = await registeredPlayer(instance, 'MaterialPainter');
  const headers = { authorization: `Bearer ${painter.accessToken}` };
  await harness.database.db
    .insertInto('entitlements')
    .values({ account_id: painter.accountId, entitlement: 'skins:designer', source: 'test' })
    .execute();
  const imageBase64 = (await colourAtlas()).toString('base64');
  const publish = async (material: Buffer, extra: Record<string, unknown> = {}) =>
    app.inject({
      method: 'POST',
      url: '/api/v1/skins/publish',
      headers,
      payload: {
        name: 'Materials',
        imageBase64,
        materialBase64: material.toString('base64'),
        buildingColor: 1,
        ...extra,
      },
    });
  const rejected: [string, Buffer, RegExp][] = [
    ['non-grey', await materialMap({ pixel: { x: 10, y: 10, value: [1, 2, 1] } }), /grey/],
    ['out of range', await materialMap({ pixel: { x: 300, y: 5, value: [4, 4, 4] } }), /ids/],
    [
      'transparent',
      await materialMap({ channels: 4, pixel: { x: 0, y: 0, value: [0, 0, 0, 128] } }),
      /opaque/,
    ],
    ['wrong size', await materialMap({ size: 256 }), /512 by 512/],
    ['not an image', Buffer.from('<svg/>'), /PNG or WebP/],
  ];
  for (const [label, material, message] of rejected) {
    const response = await publish(material);
    expect(response.statusCode, label).toBe(400);
    expect(response.json().message, label).toMatch(message);
  }
  // Grey RGBA, greyscale and lossless WebP inputs re-encode to one canonical greyscale PNG.
  const ids = (x: number, y: number) => (x + y) % 4;
  const accepted = [
    await materialMap({ id: ids, channels: 4 }),
    await materialMap({ id: ids, channels: 1 }),
    await sharp(await materialMap({ id: ids }))
      .webp({ lossless: true })
      .toBuffer(),
  ];
  const hashes = new Set<string>();
  for (const material of accepted) {
    const response = await publish(material);
    expect(response.statusCode, response.body).toBe(200);
    hashes.add(response.json().materialSha256);
  }
  expect(hashes.size).toBe(1);
  const [materialSha256] = [...hashes];
  const blob = await harness.database.db
    .selectFrom('blobs')
    .select(['storage_key', 'visibility', 'owner_account_id', 'content_type'])
    .where('sha256', '=', materialSha256!)
    .executeTakeFirstOrThrow();
  expect(blob).toMatchObject({
    visibility: 'private',
    owner_account_id: painter.accountId,
    content_type: 'image/webp',
  });
  const stored = (await harness.blobs.get(blob.storage_key))!;
  const chunks: Buffer[] = [];
  for await (const chunk of stored) chunks.push(Buffer.from(chunk as Uint8Array));
  const png = Buffer.concat(chunks);
  expect((await sharp(png).metadata()).format).toBe('webp');
  expect((await sharp(png).metadata()).channels).toBe(3);
  const decoded = await sharp(png).extractChannel(0).raw().toBuffer({ resolveWithObject: true });
  expect([0, 1, 2, 3, 512].map((i) => decoded.data[i])).toEqual([0, 1, 2, 3, 1]);
  painter.client.close();
});

it('publishes paint per swarm mesh, leaving swarmMesh out of classic manifests', async () => {
  const app = instance.app;
  const headers = { authorization: `Bearer ${player.accessToken}` };
  const png = await colourAtlas({ r: 10, g: 200, b: 90 });
  const payload = {
    name: 'Shapes',
    imageBase64: png.toString('base64'),
    materialBase64: (await materialMap()).toString('base64'),
    buildingColor: 0x445566,
  };
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
  // Game clients recompute this manifest in this key order; classic omits swarmMesh.
  const manifest = (content: object) =>
    createHash('sha256').update(JSON.stringify(content)).digest('hex');
  const base = {
    skinId: classic.skinId,
    textureSha256: classic.textureSha256,
    materialSha256: classic.materialSha256,
    layout: 'colony-v2',
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

it('exposes pending status immediately and only serves ready enabled sprite bundle assets', async () => {
  const db = harness.database.db,
    revision = '9'.repeat(64);
  await db.insertInto('skin_render_revisions').values({ revision }).execute();
  const headers = { authorization: `Bearer ${player.accessToken}` };
  const response = await instance.app.inject({
    method: 'POST',
    url: '/api/v1/skins/publish',
    headers,
    payload: {
      name: 'Sprites',
      imageBase64: (await colourAtlas()).toString('base64'),
      materialBase64: (await materialMap()).toString('base64'),
      buildingColor: 321,
    },
  });
  expect(response.statusCode, response.body).toBe(200);
  const version = response.json();
  expect(version.softwareStatus).toBe('pending');
  const derivative = await db
    .selectFrom('colony_skin_sprites')
    .selectAll()
    .where('version_id', '=', version.id)
    .where('render_revision', '=', revision)
    .executeTakeFirstOrThrow();
  const { putContent } = await import('@glob2/core');
  const page = await putContent(harness.blobs, Buffer.from('page fixture')),
    manifest = await putContent(harness.blobs, Buffer.from('manifest fixture'));
  for (const blob of [page, manifest])
    await db
      .insertInto('blobs')
      .values({
        sha256: blob.sha256,
        size: blob.size,
        storage_key: blob.key,
        content_type: blob === page ? 'image/webp' : 'application/json',
      })
      .execute();
  const url = `/api/v1/skins/versions/${version.id}/sprites/${manifest.sha256}`;
  expect((await instance.app.inject({ url: url + '/manifest' })).statusCode).toBe(404);
  await db
    .insertInto('colony_skin_sprite_pages')
    .values({ sprites_id: derivative.id, sha256: page.sha256 })
    .execute();
  await db
    .updateTable('colony_skin_sprites')
    .set({ status: 'ready', manifest_sha256: manifest.sha256 })
    .where('id', '=', derivative.id)
    .execute();
  const delivered = await instance.app.inject({ url: url + '/pages/' + page.sha256 });
  expect(delivered.statusCode).toBe(200);
  expect(delivered.headers['content-type']).toContain('image/webp');
  expect(delivered.body).toBe('page fixture');
  expect(
    (await instance.app.inject({ url: url + '/pages/' + version.textureSha256 })).statusCode,
  ).toBe(404);
  expect((await instance.app.inject({ url: url + '/manifest' })).body).toBe('manifest fixture');
  const catalog = (await instance.app.inject({ url: '/api/v1/skins', headers })).json();
  expect(catalog.items.find((s: { id: string }) => s.id === version.id).softwareStatus).toBe(
    'ready',
  );
  await db
    .updateTable('colony_skins')
    .set({ disabled_at: new Date() })
    .where('id', '=', version.skinId)
    .execute();
  expect((await instance.app.inject({ url: url + '/manifest' })).statusCode).toBe(404);
  expect((await instance.app.inject({ url: url + '/pages/' + page.sha256 })).statusCode).toBe(404);
});
