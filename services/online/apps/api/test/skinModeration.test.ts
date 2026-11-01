import { afterAll, beforeAll, expect, it } from 'vitest';
import { createHarness, type Harness, type Instance } from './support.ts';
import { registeredPlayer, type Player } from './playSupport.ts';
let harness: Harness, instance: Instance;
let player: Player, moderator: Player;
beforeAll(async () => {
  harness = await createHarness();
  instance = await harness.start({
    instance: { auth: { providers: [], local: { enabled: true } } },
  });
  player = await registeredPlayer(instance, 'Reporter');
  moderator = await registeredPlayer(instance, 'Moderator');
  await harness.database.db
    .updateTable('accounts')
    .set({ role: 'moderator' })
    .where('id', '=', moderator.accountId)
    .execute();
});
afterAll(async () => {
  player?.client.close();
  moderator?.client.close();
  await harness?.close();
});
it('reports exact versions, resolves atomically, preserves content and restricts disabled paint review', async () => {
  const app = instance.app,
    db = harness.database.db;
  const headers = { authorization: `Bearer ${player.accessToken}` };
  const adminHeaders = { authorization: `Bearer ${moderator.accessToken}` };
  const catalog = (await app.inject({ url: '/api/v1/skins', headers })).json();
  const version = catalog.items[0];
  const textureUrl = `/api/v1/skins/versions/${version.id}/texture`;
  const originalPaint = (await app.inject({ url: textureUrl })).rawPayload;
  const materialUrl = `/api/v1/skins/versions/${version.id}/material`;
  const originalMaterial = (await app.inject({ url: materialUrl })).rawPayload;
  expect(originalMaterial.length).toBeGreaterThan(0);
  const url = `/api/v1/skins/versions/${version.id}/reports`;
  expect(
    (await app.inject({ method: 'POST', url, payload: { reason: 'Example' } })).statusCode,
  ).toBe(401);
  expect(
    (await app.inject({ method: 'POST', url, headers, payload: { reason: ' ' } })).statusCode,
  ).toBe(400);
  const reports = await Promise.all(
    [0, 1].map(() =>
      app.inject({ method: 'POST', url, headers, payload: { reason: 'Review this paint' } }),
    ),
  );
  expect(reports.map((r) => r.statusCode)).toEqual([200, 200]);
  const report = reports[0]!.json();
  expect(reports[1]!.json().id).toBe(report.id);
  const queue = '/api/v1/admin/skin-reports';
  expect((await app.inject({ url: queue, headers })).statusCode).toBe(403);
  const listed = await app.inject({ url: queue, headers: adminHeaders });
  expect(listed.json().items).toHaveLength(1);
  expect(listed.json().items[0]).toMatchObject({
    id: report.id,
    versionId: version.id,
    skinId: version.skinId,
    reason: 'Review this paint',
  });
  const resolve = `${queue}/${report.id}/resolve`;
  const payload = { resolution: 'disabled', reason: 'Removed after review' };
  expect((await app.inject({ method: 'POST', url: resolve, headers, payload })).statusCode).toBe(
    403,
  );
  const resolutions = await Promise.all(
    [0, 1].map(() => app.inject({ method: 'POST', url: resolve, headers: adminHeaders, payload })),
  );
  expect(resolutions.map((r) => r.statusCode)).toEqual([200, 200]);
  expect((await app.inject({ url: queue, headers: adminHeaders })).json().items).toEqual([]);
  expect(
    (await app.inject({ url: `${queue}?status=closed`, headers: adminHeaders })).json().items[0]
      .resolution,
  ).toBe('disabled');
  expect((await app.inject({ url: textureUrl })).statusCode).toBe(404);
  expect((await app.inject({ url: materialUrl })).statusCode).toBe(404);
  expect(
    (
      await app.inject({
        method: 'PUT',
        url: '/api/v1/skins/equipped',
        headers,
        payload: { versionId: version.id },
      })
    ).statusCode,
  ).toBe(404);
  const review = `/api/v1/admin/skins/versions/${version.id}/texture`;
  expect((await app.inject({ url: review, headers })).statusCode).toBe(403);
  const reviewed = await app.inject({ url: review, headers: adminHeaders });
  expect(reviewed.rawPayload).toEqual(originalPaint);
  expect(reviewed.headers['cache-control']).toBe('private, no-store');
  const materialReview = `/api/v1/admin/skins/versions/${version.id}/material`;
  expect((await app.inject({ url: materialReview, headers })).statusCode).toBe(403);
  const reviewedMaterial = await app.inject({ url: materialReview, headers: adminHeaders });
  expect(reviewedMaterial.statusCode).toBe(200);
  expect(reviewedMaterial.rawPayload).toEqual(originalMaterial);
  expect(reviewedMaterial.headers['cache-control']).toBe('private, no-store');
  expect(
    (
      await app.inject({
        method: 'POST',
        url: resolve,
        headers: adminHeaders,
        payload: { resolution: 'dismissed', reason: 'Different verdict' },
      })
    ).statusCode,
  ).toBe(409);
  const audit = await db
    .selectFrom('admin_audit_log')
    .select('action')
    .where('target_id', 'in', [report.id, version.skinId])
    .execute();
  expect(audit.map((r) => r.action).sort()).toEqual(['skin.disable', 'skin.report.resolve']);
  const restore = `/api/v1/admin/skins/${version.skinId}/moderation`;
  expect(
    (
      await app.inject({
        method: 'POST',
        url: restore,
        headers,
        payload: { disabled: false, reason: 'Restore' },
      })
    ).statusCode,
  ).toBe(403);
  expect(
    (
      await app.inject({
        method: 'POST',
        url: restore,
        headers: adminHeaders,
        payload: { disabled: false, reason: 'Review reversed' },
      })
    ).statusCode,
  ).toBe(200);
  expect((await app.inject({ url: textureUrl })).rawPayload).toEqual(originalPaint);
  expect((await app.inject({ url: `${queue}?cursor=bad`, headers: adminHeaders })).statusCode).toBe(
    400,
  );
});
it('paginates reports with identical timestamps without omissions', async () => {
  const db = harness.database.db;
  const version = await db
    .selectFrom('colony_skin_versions')
    .select('id')
    .executeTakeFirstOrThrow();
  const accounts = await db
    .insertInto('accounts')
    .values(
      Array.from({ length: 51 }, (_, i) => ({
        kind: 'registered' as const,
        display_name: `QueueReporter${i}`,
      })),
    )
    .returning('id')
    .execute();
  await db
    .insertInto('colony_skin_reports')
    .values(
      accounts.map((a) => ({
        version_id: version.id,
        reporter_account_id: a.id,
        reason: 'Pagination fixture',
        created_at: new Date('2026-01-01T00:00:00Z'),
      })),
    )
    .execute();
  const headers = { authorization: `Bearer ${moderator.accessToken}` };
  const first = (await instance.app.inject({ url: '/api/v1/admin/skin-reports', headers })).json();
  expect(first.items).toHaveLength(50);
  const second = (
    await instance.app.inject({
      url: `/api/v1/admin/skin-reports?cursor=${first.nextCursor}`,
      headers,
    })
  ).json();
  expect(second.items).toHaveLength(1);
  expect(second.nextCursor).toBeUndefined();
  expect(new Set([...first.items, ...second.items].map((r: { id: string }) => r.id)).size).toBe(51);
});
