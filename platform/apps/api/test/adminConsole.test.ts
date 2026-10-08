import { AccountActivity } from '@glob2/core';
import { Credits, recordPaymentFact, recordAttemptUsage } from '@glob2/billing';
import { AdminOperationDetail } from '@glob2/protocol';
import { randomUUID } from 'node:crypto';
import { afterAll, beforeAll, expect, it } from 'vitest';
import { sql } from 'kysely';
import { schemaIssues, AdminReportList, AdminContentList } from '@glob2/protocol';
import { AdminAnalytics, AdminFinances, AdminOperations } from '@glob2/protocol';
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
  await sql`UPDATE map_reports SET created_at='2026-10-01T00:00:00.123456Z'::timestamptz`.execute(
    h.database.db,
  );
  await sql`UPDATE maps SET created_at='2026-10-01T00:00:00.123456Z'::timestamptz`.execute(
    h.database.db,
  );
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
  for (const path of ['/reports', '/content', '/audit']) {
    expect((await call('/api/v1/admin' + path, 'user')).status).toBe(403);
    expect((await call('/api/v1/admin' + path, 'moderator')).status).toBe(200);
  }
  const reports = (await (await call('/api/v1/admin/reports')).json()) as AdminReportList;
  expect(schemaIssues(AdminReportList, reports)).toEqual([]);
  expect(Object.keys(reports.counts)).toHaveLength(7);
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

it('restricts analytics, finances and recovery to admins and validates their contracts', async () => {
  for (const [path, schema] of [
    ['analytics', AdminAnalytics],
    ['finances', AdminFinances],
    ['operations', AdminOperations],
  ] as const) {
    expect((await call('/api/v1/admin/' + path, 'moderator')).status).toBe(403);
    const response = await call('/api/v1/admin/' + path);
    const data = await response.json();
    expect(response.status, path + JSON.stringify(data)).toBe(200);
    expect(schemaIssues(schema, data)).toEqual([]);
  }
  expect((await call('/api/v1/admin/analytics?days=1000')).status).toBe(400);
  expect((await call('/api/v1/admin/finances?mode=all')).status).toBe(400);
});
it('excludes admin polling and unsuccessful calls from account activity', async () => {
  await call('/api/v1/admin/content');
  expect(
    await h.database.db
      .selectFrom('account_activity_days')
      .selectAll()
      .where('account_id', '=', ids['admin']!)
      .execute(),
  ).toHaveLength(0);
  await call('/api/v1/accounts/me', 'user');
  await new Promise((resolve) => setTimeout(resolve, 20));
  expect(
    await h.database.db
      .selectFrom('account_activity_days')
      .selectAll()
      .where('account_id', '=', ids['user']!)
      .execute(),
  ).toHaveLength(1);
});

