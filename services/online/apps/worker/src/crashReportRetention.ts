// Retention of problem and crash reports (docs/multiplayer/problem-reports.md):
// a report's bundle (log, save, replay) is deleted after
// CRASH_BUNDLE_RETENTION_DAYS and its summary row after
// CRASH_REPORT_RETENTION_DAYS. Bundles that no row names, left by an upload
// that failed after storing its bytes, are deleted after a day. Crash groups
// (docs/multiplayer/crash-reports.md) go once their last report is gone and
// they were last seen two years ago. Symbol files are kept; only uploads no
// crash_symbols row names (a failed or replaced upload) are swept.
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
  deletedGroups: number;
  deletedOrphanSymbols: number;
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
  const groups = await sql`
    DELETE FROM crash_groups WHERE id IN (
      SELECT g.id FROM crash_groups g
      WHERE g.last_seen_at < ${days(CRASH_REPORT_RETENTION_DAYS)}
        AND NOT EXISTS (SELECT 1 FROM crash_reports r WHERE r.group_id = g.id)
      LIMIT ${CRASH_RETENTION_BATCH})`.execute(db);
  const cutoff = (options.now ?? Date.now()) - ORPHAN_GRACE_MS;
  // One indexed lookup per batch of listed keys (the unique key indexes).
  const sweepOrphans = async (prefix: string, known: (keys: string[]) => Promise<Set<string>>) => {
    let deleted = 0;
    if (!store.list) return deleted;
    const sweep = async (keys: string[]) => {
      if (keys.length === 0) return;
      const named = await known(keys);
      for (const key of keys)
        if (!named.has(key)) {
          await store.delete(key);
          deleted++;
        }
    };
    let batch: string[] = [];
    for await (const entry of store.list(prefix)) {
      if (entry.modifiedAt.getTime() >= cutoff) continue;
      batch.push(entry.key);
      if (batch.length >= CRASH_RETENTION_BATCH) {
        await sweep(batch);
        batch = [];
      }
    }
    await sweep(batch);
    return deleted;
  };
  const deletedOrphanBundles = await sweepOrphans(
    'crash-reports/',
    async (keys) =>
      new Set(
        (
          await db
            .selectFrom('crash_reports')
            .select('bundle_key')
            .where('bundle_key', 'in', keys)
            .execute()
        ).flatMap((row) => (row.bundle_key ? [row.bundle_key] : [])),
      ),
  );
  const deletedOrphanSymbols = await sweepOrphans(
    'crash-symbols/',
    async (keys) =>
      new Set(
        (
          await db
            .selectFrom('crash_symbols')
            .select('blob_key')
            .where('blob_key', 'in', keys)
            .execute()
        ).map((row) => row.blob_key),
      ),
  );
  return {
    deletedBundles,
    deletedReports: Number(reports.numAffectedRows ?? 0n),
    deletedOrphanBundles,
    deletedGroups: Number(groups.numAffectedRows ?? 0n),
    deletedOrphanSymbols,
  };
}
