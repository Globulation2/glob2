import { afterAll, beforeAll, expect, it } from 'vitest';
import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import { putContent } from '@glob2/core';
import { simVersionKey, type SetPackage, type SetDraft, type SetInfo } from '@glob2/protocol';
import { applySetJobResult, findStaleEngineJobs, sweepStaleEngineJobs } from '@glob2/play';
import { registeredPlayer, guestPlayer, type Player } from './playSupport.ts';
import { createHarness, type Harness, type Instance } from './support.ts';
let harness: Harness, instance: Instance, owner: Player, other: Player, guest: Player;
const sim = { versionMinor: 144, netProtocol: 49, dataHash: 'ab'.repeat(32) };
beforeAll(async () => {
  harness = await createHarness();
  instance = await harness.start({
    origin: 'http://sets.test',
    instance: { auth: { providers: [], local: { enabled: true } } },
  });
  owner = await registeredPlayer(instance, 'SetAuthor');
  other = await registeredPlayer(instance, 'SetReader');
  guest = await guestPlayer(instance);
});
afterAll(async () => {
  owner?.client.close();
  other?.client.close();
  guest?.client.close();
  await harness?.close();
});
function pack(): SetPackage {
  const setId = randomUUID(),
    versionId = randomUUID(),
    key = 's' + setId.replaceAll('-', '') + versionId.replaceAll('-', '') + ':grass';
  return {
    schemaVersion: 1,
    setId,
    versionId,
    title: 'Moss theme',
    description: 'A coordinated theme',
    tags: ['forest'],
    license: 'CC-BY-4.0',
    credits: [{ author: 'Artist', license: 'CC-BY-4.0' }],
    terrains: [{ key, name: 'Moss', base: 'grass', appearance: 'grass', properties: {} }],
    resources: [],
    assets: { schemaVersion: 1, sheets: [], terrains: {}, credits: [] },
  };
}
async function call(method: string, path: string, player?: Player, value?: unknown) {
  return fetch(instance.url + path, {
    method,
    headers: {
      ...(player ? { authorization: 'Bearer ' + player.accessToken } : {}),
      ...(value !== undefined ? { 'content-type': 'application/json' } : {}),
    },
    ...(value !== undefined ? { body: JSON.stringify(value) } : {}),
  });
}
async function createSaved(p = pack()) {
  const result = await call('POST', '/api/v1/set-drafts', owner, p);
  expect(result.status).toBe(201);
  const d = (await result.json()) as SetDraft;
  const saved = await call('PUT', '/api/v1/set-drafts/' + d.id, owner, {
    revision: d.revision,
    package: p,
  });
  expect(saved.status).toBe(200);
  return saved.json() as Promise<SetDraft>;
}
async function checked(d: SetDraft) {
  const stored = await putContent(harness.blobs, Buffer.from(JSON.stringify(d.package)));
  const db = harness.database.db;
  await db
    .insertInto('blobs')
    .values({
      sha256: stored.sha256,
      size: stored.size,
      storage_key: stored.key,
      content_type: 'application/json',
      visibility: 'private',
    })
    .onConflict((oc) => oc.column('sha256').doNothing())
    .execute();
  const jobId = randomUUID();
  await db
    .insertInto('engine_jobs')
    .values({
      id: jobId,
      kind: 'validate-set',
      sim_version: simVersionKey(sim),
      payload: JSON.stringify({ blobHash: stored.sha256, suite: 1 }),
      status: 'succeeded',
      result: JSON.stringify({
        hash: stored.sha256,
        suite: 1,
        valid: true,
        minVersionMinor: 144,
        terrainCount: 1,
        resourceCount: 0,
      }),
    })
    .execute();
  await db
    .updateTable('set_drafts')
    .set({
      hash: stored.sha256,
      validation_job_id: jobId,
      sim_version: simVersionKey(sim),
      status: 'pending',
    })
    .where('id', '=', d.id)
    .execute();
  await db.transaction().execute((trx) => applySetJobResult(trx, jobId));
  return stored;
}
it('protects drafts, rejects stale saves, and requires current validation to publish', async () => {
  expect((await call('POST', '/api/v1/set-drafts', guest, pack())).status).toBe(403);
  const d = await createSaved();
  expect((await call('GET', '/api/v1/set-drafts/' + d.id, other)).status).toBe(404);
  expect((await call('GET', '/api/v1/sets/' + d.package.setId)).status).toBe(404);
  expect(
    (await call('PUT', '/api/v1/set-drafts/' + d.id, owner, { revision: 0, package: d.package }))
      .status,
  ).toBe(409);
  const publish = { revision: d.revision, label: '1.0', notes: '', visibility: 'public' };
  expect((await call('POST', `/api/v1/set-drafts/${d.id}/publish`, owner, publish)).status).toBe(
    409,
  );
  await checked(d);
  const edited = await call('PUT', '/api/v1/set-drafts/' + d.id, owner, {
    revision: d.revision,
    package: { ...d.package, title: 'Changed' },
  });
  expect(edited.status).toBe(200);
  expect(
    (
      await call('POST', `/api/v1/set-drafts/${d.id}/publish`, owner, {
        ...publish,
        revision: d.revision + 1,
      })
    ).status,
  ).toBe(409);
});
it('publishes exact immutable bytes, likes once, pages, and withdraws future downloads', async () => {
  const d = await createSaved();
  const drafts = (await (await call('GET', '/api/v1/set-drafts', owner)).json()) as {
    items: { id: string; title: string; package?: unknown }[];
  };
  const summary = drafts.items.find((item) => item.id === d.id);
  expect(summary?.title).toBe(d.package.title);
  expect(summary).not.toHaveProperty('package');
  const stored = await checked(d),
    publish = { revision: d.revision, label: '1.0', notes: 'Original', visibility: 'public' };
  for (let i = 0; i < 2; i++)
    expect((await call('POST', `/api/v1/set-drafts/${d.id}/publish`, owner, publish)).status).toBe(
      200,
    );
  const file = `/api/v1/sets/${d.package.setId}/versions/${d.id}/file`;
  expect(await (await call('GET', file)).text()).toBe(JSON.stringify(d.package));
  for (let i = 0; i < 2; i++)
    expect((await call('PUT', `/api/v1/sets/${d.package.setId}/like`, other)).status).toBe(204);
  const info = (await (await call('GET', '/api/v1/sets/' + d.package.setId)).json()) as SetInfo;
  expect(info.likes).toBe(1);
  expect(info.versions[0]?.credits).toEqual(d.package.credits);
  const listing = (await (await call('GET', '/api/v1/sets')).json()) as { items: SetInfo[] };
  expect(listing.items.find((item) => item.id === info.id)?.versions[0]?.credits).toEqual([]);
  expect(info.versions[0]?.hash).toBe(stored.sha256);
  expect(
    (
      await call('PUT', '/api/v1/set-drafts/' + d.id, owner, {
        revision: d.revision,
        package: d.package,
      })
    ).status,
  ).toBe(409);
  const second = await createSaved();
  await checked(second);
  expect(
    (await call('POST', `/api/v1/set-drafts/${second.id}/publish`, owner, publish)).status,
  ).toBe(200);
  const page = (await (
    await call('GET', '/api/v1/sets?limit=1&kind=terrain&tags=forest&sort=likes')
  ).json()) as { items: SetInfo[]; nextCursor: string };
  expect(page.items).toHaveLength(1);
  expect(page.nextCursor).toBeTruthy();
  const next = (await (
    await call(
      'GET',
      '/api/v1/sets?limit=1&kind=terrain&tags=forest&sort=likes&cursor=' + page.nextCursor,
    )
  ).json()) as { items: SetInfo[] };
  expect(next.items).toHaveLength(1);
  expect(next.items[0]?.id).not.toBe(page.items[0]?.id);
  expect(
    (
      await call('PATCH', '/api/v1/sets/' + d.package.setId, owner, {
        title: d.package.title,
        description: '',
        tags: [],
        visibility: 'private',
      })
    ).status,
  ).toBe(200);
  expect((await call('GET', file)).status).toBe(404);
  expect((await call('GET', file, owner)).status).toBe(200);
});
it('ignores an obsolete job after a newer revision is saved', async () => {
  const d = await createSaved();
  await checked(d);
  const db = harness.database.db,
    old = await db
      .selectFrom('set_drafts')
      .select('validation_job_id')
      .where('id', '=', d.id)
      .executeTakeFirstOrThrow();
  await call('PUT', '/api/v1/set-drafts/' + d.id, owner, {
    revision: d.revision,
    package: { ...d.package, description: 'New revision' },
  });
  await db.transaction().execute((trx) => applySetJobResult(trx, old.validation_job_id!));
  const updated = (await (
    await call('GET', '/api/v1/set-drafts/' + d.id, owner)
  ).json()) as SetDraft;
  expect(updated.validation).toBeNull();
  expect(updated.revision).toBe(d.revision + 1);
});

