import { afterAll, beforeAll, expect, it } from 'vitest';
import { sql } from 'kysely';
import { createTestDatabase, type TestDatabase } from '../../db/test/support.ts';
import { AccountActivity, retainAnalytics } from '../src/analytics.ts';
let db: TestDatabase;
beforeAll(async () => {
  db = await createTestDatabase();
});
afterAll(async () => {
  await db?.drop();
});
it('deduplicates across replicas, crosses midnight, and updates guest kind', async () => {
  const account = await db.db
    .insertInto('accounts')
    .values({ kind: 'guest', display_name: 'Activity guest' })
    .returningAll()
    .executeTakeFirstOrThrow();
  const a = new AccountActivity(db.db),
    b = new AccountActivity(db.db);
  await Promise.all([
    a.record(account, new Date('2026-10-08T23:59:59Z')),
    b.record(account, new Date('2026-10-08T23:59:59Z')),
  ]);
  await a.record(account, new Date('2026-10-09T00:00:00Z'));
  await db.db
    .updateTable('accounts')
    .set({ kind: 'registered' })
    .where('id', '=', account.id)
    .execute();
  await a.record({ ...account, kind: 'registered' }, new Date('2026-10-09T00:00:00Z'));
  const rows = await db.db
    .selectFrom('account_activity_days')
    .selectAll()
    .where('account_id', '=', account.id)
    .orderBy('day')
    .execute();
  expect(rows).toHaveLength(2);
  expect(rows[1]?.kind).toBe('registered');
  const totals = await db.db
    .selectFrom('admin_daily_metrics')
    .selectAll()
    .where('metric', '=', 'activity')
    .where('day', '=', '2026-10-09')
    .execute();
  expect(totals.find((r) => r.dimension === 'guest')?.value).toBe(0);
  expect(totals.find((r) => r.dimension === 'registered')?.value).toBe(1);
});
it('preserves aggregates through retention and makes repeated status updates idempotent', async () => {
  const account = await db.db.selectFrom('accounts').selectAll().limit(1).executeTakeFirstOrThrow();
  const collector = new AccountActivity(db.db);
  await collector.record(account, new Date('2025-10-01T12:00:00Z'));
  await retainAnalytics(db.db, new Date('2026-10-08T12:00:00Z'));
  expect(
    await db.db
      .selectFrom('account_activity_days')
      .selectAll()
      .where('day', '=', '2025-10-01')
      .execute(),
  ).toHaveLength(0);
  expect(
    await db.db
      .selectFrom('admin_daily_metrics')
      .selectAll()
      .where('day', '=', '2025-10-01')
      .where('metric', '=', 'activity')
      .execute(),
  ).toHaveLength(1);
  const job = await db.db
    .insertInto('engine_jobs')
    .values({
      kind: 'render-preview',
      sim_version: '125-49-' + 'ab'.repeat(32),
      payload: {},
      created_at: new Date('2026-10-08T00:00:00Z'),
    })
    .returning('id')
    .executeTakeFirstOrThrow();
  await db.db
    .updateTable('engine_jobs')
    .set({ status: 'failed', completed_at: new Date('2026-10-08T00:01:00Z') })
    .where('id', '=', job.id)
    .execute();
  await db.db
    .updateTable('engine_jobs')
    .set({ status: 'failed' })
    .where('id', '=', job.id)
    .execute();
  expect(
    (
      await db.db
        .selectFrom('admin_daily_metrics')
        .select('value')
        .where('metric', '=', 'status.engine')
        .where('dimension', '=', 'failed')
        .where('day', '=', '2026-10-08')
        .executeTakeFirstOrThrow()
    ).value,
  ).toBe(1);
  await db.db.deleteFrom('engine_jobs').where('id', '=', job.id).execute();
  expect(
    (
      await db.db
        .selectFrom('admin_daily_metrics')
        .select('value')
        .where('metric', '=', 'status.engine')
        .where('dimension', '=', 'failed')
        .where('day', '=', '2026-10-08')
        .executeTakeFirstOrThrow()
    ).value,
  ).toBe(1);
});
it('can disable activity recording independently', async () => {
  const account = await db.db.selectFrom('accounts').selectAll().limit(1).executeTakeFirstOrThrow();
  await new AccountActivity(db.db, false).record(account, new Date('2026-10-10T00:00:00Z'));
  expect(
    await db.db
      .selectFrom('account_activity_days')
      .selectAll()
      .where('day', '=', '2026-10-10')
      .execute(),
  ).toHaveLength(0);
  await sql`UPDATE admin_analytics_settings SET collection=false WHERE id`.execute(db.db);
  await db.db
    .insertInto('accounts')
    .values({
      kind: 'guest',
      display_name: 'Disabled guest',
      created_at: new Date('2026-10-11T00:00:00Z'),
    })
    .execute();
  expect(
    await db.db
      .selectFrom('admin_daily_metrics')
      .selectAll()
      .where('day', '=', '2026-10-11')
      .where('metric', '=', 'accounts.created')
      .execute(),
  ).toHaveLength(0);
  await sql`UPDATE admin_analytics_settings SET collection=true WHERE id`.execute(db.db);
});

