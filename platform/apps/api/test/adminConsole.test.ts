import {} from '@glob2/protocol';
import { randomUUID } from 'node:crypto';
import { afterAll, beforeAll, expect, it } from 'vitest';
import { sql } from 'kysely';
import { schemaIssues, AdminReportList, AdminContentList } from '@glob2/protocol';
import {} from '@glob2/protocol';
import { createHarness, type Harness, type Instance } from './support.ts';
let h: Harness, api: Instance;
const sessions: Record<string, string> = {},
  ids: Record<string, string> = {};
beforeAll(async () => {
  h = await createHarness();
  api = await h.start();
  for (const role of ['user', 'moderator', 'admin'] as const) {
    const row = await h.database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: role, role })
      .returning('id')
      .executeTakeFirstOrThrow();
    ids[role] = row.id;
    sessions[role] = await api.app.identity.webSessions.create(row.id);
  }
  for (let i = 0; i < 3; i++)
    await h.database.db
      .insertInto('maps')
      .values({
        id: randomUUID(),
        owner_account_id: ids['user']!,
        title: `Map ${i}`,
        visibility: 'private',
        created_at: new Date('2026-10-01T00:00:00Z'),
      })
      .execute();
  const maps = await h.database.db.selectFrom('maps').select('id').execute();
  for (const map of maps)
    await h.database.db
      .insertInto('map_reports')
      .values({
        map_id: map.id,
        reporter_account_id: ids['user']!,
        reason: 'other',
        created_at: new Date('2026-10-01T00:00:00Z'),
      })
      .execute();
});
afterAll(async () => {
  await h?.close();
});
function call(path: string, role = 'admin', body?: unknown) {
  return fetch(api.url + path, {
    method: body ? 'POST' : 'GET',
    headers: {
      cookie: `glob2_session=${sessions[role]}`,
      origin: api.url,
      ...(body ? { 'content-type': 'application/json' } : {}),
    },
    ...(body ? { body: JSON.stringify(body) } : {}),
  });
}
it('guards reads and validates all library union contracts', async () => {
  for (const path of ['/reports', '/content']) {
    expect((await call('/api/v1/admin' + path, 'user')).status).toBe(403);
    expect((await call('/api/v1/admin' + path, 'moderator')).status).toBe(200);
  }
  const reports = (await (await call('/api/v1/admin/reports')).json()) as AdminReportList;
  expect(schemaIssues(AdminReportList, reports)).toEqual([]);
  expect(Object.keys(reports.counts)).toHaveLength(6);
  const content = (await (await call('/api/v1/admin/content')).json()) as AdminContentList;
  expect(schemaIssues(AdminContentList, content)).toEqual([]);
});
it('paginates equal timestamps without gaps or duplicates', async () => {
  const seen: string[] = [];
  let cursor = '';
  do {
    const page = (await (
      await call('/api/v1/admin/reports?limit=1&cursor=' + cursor)
    ).json()) as AdminReportList;
    seen.push(...page.items.map((r: { id: string }) => r.id));
    cursor = page.nextCursor ?? '';
  } while (cursor);
  expect(seen).toHaveLength(3);
  expect(new Set(seen).size).toBe(3);
  expect((await call('/api/v1/admin/reports?cursor=bad')).status).toBe(400);
});
it('resolves a report and hides its content atomically under contention', async () => {
  const page = (await (await call('/api/v1/admin/reports')).json()) as AdminReportList,
    report = page.items[0]!;
  const results = await Promise.all(
    [1, 2].map(() =>
      call(`/api/v1/admin/reports/maps/${report.id}/resolve`, 'moderator', {
        resolution: 'resolved',
        reason: 'Reviewed',
        hide: true,
      }),
    ),
  );
  expect(results.map((r) => r.status).sort()).toEqual([204, 409]);
  expect(
    (
      await h.database.db
        .selectFrom('maps')
        .select('hidden')
        .where('id', '=', report.contentId)
        .executeTakeFirstOrThrow()
    ).hidden,
  ).toBe(true);
  const audit = (
    await sql<{
      n: number;
    }>`SELECT count(*)::int AS n FROM admin_audit_log WHERE action='report.resolve' AND target_id=${report.id}`.execute(
      h.database.db,
    )
  ).rows[0];
  expect(audit?.n).toBe(1);
  const hidden = (await (
    await call('/api/v1/admin/content?hidden=true')
  ).json()) as AdminContentList;
  expect(hidden.items).toHaveLength(1);
  expect(
    (
      await call(`/api/v1/admin/content/maps/${report.contentId}/moderation`, 'moderator', {
        hidden: false,
        reason: 'Restored',
      })
    ).status,
  ).toBe(204);
  const closed = (await (
    await call('/api/v1/admin/reports?status=resolved')
  ).json()) as AdminReportList;
  expect(closed.items[0]!.resolution).toBe('Reviewed');
});