it('keeps drafts when no isolated validator is available and coalesces concurrent checks', async () => {
  const d = await createSaved();
  const path = `/api/v1/set-drafts/${d.id}/validate`;
  expect((await call('POST', path, owner, { revision: d.revision })).status).toBe(503);
  expect((await call('GET', `/api/v1/set-drafts/${d.id}`, owner)).status).toBe(200);
  const agentId = randomUUID();
  await harness.database.db
    .insertInto('engine_agents')
    .values({
      id: agentId,
      sim_version: simVersionKey(sim),
      kinds: ['validate-set'],
      build: 'fixture',
      started_at: new Date(),
      last_seen_at: new Date(),
    })
    .execute();
  try {
    const responses = await Promise.all([
      call('POST', path, owner, { revision: d.revision }),
      call('POST', path, owner, { revision: d.revision }),
    ]);
    for (const response of responses) expect(response.status).toBe(200);
    const row = await harness.database.db
      .selectFrom('set_drafts')
      .selectAll()
      .where('id', '=', d.id)
      .executeTakeFirstOrThrow();
    expect(row.status).toBe('pending');
    const jobs = await harness.database.db
      .selectFrom('engine_jobs')
      .select('id')
      .where('kind', '=', 'validate-set')
      .where('payload', '@>', JSON.stringify({ blobHash: row.hash }))
      .execute();
    expect(jobs).toHaveLength(1);
  } finally {
    await harness.database.db.deleteFrom('engine_agents').where('id', '=', agentId).execute();
  }
});

