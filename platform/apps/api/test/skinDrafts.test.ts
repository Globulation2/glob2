import sharp from 'sharp';
import { afterAll, beforeAll, expect, it } from 'vitest';
import { createHarness, type Harness, type Instance } from './support.ts';
import { registeredPlayer, type Player } from './playSupport.ts';
let harness: Harness;
let instance: Instance;
let owner: Player;
let other: Player;
beforeAll(async () => {
  harness = await createHarness();
  instance = await harness.start({
    instance: { auth: { providers: [], local: { enabled: true } } },
  });
  owner = await registeredPlayer(instance, 'DraftOwner');
  other = await registeredPlayer(instance, 'OtherPainter');
});
afterAll(async () => {
  owner?.client.close();
  other?.client.close();
  await harness?.close();
});
it('keeps drafts private, canonical and independent of paid publishing, and rejects stale writes', async () => {
  const app = instance.app;
  const headers = { authorization: `Bearer ${owner.accessToken}` };
  const otherHeaders = { authorization: `Bearer ${other.accessToken}` };
  const url = '/api/v1/skins/draft';
  expect((await app.inject({ url })).statusCode).toBe(401);
  expect((await app.inject({ url, headers })).json()).toEqual({ draft: null });
  const png = await sharp({
    create: { width: 256, height: 256, channels: 4, background: '#ee224488' },
  })
    .png()
    .toBuffer();
  const payload = {
    revision: null,
    name: ' My draft ',
    buildingColor: 0x123456,
    imageBase64: png.toString('base64'),
  };
  const saves = await Promise.all(
    [0, 1].map(() => app.inject({ method: 'PUT', url, headers, payload })),
  );
  expect(saves.map((r) => r.statusCode).sort()).toEqual([200, 409]);
  const revision = saves.find((r) => r.statusCode === 200)!.json().revision;
  const read = await app.inject({ url, headers });
  expect(read.headers['cache-control']).toBe('private, no-store');
  expect(read.json().draft).toMatchObject({
    revision,
    name: 'My draft',
    buildingColor: 0x123456,
    swarmMesh: 'classic',
  });
  expect(
    (await sharp(Buffer.from(read.json().draft.imageBase64, 'base64')).metadata()).hasAlpha,
  ).toBe(false);
  expect((await app.inject({ url, headers: otherHeaders })).json()).toEqual({ draft: null });
  expect(
    (
      await app.inject({
        method: 'PUT',
        url,
        headers: otherHeaders,
        payload: { ...payload, revision },
      })
    ).statusCode,
  ).toBe(409);
  const update = await app.inject({
    method: 'PUT',
    url,
    headers,
    payload: { ...payload, revision, name: 'Next', swarmMesh: 'skep' },
  });
  expect(update.statusCode, update.body).toBe(200);
  expect(update.json().revision).not.toBe(revision);
  expect(
    (await app.inject({ method: 'PUT', url, headers, payload: { ...payload, revision } }))
      .statusCode,
  ).toBe(409);
  for (const change of [
    { name: ' ' },
    { imageBase64: Buffer.from('<svg/>').toString('base64') },
    { buildingColor: -1 },
    { swarmMesh: 'pyramid' },
  ]) {
    expect(
      (
        await app.inject({
          method: 'PUT',
          url,
          headers,
          payload: { ...payload, revision: update.json().revision, ...change },
        })
      ).statusCode,
    ).toBe(400);
  }
  expect((await app.inject({ url, headers })).json().draft).toMatchObject({
    name: 'Next',
    swarmMesh: 'skep',
  });
  expect(
    await harness.database.db.selectFrom('colony_skin_drafts').selectAll().execute(),
  ).toHaveLength(1);
  expect(
    await harness.database.db
      .selectFrom('colony_skins')
      .select('id')
      .where('owner_account_id', '=', owner.accountId)
      .execute(),
  ).toHaveLength(0);
});

it('retains an owned design in a draft and rejects another account or a disabled design', async () => {
  const db = harness.database.db;
  const skin = await db
    .insertInto('colony_skins')
    .values({
      owner_account_id: owner.accountId,
      name: 'Original',
      kind: 'custom',
      entitlement: 'skins:designer',
    })
    .returning('id')
    .executeTakeFirstOrThrow();
  const headers = { authorization: `Bearer ${owner.accessToken}` };
  const url = '/api/v1/skins/draft';
  const { draft } = (await instance.app.inject({ url, headers })).json();
  const saved = await instance.app.inject({
    method: 'PUT',
    url,
    headers,
    payload: { ...draft, skinId: skin.id },
  });
  expect(saved.statusCode, saved.body).toBe(200);
  expect((await instance.app.inject({ url, headers })).json().draft.skinId).toBe(skin.id);
  const foreign = await instance.app.inject({
    method: 'PUT',
    url,
    headers: { authorization: `Bearer ${other.accessToken}` },
    payload: { ...draft, revision: null, skinId: skin.id },
  });
  expect(foreign.statusCode).toBe(404);
  await db
    .updateTable('colony_skins')
    .set({ disabled_at: new Date() })
    .where('id', '=', skin.id)
    .execute();
  expect(
    (
      await instance.app.inject({
        method: 'PUT',
        url,
        headers,
        payload: { ...draft, revision: saved.json().revision, skinId: skin.id },
      })
    ).statusCode,
  ).toBe(404);
  // A painter can still keep their private canvas as a separate design.
  expect(
    (
      await instance.app.inject({
        method: 'PUT',
        url,
        headers,
        payload: { ...draft, revision: saved.json().revision },
      })
    ).statusCode,
  ).toBe(200);
  expect((await instance.app.inject({ url, headers })).json().draft.skinId).toBeUndefined();
});

it('rejects guests and accounts that become inactive', async () => {
  const guest = await instance.app.inject({
    method: 'POST',
    url: '/api/v1/auth/guest',
    payload: { platform: 'desktop' },
  });
  expect(guest.statusCode).toBe(200);
  const headers = { authorization: `Bearer ${guest.json().tokens.accessToken}` };
  for (const method of ['GET', 'PUT'] as const) {
    expect(
      (await instance.app.inject({ method, url: '/api/v1/skins/draft', headers })).statusCode,
    ).toBe(403);
  }
  await harness.database.db
    .updateTable('accounts')
    .set({ status: 'banned' })
    .where('id', '=', other.accountId)
    .execute();
  for (const method of ['GET', 'PUT'] as const) {
    const response = await instance.app.inject({
      method,
      url: '/api/v1/skins/draft',
      headers: { authorization: `Bearer ${other.accessToken}` },
    });
    expect(response.statusCode).toBe(403);
  }
});