it('supports inspect, hide, resolve, revisit and restore for every library', async () => {
  const db = h.database.db,
    owner = ids['user']!;
  const ai = (
    await db
      .insertInto('ais')
      .values({ name: 'Reviewed AI', owner_account_id: owner })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  await db
    .insertInto('ai_reports')
    .values({ ai_id: ai, reporter_account_id: owner, reason: 'other', details: 'AI report' })
    .execute();
  const building = (
    await db
      .insertInto('building_families')
      .values({ namespace: randomUUID(), name: 'Reviewed building', owner_account_id: owner })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  await db
    .insertInto('building_reports')
    .values({ family_id: building, reporter_account_id: owner, reason: 'Building report' })
    .execute();
  const set = randomUUID();
  await db
    .insertInto('asset_sets')
    .values({ id: set, title: 'Reviewed set', owner_account_id: owner })
    .execute();
  await db
    .insertInto('set_reports')
    .values({ set_id: set, reporter_account_id: owner, reason: 'other', details: 'Set report' })
    .execute();
  const music = randomUUID();
  await sql`INSERT INTO music_releases(id,owner_id,metadata) VALUES(${music},${owner},'{"title":"Reviewed music"}'::jsonb)`.execute(
    db,
  );
  await db
    .insertInto('music_reports')
    .values({ release_id: music, account_id: owner, reason: 'Music report' })
    .execute();
  const skin = (
    await db
      .insertInto('colony_skins')
      .values({
        kind: 'custom',
        owner_account_id: owner,
        name: 'Reviewed skin',
        entitlement: 'skins:designer',
      })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  const hash = 'cd'.repeat(32);
  await db
    .insertInto('blobs')
    .values({ sha256: hash, size: 1, content_type: 'image/png', storage_key: hash })
    .onConflict((oc) => oc.doNothing())
    .execute();
  const version = (
    await db
      .insertInto('colony_skin_versions')
      .values({
        skin_id: skin,
        texture_sha256: hash,
        material_sha256: hash,
        manifest_sha256: hash,
        layout: 'colony-v2',
        building_color: 0,
      })
      .returning('id')
      .executeTakeFirstOrThrow()
  ).id;
  await db
    .insertInto('colony_skin_reports')
    .values({ version_id: version, reporter_account_id: owner, reason: 'Skin report' })
    .execute();
  for (const library of ['maps', 'ais', 'buildings', 'sets', 'skins', 'music']) {
    const report = (
      (await (await call('/api/v1/admin/reports?library=' + library)).json()) as AdminReportList
    ).items[0]!;
    expect(report.library).toBe(library);
    expect(report.reporterName).toBe('user');
    expect(
      (
        await call('/api/v1/admin/reports/' + library + '/' + report.id + '/resolve', 'moderator', {
          resolution: 'resolved',
          reason: 'Reviewed content',
          hide: true,
        })
      ).status,
    ).toBe(204);
    const history = (await (
      await call('/api/v1/admin/reports?library=' + library + '&status=resolved')
    ).json()) as AdminReportList;
    expect(history.items.find((r) => r.id === report.id)).toMatchObject({
      hidden: true,
      resolution: 'Reviewed content',
    });
    expect(
      (
        await call(
          '/api/v1/admin/content/' + library + '/' + report.contentId + '/moderation',
          'moderator',
          { hidden: false, reason: 'Restored after review' },
        )
      ).status,
    ).toBe(204);
  }
});