it('pages every owned draft, including equal timestamps and more than 100 across sets', async () => {
  const first = await createSaved(),
    second = await createSaved();
  const db = harness.database.db;
  const timestamp = new Date('2026-01-01T00:00:00Z');
  const ids = [first.id, second.id];
  try {
    const drafts = Array.from({ length: 99 }, () => {
      const id = randomUUID();
      ids.push(id);
      return {
        id,
        set_id: first.package.setId,
        document: JSON.stringify({ ...first.package, versionId: id }),
        updated_at: timestamp,
        hash: null,
        validation_job_id: null,
        sim_version: null,
        report: null,
        status: null,
        error: null,
        published_version_id: null,
      };
    });
    await db.insertInto('set_drafts').values(drafts).execute();
    await db
      .updateTable('set_drafts')
      .set({ updated_at: sql<Date>`'2026-01-01T00:00:00.123456Z'::timestamptz` })
      .where('id', 'in', ids)
      .execute();
    const found: string[] = [];
    let cursor: string | undefined;
    do {
      const response = await call(
        'GET',
        '/api/v1/set-drafts?limit=20' + (cursor ? '&cursor=' + cursor : ''),
        owner,
      );
      expect(response.status).toBe(200);
      const page = (await response.json()) as { items: { id: string }[]; nextCursor?: string };
      found.push(...page.items.map((d) => d.id));
      cursor = page.nextCursor;
    } while (cursor);
    expect(new Set(found).size).toBe(found.length);
    for (const id of ids) expect(found).toContain(id);
    const filtered = (await (
      await call('GET', '/api/v1/set-drafts?setId=' + second.package.setId, owner)
    ).json()) as { items: { id: string }[] };
    expect(filtered.items.map((d) => d.id)).toEqual([second.id]);
    expect((await call('GET', '/api/v1/set-drafts?cursor=invalid', owner)).status).toBe(400);
    expect((await call('GET', '/api/v1/set-drafts?limit=101', owner)).status).toBe(400);
  } finally {
    await db
      .deleteFrom('asset_sets')
      .where('id', 'in', [first.package.setId, second.package.setId])
      .execute();
  }
});

it('fails an unserved set check when a live same-version agent loses that capability, then retries', async () => {
  const d = await createSaved(),
    agentId = randomUUID(),
    db = harness.database.db;
  const path = `/api/v1/set-drafts/${d.id}/validate`;
  await db
    .insertInto('engine_agents')
    .values({
      id: agentId,
      sim_version: simVersionKey(sim),
      kinds: ['validate-set'],
      build: 'fixture',
      started_at: new Date(),
      last_seen_at: new Date(),
    })
    .execute();
  try {
    expect((await call('POST', path, owner, { revision: d.revision })).status).toBe(200);
    const pending = await db
      .selectFrom('set_drafts')
      .selectAll()
      .where('id', '=', d.id)
      .executeTakeFirstOrThrow();
    await sql`UPDATE engine_jobs SET created_at=now()-interval '7 hours' WHERE id=${pending.validation_job_id}`.execute(
      db,
    );
    expect((await findStaleEngineJobs(db)).some((j) => j.jobId === pending.validation_job_id)).toBe(
      false,
    );
    await db
      .updateTable('engine_agents')
      .set({ kinds: ['validate-map'] })
      .where('id', '=', agentId)
      .execute();
    expect(await findStaleEngineJobs(db)).toContainEqual({
      jobId: pending.validation_job_id,
      kind: 'validate-set',
      reason: 'unserved',
    });
    await sweepStaleEngineJobs(db);
    expect(
      (
        await db
          .selectFrom('set_drafts')
          .select('status')
          .where('id', '=', d.id)
          .executeTakeFirstOrThrow()
      ).status,
    ).toBe('error');
    await db
      .updateTable('engine_agents')
      .set({ kinds: ['validate-set'] })
      .where('id', '=', agentId)
      .execute();
    expect((await call('POST', path, owner, { revision: d.revision })).status).toBe(200);
    const retried = await db
      .selectFrom('set_drafts')
      .selectAll()
      .where('id', '=', d.id)
      .executeTakeFirstOrThrow();
    expect(retried.status).toBe('pending');
    expect(retried.validation_job_id).not.toBe(pending.validation_job_id);
  } finally {
    await db.deleteFrom('engine_agents').where('id', '=', agentId).execute();
  }
});
