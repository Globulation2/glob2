import { randomUUID } from 'node:crypto';
import { afterAll, beforeAll, expect, it } from 'vitest';
import { simVersionKey, type BuildingDraft, type BuildingFamily } from '@glob2/protocol';
import { readBuildingArchive, buildingAssetHash } from '@glob2/protocol/node';
import { createHarness, SIM, type Harness, type Instance } from './support.ts';
import { registeredPlayer, type Player } from './playSupport.ts';
let h: Harness, i: Instance, a: Player, b: Player;
const baseHash = 'ab'.repeat(32);
const catalog = { snapshot: '{}', hash: buildingAssetHash(Buffer.from('{}')) };
beforeAll(async () => {
  h = await createHarness();
  i = await h.start({ instance: { auth: { providers: [], local: { enabled: true } } } });
  a = await registeredPlayer(i, 'BuildingPublisher');
  b = await registeredPlayer(i, 'BuildingReader');
  await h.database.db
    .insertInto('engine_agents')
    .values({
      id: 'building-validator',
      sim_version: simVersionKey(SIM),
      kinds: ['validate-buildings'],
      build: 'test',
      building_catalog_hash: baseHash,
    })
    .execute();
});
afterAll(async () => {
  a?.client.close();
  b?.client.close();
  await h?.close();
});
const headers = () => ({ authorization: `Bearer ${a.accessToken}` });
async function publish(visibility = 'public') {
  const draft = (
    await i.app.inject({ method: 'POST', url: '/api/v1/building-drafts', headers: headers() })
  ).json<BuildingDraft>();
  const response = await i.app.inject({
    method: 'POST',
    url: `/api/v1/building-drafts/${draft.id}/publish`,
    headers: headers(),
    payload: { revision: draft.revision, description: 'Portable family', visibility },
  });
  expect(response.statusCode, response.body).toBe(202);
  return { draft, family: response.json<BuildingFamily>() };
}
async function validate(family: BuildingFamily, valid = true) {
  const v = family.releases[0]!;
  const row = await h.database.db
    .selectFrom('building_releases')
    .selectAll()
    .where('id', '=', v.id)
    .executeTakeFirstOrThrow();
  await h.database.db
    .updateTable('engine_jobs')
    .set({
      status: 'succeeded',
      result: JSON.stringify(
        valid
          ? { valid: true, archiveHash: v.archiveHash, baseHash, suite: 1, catalog }
          : { valid: false, reason: 'Invalid upgrade relationship' },
      ),
    })
    .where('id', '=', row.job_id)
    .execute();
}
it('keeps pending releases unavailable and freezes the exact saved package', async () => {
  const { draft, family } = await publish();
  const version = family.releases[0]!,
    url = `/api/v1/buildings/${family.id}/releases/${version.id}/archive`;
  expect((await i.app.inject({ url })).statusCode).toBe(404);
  const pkg = structuredClone(draft.package);
  pkg.variants[0]!.properties.hpMax = 400;
  expect(
    (
      await i.app.inject({
        method: 'PUT',
        url: `/api/v1/building-drafts/${draft.id}`,
        headers: headers(),
        payload: { revision: draft.revision, name: 'Edited later', package: pkg },
      })
    ).statusCode,
  ).toBe(200);
  await validate(family);
  const download = await i.app.inject({ url });
  expect(download.statusCode, download.body).toBe(200);
  expect(buildingAssetHash(download.rawPayload)).toBe(version.archiveHash);
  expect(readBuildingArchive(download.rawPayload).package.variants[0]!.properties.hpMax).toBe(200);
  const fork = await i.app.inject({
    method: 'POST',
    url: `/api/v1/buildings/${family.id}/releases/${version.id}/fork`,
    headers: { authorization: `Bearer ${b.accessToken}` },
    payload: {},
  });
  expect(fork.statusCode, fork.body).toBe(201);
  const forked = (
    await i.app.inject({
      url: '/api/v1/building-drafts/' + fork.json<{ id: string }>().id,
      headers: { authorization: `Bearer ${b.accessToken}` },
    })
  ).json<BuildingDraft>();
  expect(forked.package.namespace).not.toBe(draft.package.namespace);
  expect(forked.package.variants[0]!.key).not.toBe(draft.package.variants[0]!.key);
});
it('enforces private visibility, invalid validation and idempotent social actions', async () => {
  const { family } = await publish('private');
  expect((await i.app.inject({ url: '/api/v1/buildings/' + family.id })).statusCode).toBe(404);
  await validate(family, false);
  const detail = (
    await i.app.inject({ url: '/api/v1/buildings/' + family.id, headers: headers() })
  ).json<BuildingFamily>();
  expect(detail.releases[0]!.status).toBe('invalid');
  expect(
    (
      await i.app.inject({
        url: `/api/v1/buildings/${family.id}/releases/${family.releases[0]!.id}/archive`,
        headers: headers(),
      })
    ).statusCode,
  ).toBe(404);
  const published = await publish();
  await validate(published.family);
  for (let n = 0; n < 2; n++)
    expect(
      (
        await i.app.inject({
          method: 'PUT',
          url: `/api/v1/buildings/${published.family.id}/like`,
          headers: headers(),
          payload: {},
        })
      ).statusCode,
    ).toBe(204);
  const liked = (
    await i.app.inject({ url: '/api/v1/buildings/' + published.family.id, headers: headers() })
  ).json<BuildingFamily>();
  expect(liked.likes).toBe(1);
  expect(liked.liked).toBe(true);
  expect(
    (
      await i.app.inject({
        method: 'POST',
        url: `/api/v1/buildings/${published.family.id}/reports`,
        headers: headers(),
        payload: { reason: 'Review this family' },
      })
    ).statusCode,
  ).toBe(201);
  const blocked = await i.app.inject({
    method: 'PUT',
    url: `/api/v1/buildings/${published.family.id}/moderation`,
    headers: headers(),
    payload: { hidden: true, reason: 'Abuse' },
  });
  expect(blocked.statusCode).toBe(403);
  await h.database.db
    .updateTable('accounts')
    .set({ role: 'moderator' })
    .where('id', '=', a.accountId)
    .execute();
  expect(
    (
      await i.app.inject({
        method: 'PUT',
        url: `/api/v1/buildings/${published.family.id}/moderation`,
        headers: headers(),
        payload: { hidden: true, reason: 'Abuse' },
      })
    ).statusCode,
  ).toBe(204);
  expect((await i.app.inject({ url: '/api/v1/buildings/' + published.family.id })).statusCode).toBe(
    404,
  );
  expect(
    (
      await i.app.inject({
        url: `/api/v1/buildings/${published.family.id}/releases/${published.family.releases[0]!.id}/archive`,
        headers: headers(),
      })
    ).statusCode,
  ).toBe(404);
  const reports = (
    await i.app.inject({ url: '/api/v1/building-reports', headers: headers() })
  ).json<{ items: { id: string }[] }>();
  expect(reports.items).toHaveLength(1);
  expect(
    (
      await i.app.inject({
        method: 'PUT',
        url: `/api/v1/building-reports/${reports.items[0]!.id}`,
        headers: headers(),
        payload: { resolved: true },
      })
    ).statusCode,
  ).toBe(204);
  const audit = await h.database.db
    .selectFrom('admin_audit_log')
    .select(['actor_account_id', 'action', 'target_id', 'details'])
    .where('target_type', '=', 'building')
    .where('target_id', '=', published.family.id)
    .orderBy('created_at')
    .execute();
  expect(audit.map((entry) => entry.action)).toEqual(['building.hide', 'building.report.resolve']);
  expect(audit.every((entry) => entry.actor_account_id === a.accountId)).toBe(true);
  expect(audit[0]!.details).toEqual({ reason: 'Abuse' });
});
it('retries failed validation without creating or rewriting an immutable release', async () => {
  const { draft, family } = await publish();
  const version = family.releases[0]!;
  const old = await h.database.db
    .selectFrom('building_releases')
    .select('job_id')
    .where('id', '=', version.id)
    .executeTakeFirstOrThrow();
  await h.database.db
    .updateTable('engine_jobs')
    .set({
      status: 'failed',
      error: JSON.stringify({ code: 'internal', message: 'Engine unavailable' }),
    })
    .where('id', '=', old.job_id)
    .execute();
  const response = await i.app.inject({
    method: 'POST',
    url: `/api/v1/building-drafts/${draft.id}/publish`,
    headers: headers(),
    payload: { revision: draft.revision, description: 'Retry', visibility: 'public' },
  });
  expect(response.statusCode, response.body).toBe(202);
  const retried = response.json<BuildingFamily>();
  expect(retried.releases).toHaveLength(1);
  expect(retried.releases[0]).toMatchObject({
    id: version.id,
    archiveHash: version.archiveHash,
    status: 'pending',
  });
  const now = await h.database.db
    .selectFrom('building_releases')
    .select('job_id')
    .where('id', '=', version.id)
    .executeTakeFirstOrThrow();
  expect(now.job_id).not.toBe(old.job_id);
});
it('lets only authors edit and withdraw listings without a draft or validator', async () => {
  const { draft, family } = await publish();
  await validate(family);
  await i.app.inject({
    method: 'DELETE',
    url: `/api/v1/building-drafts/${draft.id}`,
    headers: headers(),
  });
  await h.database.db
    .updateTable('engine_agents')
    .set({ last_seen_at: new Date(0) })
    .execute();
  const url = '/api/v1/buildings/' + family.id;
  const otherHeaders = { authorization: `Bearer ${b.accessToken}` };
  for (const method of ['PATCH', 'DELETE'] as const)
    expect(
      (
        await i.app.inject({
          method,
          url,
          headers: otherHeaders,
          ...(method === 'PATCH' ? { payload: { visibility: 'private' } } : {}),
        })
      ).statusCode,
    ).toBe(404);
  const edited = await i.app.inject({
    method: 'PATCH',
    url,
    headers: headers(),
    payload: { name: ' Renamed family ', description: 'Withdrawn', visibility: 'private' },
  });
  expect(edited.statusCode, edited.body).toBe(200);
  expect(edited.json<BuildingFamily>()).toMatchObject({
    name: 'Renamed family',
    description: 'Withdrawn',
    visibility: 'private',
    hidden: false,
    releases: family.releases.map((v) => ({ id: v.id, archiveHash: v.archiveHash })),
  });
  expect((await i.app.inject({ url })).statusCode).toBe(404);
  expect(
    (
      await i.app.inject({
        method: 'PATCH',
        url,
        headers: headers(),
        payload: { hidden: false },
      })
    ).statusCode,
  ).toBe(400);
  expect((await i.app.inject({ method: 'DELETE', url, headers: headers() })).statusCode).toBe(204);
  expect((await i.app.inject({ url, headers: headers() })).statusCode).toBe(404);
  expect(
    await h.database.db
      .selectFrom('building_releases')
      .select('id')
      .where('family_id', '=', family.id)
      .execute(),
  ).toHaveLength(0);
  const audit = await h.database.db
    .selectFrom('admin_audit_log')
    .select('action')
    .where('target_type', '=', 'building')
    .where('target_id', '=', family.id)
    .orderBy('created_at')
    .execute();
  expect(audit.map((entry) => entry.action)).toEqual(['building.update', 'building.delete']);
  await h.database.db.updateTable('engine_agents').set({ last_seen_at: new Date() }).execute();
});
it('requires a fresh validator and rejects namespace impersonation or stale drafts', async () => {
  const { draft, family } = await publish();
  const otherHeaders = { authorization: `Bearer ${b.accessToken}` };
  const other = (
    await i.app.inject({ method: 'POST', url: '/api/v1/building-drafts', headers: otherHeaders })
  ).json<BuildingDraft>();
  const exported = await i.app.inject({
    url: `/api/v1/building-drafts/${draft.id}/archive`,
    headers: headers(),
  });
  const imported = await i.app.inject({
    method: 'PUT',
    url: `/api/v1/building-drafts/${other.id}/archive?revision=${other.revision}`,
    headers: { ...otherHeaders, 'content-type': 'application/octet-stream' },
    payload: exported.rawPayload,
  });
  expect(imported.statusCode, imported.body).toBe(200);
  expect(
    (
      await i.app.inject({
        method: 'POST',
        url: `/api/v1/building-drafts/${other.id}/publish`,
        headers: otherHeaders,
        payload: {
          revision: imported.json<BuildingDraft>().revision,
          description: 'Another author cannot claim this namespace',
          visibility: 'public',
        },
      })
    ).statusCode,
  ).toBe(409);
  const response = await i.app.inject({
    method: 'POST',
    url: `/api/v1/building-drafts/${draft.id}/publish`,
    headers: headers(),
    payload: { revision: randomUUID(), description: '', visibility: 'public' },
  });
  expect(response.statusCode).toBe(409);
  await h.database.db
    .updateTable('engine_agents')
    .set({ last_seen_at: new Date(0) })
    .execute();
  expect(
    (
      await i.app.inject({
        method: 'POST',
        url: `/api/v1/building-drafts/${draft.id}/publish`,
        headers: headers(),
        payload: { revision: draft.revision, description: '', visibility: 'public' },
      })
    ).statusCode,
  ).toBe(409);
  expect(family.releases).toHaveLength(1);
});