it('reconciles Hive once under contention, audits credit consequences, and exposes only metering', async () => {
  const credits = new Credits(h.database.db),
    account = ids['user']!,
    id = randomUUID();
  await h.database.db
    .insertInto('hive_wallets')
    .values({ account_id: account, balance: 100 })
    .execute();
  await credits.reserve(account, id, 50, {
    version: 'test',
    model: 'test-model',
    input: 1000000,
    cachedInput: 0,
    output: 1000000,
  });
  await credits.dispatch(id);
  await credits.uncertain(id);
  const detail = await (await call('/api/v1/admin/operations/hive/' + id)).json();
  expect(schemaIssues(AdminOperationDetail, detail)).toEqual([]);
  expect(JSON.stringify(detail)).not.toContain('prompt');
  const statuses = await Promise.all(
    [1, 2].map(
      async () =>
        (
          await call('/api/v1/admin/hive/calls/' + id + '/reconcile', 'admin', {
            usage: { input: 2, cachedInput: 0, output: 1 },
            evidence: 'Confirmed metering',
          })
        ).status,
    ),
  );
  expect(statuses.every((s) => s === 200 || s === 409)).toBe(true);
  expect(await credits.balance(account)).toEqual({ balance: 97, reserved: 0, available: 97 });
  const logs = await h.database.db
    .selectFrom('admin_audit_log')
    .selectAll()
    .where('action', '=', 'hive.reconcile')
    .where('target_id', '=', id)
    .execute();
  expect(logs).toHaveLength(1);
  expect(logs[0]?.details).toMatchObject({
    reason: 'Confirmed metering',
    to: { charged: 3, reserved: 0 },
  });
  expect(
    (
      await call('/api/v1/admin/hive/calls/' + id + '/reconcile', 'moderator', {
        usage: { input: 2, cachedInput: 0, output: 1 },
        evidence: 'Metering',
      })
    ).status,
  ).toBe(403);
});
it('keeps financial modes separate and makes unpriced metered costs unavailable', async () => {
  await h.database.db.transaction().execute((tx) =>
    recordPaymentFact(tx, {
      product: 'maps',
      purchaseId: 'money-fixture',
      providerId: 'pi_verified',
      mode: 'test',
      currency: 'cad',
      paid: 1234,
      refunded: 0,
      disputed: false,
      occurredAt: new Date(),
    }),
  );
  await h.database.db
    .insertInto('admin_provider_attempts')
    .values({
      product: 'maps',
      attempt_id: 'meter-fixture',
      request_id: 'safe-metadata',
      model: 'unpriced',
      stage: 'image',
      status: 'failed',
      usage: { input: 10, output: 5, cachedInput: 0 },
      created_at: new Date(),
    })
    .execute();
  const response = await call('/api/v1/admin/finances?days=7&mode=test'),
    data = (await response.json()) as AdminFinances;
  expect(response.status).toBe(200);
  expect(schemaIssues(AdminFinances, data)).toEqual([]);
  expect(data.cash).toContainEqual(
    expect.objectContaining({ currency: 'cad', amount: 1234, mode: 'test' }),
  );
  expect(data.credits).toContainEqual({ product: 'hive', kind: 'returned', amount: 47 });
  expect(data.costs.find((c) => c.model === 'unpriced')).toMatchObject({
    metered: 1,
    priced: 0,
    estimatedMicros: null,
  });
  const live = (await (
    await call('/api/v1/admin/finances?days=7&mode=live')
  ).json()) as AdminFinances;
  expect(live.cash).toHaveLength(0);
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

it('exports retained activity and erases markers even when recording races with deletion', async () => {
  const victim = await h.database.db
    .insertInto('accounts')
    .values({ kind: 'registered', display_name: 'Activity deletion' })
    .returningAll()
    .executeTakeFirstOrThrow();
  sessions['victim'] = await api.app.identity.webSessions.create(victim.id);
  await new AccountActivity(h.database.db).record(victim);
  const exported = (await (await call('/api/v1/accounts/me/export', 'victim')).json()) as {
    activityDays: { day: string; kind: string }[];
  };
  expect(exported.activityDays).toHaveLength(1);
  const admin = await h.database.db
    .selectFrom('accounts')
    .selectAll()
    .where('id', '=', ids['admin']!)
    .executeTakeFirstOrThrow();
  await Promise.all([
    new AccountActivity(h.database.db).record(victim),
    api.app.identity.admin.deleteAccount(admin, victim, 'Deletion test'),
    new AccountActivity(h.database.db).record(victim),
  ]);
  expect(
    await h.database.db
      .selectFrom('account_activity_days')
      .selectAll()
      .where('account_id', '=', victim.id)
      .execute(),
  ).toHaveLength(0);
});

it('includes the whole selected UTC audit day and hides non-moderation library actions', async () => {
  const target = randomUUID();
  for (const [action, at] of [
    ['content.hide', '2026-10-08T23:59:59.999Z'],
    ['content.restore', '2026-10-09T00:00:00Z'],
    ['music.credentials.rotate', '2026-10-08T12:00:00Z'],
    ['building.update', '2026-10-08T12:00:00Z'],
  ])
    await h.database.db
      .insertInto('admin_audit_log')
      .values({
        actor_account_id: ids['admin']!,
        action: action!,
        target_type: 'maps',
        target_id: target,
        details: {},
        created_at: new Date(at!),
      })
      .execute();
  const path = '/api/v1/admin/audit?target=' + target + '&from=2026-10-08&to=2026-10-08';
  const mod = (await (await call(path, 'moderator')).json()) as { items: { action: string }[] };
  expect(mod.items.map((r) => r.action)).toEqual(['content.hide']);
  const admin = (await (await call(path)).json()) as { items: { action: string }[] };
  expect(admin.items).toHaveLength(3);
  expect((await call('/api/v1/admin/audit?to=2026-02-30')).status).toBe(400);
});

it('rejects NUL moderation reasons before any mutation', async () => {
  const map = await h.database.db
    .selectFrom('maps')
    .select(['id', 'hidden'])
    .limit(1)
    .executeTakeFirstOrThrow();
  expect(
    (
      await call('/api/v1/admin/content/maps/' + map.id + '/moderation', 'moderator', {
        hidden: !map.hidden,
        reason: 'Review\0reason',
      })
    ).status,
  ).toBe(400);
  expect(
    (
      await h.database.db
        .selectFrom('maps')
        .select('hidden')
        .where('id', '=', map.id)
        .executeTakeFirstOrThrow()
    ).hidden,
  ).toBe(map.hidden);
});

it('commits role changes and accurate audit values together even with stale account rows', async () => {
  const actor = await h.database.db
    .selectFrom('accounts')
    .selectAll()
    .where('id', '=', ids['admin']!)
    .executeTakeFirstOrThrow();
  const target = await h.database.db
    .insertInto('accounts')
    .values({ kind: 'registered', display_name: 'Role transaction' })
    .returningAll()
    .executeTakeFirstOrThrow();
  await api.app.identity.admin.setRole(actor, target, 'moderator', 'Promoted');
  await api.app.identity.admin.setRole(actor, target, 'user', 'Demoted from stale list');
  const audits = await h.database.db
    .selectFrom('admin_audit_log')
    .select('details')
    .where('target_id', '=', target.id)
    .where('action', '=', 'account.role')
    .orderBy('id')
    .execute();
  expect(audits.map((a) => a.details)).toEqual([
    { from: 'user', to: 'moderator', reason: 'Promoted' },
    { from: 'moderator', to: 'user', reason: 'Demoted from stale list' },
  ]);
  // A missing actor makes the audit FK fail after the account UPDATE. The role
  // must still roll back; this catches a separately committed audit write.
  await expect(
    api.app.identity.admin.setRole({ ...actor, id: randomUUID() }, target, 'admin', 'Audit fails'),
  ).rejects.toThrow();
  expect(
    (
      await h.database.db
        .selectFrom('accounts')
        .select('role')
        .where('id', '=', target.id)
        .executeTakeFirstOrThrow()
    ).role,
  ).toBe('user');
  await api.app.identity.admin.deleteAccount(actor, target, 'Role deletion test');
  await expect(api.app.identity.admin.setRole(actor, target, 'admin')).rejects.toThrow(
    'No such account',
  );
});

it('shows retained metering when the provider result was never journaled to private output', async () => {
  const thread = await h.database.db
    .insertInto('studio_threads')
    .values({ account_id: ids['user']!, title: 'Lost provider reply' })
    .returning('id')
    .executeTakeFirstOrThrow();
  const requestId = randomUUID(),
    attemptId = randomUUID(),
    usage = { input_tokens: 20, output_tokens: 5 };
  await h.database.db
    .insertInto('studio_requests')
    .values({
      id: requestId,
      thread_id: thread.id,
      account_id: ids['user']!,
      kind: 'generate',
      status: 'uncertain',
      input: {},
    })
    .execute();
  await h.database.db
    .insertInto('studio_attempts')
    .values({
      id: attemptId,
      request_id: requestId,
      model: 'review-model',
      stage: 'image',
      status: 'uncertain',
      input: { privatePrompt: 'Must remain private' },
    })
    .execute();
  await recordAttemptUsage(h.database.db, 'maps', requestId, 'image', usage);
  const detail = (await (
    await call('/api/v1/admin/operations/maps/' + requestId)
  ).json()) as AdminOperationDetail;
  expect(detail.attempts[0]?.usage).toEqual({ input: 20, cached: 0, output: 5 });
  expect(JSON.stringify(detail)).not.toContain('privatePrompt');
  expect(schemaIssues(AdminOperationDetail, detail)).toEqual([]);
});

it('excludes preserved staff endpoints and dashboard session reads from activity', async () => {
  const staff = await h.database.db
    .insertInto('accounts')
    .values({ kind: 'registered', display_name: 'Dashboard observer', role: 'moderator' })
    .returningAll()
    .executeTakeFirstOrThrow();
  sessions['observer'] = await api.app.identity.webSessions.create(staff.id);
  expect((await call('/api/v1/building-reports', 'observer')).status).toBe(200);
  expect(
    (
      await fetch(api.url + '/api/v1/accounts/me', {
        headers: {
          cookie: 'glob2_session=' + sessions['observer'],
          referer: api.url + '/admin/reports',
        },
      })
    ).status,
  ).toBe(200);
  await new Promise((resolve) => setTimeout(resolve, 20));
  const markers = () =>
    h.database.db
      .selectFrom('account_activity_days')
      .selectAll()
      .where('account_id', '=', staff.id)
      .execute();
  expect(await markers()).toHaveLength(0);
  // Ordinary game/account use by a moderator still contributes activity.
  await call('/api/v1/accounts/me', 'observer');
  await new Promise((resolve) => setTimeout(resolve, 20));
  expect(await markers()).toHaveLength(1);
});

it('preserves microsecond cursors for content, audit, accounts and operations', async () => {
  const timestamp = sql<Date>`'2026-10-01T00:00:00.123456Z'::timestamptz`,
    target = randomUUID();
  for (let i = 0; i < 51; i++)
    await h.database.db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: 'Precision account ' + i, created_at: timestamp })
      .execute();
  for (let i = 0; i < 3; i++) {
    await h.database.db
      .insertInto('admin_audit_log')
      .values({
        actor_account_id: ids['admin']!,
        action: 'content.hide',
        target_type: 'maps',
        target_id: target,
        details: {},
        created_at: timestamp,
      })
      .execute();
    await h.database.db
      .insertInto('engine_jobs')
      .values({
        kind: 'render-preview',
        sim_version: '125-49-' + 'ab'.repeat(32),
        payload: {},
        created_at: timestamp,
      })
      .execute();
  }
  for (let i = 0; i < 3; i++)
    await h.database.db
      .insertInto('matches')
      .values({
        origin: 'queue',
        queue_id: 'precision',
        sim_version: '125-49-' + 'ab'.repeat(32),
        setup: {},
        seed: 1,
        map_hash: 'ab'.repeat(32),
        created_at: timestamp,
      })
      .execute();
  for (const [base, expected] of [
    ['/api/v1/admin/content?library=maps&q=Map&limit=1', 3],
    ['/api/v1/admin/audit?target=' + target + '&limit=1', 3],
    ['/api/v1/admin/accounts?q=Precision%20account', 51],
    ['/api/v1/admin/operations?product=engine&limit=1', 3],
    ['/api/v1/admin/matches?queue=precision&limit=1', 3],
  ] as const) {
    const ids: string[] = [];
    let cursor = '';
    do {
      const page = (await (await call(base + '&cursor=' + cursor)).json()) as {
        items: { id: string }[];
        nextCursor?: string;
      };
      ids.push(...page.items.map((row) => row.id));
      cursor = page.nextCursor ?? '';
    } while (cursor);
    expect(ids, base).toHaveLength(expected);
    expect(new Set(ids).size, base).toBe(expected);
  }
});

