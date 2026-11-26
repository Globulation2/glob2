// Retention of problem and crash reports (docs/multiplayer/problem-reports.md):
// a report's bundle (log, save, replay) is deleted after
// CRASH_BUNDLE_RETENTION_DAYS and its summary row after
// CRASH_REPORT_RETENTION_DAYS. Bundles that no row names, left by an upload
// that failed after storing its bytes, are deleted after a day.
import { sql, type Kysely } from 'kysely';
import type { BlobStore, Logger } from '@glob2/core';
import type { Database } from '@glob2/db';

export const CRASH_BUNDLE_RETENTION_DAYS = 90;
export const CRASH_REPORT_RETENTION_DAYS = 730;
export const CRASH_RETENTION_BATCH = 500;
const ORPHAN_GRACE_MS = 86_400_000;

export interface CrashRetentionResult {
  deletedBundles: number;
  deletedReports: number;
  deletedOrphanBundles: number;
}

const days = (n: number) => sql<Date>`now() - make_interval(days => ${n})`;

export async function retainCrashReports(
  db: Kysely<Database>,
  store: BlobStore,
  options: { logger?: Pick<Logger, 'warn'>; now?: number } = {},
): Promise<CrashRetentionResult> {
  const expired = await db
    .selectFrom('crash_reports')
    .select(['id', 'bundle_key'])
    .where('bundle_key', 'is not', null)
    .where('received_at', '<', days(CRASH_BUNDLE_RETENTION_DAYS))
    .orderBy('received_at')
    .limit(CRASH_RETENTION_BATCH)
    .execute();
  let deletedBundles = 0;
  for (const row of expired) {
    if (!row.bundle_key) continue;
    try {
      await store.delete(row.bundle_key);
      await db
        .updateTable('crash_reports')
        .set({ bundle_key: null })
        .where('id', '=', row.id)
        .execute();
      deletedBundles++;
    } catch (error) {
      options.logger?.warn({ err: error, report: row.id }, 'could not delete report bundle');
    }
  }
  // Rows older than the bundle retention have no bundle left (above).
  const reports = await sql`
    DELETE FROM crash_reports WHERE id IN (
      SELECT id FROM crash_reports
      WHERE bundle_key IS NULL AND received_at < ${days(CRASH_REPORT_RETENTION_DAYS)}
      LIMIT ${CRASH_RETENTION_BATCH})`.execute(db);
  let deletedOrphanBundles = 0;
  if (store.list) {
    const cutoff = (options.now ?? Date.now()) - ORPHAN_GRACE_MS;
    for await (const entry of store.list('crash-reports/')) {
      if (entry.modifiedAt.getTime() >= cutoff) continue;
      const known = await db
        .selectFrom('crash_reports')
        .select('id')
        .where('bundle_key', '=', entry.key)
        .executeTakeFirst();
      if (!known) {
        await store.delete(entry.key);
        deletedOrphanBundles++;
      }
    }
  }
  return {
    deletedBundles,
    deletedReports: Number(reports.numAffectedRows ?? 0n),
    deletedOrphanBundles,
  };
}
