import { beforeAll, afterAll, it, expect } from 'vitest';
import { randomUUID } from 'node:crypto';
import sharp from 'sharp';
import { buildingAssetHash } from '@glob2/protocol/node';
import { BuildingAiStudio } from '@glob2/building-studio';
import type { BuildingDraft } from '@glob2/protocol';
import { registeredPlayer, type Player } from './playSupport.ts';
import { createHarness, type Harness, type Instance } from './support.ts';
let harness: Harness, instance: Instance, owner: Player, other: Player, studio: BuildingAiStudio;
beforeAll(async () => {
  harness = await createHarness();
  studio = new BuildingAiStudio(harness.database.db);
  instance = await harness.start({
    instance: {
      auth: { providers: [], local: { enabled: true } },
      buildingStudio: {
        enabled: true,
        salesEnabled: false,
        textModel: 'test',
        imageModel: 'test',
        pipelineVersion: 'building-v1',
        maxOutputTokens: 16000,
        providerCallsPerDay: 100,
        timeoutSeconds: 1800,
      },
    },
  });
  owner = await registeredPlayer(instance, 'BuildingOwner');
  other = await registeredPlayer(instance, 'BuildingOther');
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
const root = '/api/v1/ai-building-studio';
it('creates an idempotent private project on an ordinary building draft', async () => {
  const id = randomUUID(),
    body = { id, title: 'Mushroom hospital' };
  expect((await call('POST', root + '/threads', owner, body)).status).toBe(200);
  expect((await call('POST', root + '/threads', owner, body)).status).toBe(200);
  expect((await call('GET', root + '/threads/' + id, other)).status).toBe(404);
  const t = (await (await call('GET', root + '/threads/' + id, owner)).json()) as {
    draftId: string;
  };
  const d = (await (
    await call('GET', '/api/v1/building-drafts/' + t.draftId, owner)
  ).json()) as BuildingDraft;
  expect(d.name).toBe('Mushroom hospital');
  expect(d.revision).toMatch(/^[a-f0-9-]{36}$/);
  const submission = {
    id: randomUUID(),
    text: 'Make a hospital',
    expectedRevision: d.revision,
    references: [],
  };
  expect((await call('POST', root + '/threads/' + id + '/turns', owner, submission)).status).toBe(
    200,
  );
  expect((await call('POST', root + '/threads/' + id + '/turns', owner, submission)).status).toBe(
    200,
  );
  expect(
    (
      await call('POST', root + '/threads/' + id + '/turns', owner, {
        ...submission,
        text: 'Changed',
      })
    ).status,
  ).toBe(409);
  expect((await call('DELETE', '/api/v1/building-drafts/' + t.draftId, owner)).status).toBe(409);
  expect(
    (
      await call('POST', root + '/threads/' + id + '/turns', owner, {
        ...submission,
        action: 'build',
      })
    ).status,
  ).toBe(400);
  const row = (await studio.claim())!;
  await studio.finish(row, { text: 'Which units should it heal?' });
  expect((await studio.credits.balance(owner.accountId)).balance).toBe(3);
  expect((await call('DELETE', '/api/v1/building-drafts/' + t.draftId, owner)).status).toBe(204);
});
it('normalizes references and checks ownership, stale revisions and project attachment', async () => {
  const id = randomUUID();
  await call('POST', root + '/threads', owner, { id, title: 'Tower' });
  const t = (await (await call('GET', root + '/threads/' + id, owner)).json()) as {
    draftId: string;
  };
  const d = (await (
    await call('GET', '/api/v1/building-drafts/' + t.draftId, owner)
  ).json()) as BuildingDraft;
  const png = await sharp({ create: { width: 32, height: 32, channels: 4, background: '#654321' } })
    .png()
    .toBuffer();
  const upload = await call('POST', root + '/threads/' + id + '/references', owner, png);
  expect(upload.status).toBe(200);
  const reference = (await upload.json()) as { hash: string; url: string };
  expect((await call('GET', reference.url, other)).status).toBe(404);
  expect((await call('GET', reference.url, owner)).headers.get('content-type')).toContain(
    'image/png',
  );
  const turn = {
    id: randomUUID(),
    text: 'Create tower',
    expectedRevision: randomUUID(),
    references: [],
  };
  expect((await call('POST', root + '/threads/' + id + '/turns', owner, turn)).status).toBe(409);
  expect(
    (
      await call('POST', root + '/threads/' + id + '/turns', owner, {
        ...turn,
        expectedRevision: d.revision,
        references: ['a'.repeat(64)],
      })
    ).status,
  ).toBe(400);
  expect(
    (
      await call('POST', root + '/threads', other, {
        id: randomUUID(),
        title: 'Stolen',
        draftId: d.id,
      })
    ).status,
  ).toBe(404);
  expect(
    (await call('POST', root + '/threads/' + id + '/references', owner, Buffer.from('bad'))).status,
  ).toBe(400);
});
it('enforces reference capacity across owned projects without counting other accounts', async () => {
  const fullProject = randomUUID(),
    nextProject = randomUUID();
  await call('POST', root + '/threads', owner, { id: fullProject, title: 'Full references' });
  await call('POST', root + '/threads', owner, { id: nextProject, title: 'New references' });
  // Seed the retained size directly: the test exercises aggregate ownership and
  // locking without allocating a 64 MiB image fixture or depending on compression.
  const hash = 'd'.repeat(64);
  await harness.database.db
    .insertInto('blobs')
    .values({
      sha256: hash,
      size: 64 * 1024 * 1024,
      storage_key: 'reference-quota-test',
      content_type: 'image/png',
      visibility: 'private',
    })
    .execute();
  await harness.database.db
    .insertInto('building_studio_artifacts')
    .values({
      thread_id: fullProject,
      request_id: null,
      stage: 'prepare',
      kind: 'reference',
      label: 'Quota fixture',
      hash,
    })
    .execute();
  const image = await sharp({
    create: { width: 16, height: 16, channels: 4, background: '#432165' },
  })
    .png()
    .toBuffer();
  const denied = await call('POST', root + '/threads/' + nextProject + '/references', owner, image);
  expect(denied.status).toBe(400);
  expect(await denied.text()).toContain('64 MiB');
  const normalized = await sharp(image)
    .rotate()
    .resize({ width: 1024, height: 1024, fit: 'inside', withoutEnlargement: true })
    .png()
    .toBuffer();
  expect(
    await harness.database.db
      .selectFrom('blobs')
      .select('sha256')
      .where('sha256', '=', buildingAssetHash(normalized))
      .executeTakeFirst(),
  ).toBeUndefined();
  expect(
    (await call('POST', root + '/threads/' + fullProject + '/references', other, image)).status,
  ).toBe(404);
  const otherProject = randomUUID();
  await call('POST', root + '/threads', other, { id: otherProject, title: 'Other references' });
  expect(
    (await call('POST', root + '/threads/' + otherProject + '/references', other, image)).status,
  ).toBe(200);
  expect((await call('DELETE', root + '/threads/' + fullProject, owner)).status).toBe(200);
  expect(
    (await call('POST', root + '/threads/' + nextProject + '/references', owner, image)).status,
  ).toBe(200);
});