it('preserves cache-write evidence and charges its distinct credit rate during Hive recovery', async () => {
  const account = await h.database.db
    .insertInto('accounts')
    .values({ kind: 'registered', display_name: 'Cache write recovery' })
    .returning('id')
    .executeTakeFirstOrThrow();
  const credits = new Credits(h.database.db),
    id = randomUUID();
  await credits.adjust(account.id, randomUUID(), 100, 'grant');
  await credits.reserve(account.id, id, 50, {
    version: 'cache-test',
    model: 'cache-model',
    input: 1000000,
    cachedInput: 0,
    cacheWrite: 2000000,
    output: 1000000,
  });
  await credits.dispatch(id);
  await credits.uncertain(id);
  const usage = { input: 10, cachedInput: 3, cacheWrite: 4, output: 2 };
  await recordAttemptUsage(h.database.db, 'hive', id, 'usage', usage);
  const detail = (await (
    await call('/api/v1/admin/operations/hive/' + id)
  ).json()) as AdminOperationDetail;
  expect(detail.usage).toEqual({ input: 10, cached: 3, cacheWrite: 4, output: 2 });
  const response = await call('/api/v1/admin/hive/calls/' + id + '/reconcile', 'admin', {
    usage,
    evidence: 'Provider confirmed cache writes',
  });
  expect(response.status).toBe(200);
  expect(await response.json()).toEqual({ charged: 13 });
  expect(await credits.balance(account.id)).toEqual({ balance: 87, reserved: 0, available: 87 });
  await h.database.db
    .insertInto('admin_provider_rates')
    .values({
      version: 'cache-cost',
      model: 'cache-model',
      currency: 'usd',
      effective_at: new Date('2025-01-01T00:00:00Z'),
      input_micros: 1000000,
      cached_input_micros: 0,
      output_micros: 1000000,
      call_micros: 0,
    })
    .execute();
  const finances = (await (
    await call('/api/v1/admin/finances?days=90&mode=unclassified')
  ).json()) as AdminFinances;
  expect(finances.costs.find((r) => r.model === 'cache-model')).toMatchObject({
    metered: 1,
    priced: 0,
    estimatedMicros: null,
  });
});