it('replaces the last counted state after collection pauses and preserves totals after cleanup', async () => {
  const day = '2026-10-12';
  const job = await db.db
    .insertInto('engine_jobs')
    .values({
      kind: 'render-preview',
      sim_version: '125-49-' + 'ab'.repeat(32),
      payload: {},
      created_at: new Date(day + 'T00:00:00Z'),
    })
    .returning('id')
    .executeTakeFirstOrThrow();
  const totals = () =>
    db.db.selectFrom('admin_daily_metrics').selectAll().where('day', '=', day).execute();
  await sql`UPDATE admin_analytics_settings SET collection=false WHERE id`.execute(db.db);
  try {
    await db.db
      .updateTable('engine_jobs')
      .set({ status: 'failed' })
      .where('id', '=', job.id)
      .execute();
    const paused = await db.db
      .insertInto('engine_jobs')
      .values({
        kind: 'render-preview',
        sim_version: '125-49-' + 'ab'.repeat(32),
        payload: {},
        created_at: new Date(day + 'T00:00:00Z'),
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    expect(
      await db.db
        .selectFrom('admin_metric_sources')
        .selectAll()
        .where('source_id', '=', paused.id)
        .execute(),
    ).toHaveLength(0);
    await sql`UPDATE admin_analytics_settings SET collection=true WHERE id`.execute(db.db);
    await db.db
      .updateTable('engine_jobs')
      .set({ status: 'succeeded', completed_at: new Date(day + 'T00:01:00Z') })
      .where('id', '=', job.id)
      .execute();
    await db.db
      .updateTable('engine_jobs')
      .set({ status: 'failed' })
      .where('id', '=', paused.id)
      .execute();
    let rows = await totals();
    expect(
      rows.filter((r) => r.metric === 'status.engine').reduce((sum, r) => sum + r.value, 0),
    ).toBe(2);
    expect(rows.find((r) => r.metric === 'status.engine' && r.dimension === 'queued')?.value).toBe(
      0,
    );
    expect(
      rows.find((r) => r.metric === 'duration.engine' && r.dimension === 'seconds')?.value,
    ).toBe(60);
    await sql`UPDATE admin_analytics_settings SET collection=false WHERE id`.execute(db.db);
    await db.db
      .updateTable('engine_jobs')
      .set({ completed_at: new Date(day + 'T00:02:00Z') })
      .where('id', '=', job.id)
      .execute();
    await sql`UPDATE admin_analytics_settings SET collection=true WHERE id`.execute(db.db);
    // Even an unrelated update reconciles the previously counted duration.
    await db.db
      .updateTable('engine_jobs')
      .set({ payload: { refreshed: true } })
      .where('id', '=', job.id)
      .execute();
    await db.db
      .updateTable('engine_jobs')
      .set({ payload: { refreshed: true } })
      .where('id', '=', job.id)
      .execute();
    rows = await totals();
    expect(
      rows.find((r) => r.metric === 'duration.engine' && r.dimension === 'seconds')?.value,
    ).toBe(120);
    expect(
      rows.find((r) => r.metric === 'duration.engine' && r.dimension === 'samples')?.value,
    ).toBe(1);
    await db.db.deleteFrom('engine_jobs').where('id', 'in', [job.id, paused.id]).execute();
    expect(
      await db.db
        .selectFrom('admin_metric_sources')
        .selectAll()
        .where('source_id', 'in', [job.id, paused.id])
        .execute(),
    ).toHaveLength(0);
    expect(await totals()).toEqual(rows);
  } finally {
    await sql`UPDATE admin_analytics_settings SET collection=true WHERE id`.execute(db.db);
  }
});
