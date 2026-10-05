// Blob garbage collection: mark and sweep over the `blobs` table. A blob is
// garbage when nothing references it and it is older than the grace period:
// no map version (file or preview), match artifact (record, replay, result),
// colony skin texture or material map, upload, generated or warm map, and no match played on it (matches.map_hash:
// replays and re-verification need the map). Rows referenced through foreign
// keys cannot be deleted anyway; the others are checked in the same statement.
// The row goes first, then the stored bytes. A second pass (stores that can
// list their keys) deletes stored files that no row names, e.g. from an upload
// that failed between storing the bytes and recording them.
//
// Known window: content re-uploaded at the very moment its unreferenced,
// week-old blob is collected can lose its bytes (the upload saw the file just
// before the collector removed it). The grace period makes this unlikely; the
// collector re-checks the row right before deleting the bytes.
import { sql, type Kysely } from 'kysely';
import { contentKey, type BlobStore, type Logger } from '@glob2/core';
import type { Database } from '@glob2/db';

type Db = Kysely<Database>;

/** Unreferenced blobs (and unrecorded stored files) younger than this are kept. */
export const BLOB_GC_GRACE_DAYS = 7;
/** Blobs collected per run. */
export const BLOB_GC_BATCH = 200;

export interface BlobGcResult {
  deletedBlobs: number;
  deletedOrphanFiles: number;
}

export async function collectBlobs(
  db: Db,
  store: BlobStore,
  options: { graceDays?: number; batch?: number; logger?: Pick<Logger, 'warn'> } = {},
): Promise<BlobGcResult> {
  const graceDays = options.graceDays ?? BLOB_GC_GRACE_DAYS;
  const deleted = await sql<{ sha256: string; storage_key: string }>`
    DELETE FROM blobs b
    WHERE b.sha256 IN (
      SELECT c.sha256 FROM blobs c
      WHERE c.created_at < now() - make_interval(days => ${graceDays})
        AND NOT EXISTS (SELECT 1 FROM colony_skin_versions v WHERE v.texture_sha256 = c.sha256 OR v.material_sha256 = c.sha256)
        AND NOT EXISTS (SELECT 1 FROM ai_versions v WHERE v.hash = c.sha256)
        AND NOT EXISTS (SELECT 1 FROM ai_validations v WHERE v.hash = c.sha256)
        AND NOT EXISTS (SELECT 1 FROM music_assets m WHERE m.sha256 = c.sha256)
        AND NOT EXISTS (SELECT 1 FROM map_versions v WHERE v.hash = c.sha256)
        AND NOT EXISTS (SELECT 1 FROM map_versions v WHERE v.preview_hash = c.sha256)
        AND NOT EXISTS (SELECT 1 FROM match_artifacts a WHERE a.blob_sha256 = c.sha256)
        AND NOT EXISTS (SELECT 1 FROM map_uploads u WHERE u.blob_sha256 = c.sha256)
        AND NOT EXISTS (SELECT 1 FROM generated_maps g WHERE g.map_hash = c.sha256)
        AND NOT EXISTS (SELECT 1 FROM matches m WHERE m.map_hash = c.sha256)
        AND NOT EXISTS (SELECT 1 FROM music_studio_artifacts a WHERE a.hash = c.sha256)
        AND NOT EXISTS (SELECT 1 FROM music_studio_requests r WHERE strpos(r.checkpoints::text, c.sha256) > 0)
        AND NOT EXISTS (SELECT 1 FROM music_studio_attempts a WHERE strpos(a.output::text, c.sha256) > 0)
        AND NOT EXISTS (SELECT 1 FROM studio_artifacts a WHERE a.hash = c.sha256)
        AND NOT EXISTS (SELECT 1 FROM studio_requests r WHERE r.map_hash = c.sha256 OR strpos(r.checkpoints::text, c.sha256) > 0)
        AND NOT EXISTS (SELECT 1 FROM studio_attempts a WHERE strpos(a.input::text, c.sha256) > 0 OR strpos(a.output::text, c.sha256) > 0)
      ORDER BY c.created_at
      LIMIT ${options.batch ?? BLOB_GC_BATCH}
      FOR UPDATE SKIP LOCKED
    )
    RETURNING b.sha256, b.storage_key`.execute(db);
  let deletedBlobs = 0;
  for (const row of deleted.rows) {
    const again = await db
      .selectFrom('blobs')
      .select('sha256')
      .where('sha256', '=', row.sha256)
      .executeTakeFirst();
    if (again) continue; // stored again meanwhile: keep the bytes
    try {
      await store.delete(row.storage_key);
      deletedBlobs++;
    } catch (error) {
      options.logger?.warn({ err: error, blob: row.sha256 }, 'could not delete blob bytes');
    }
  }
  let deletedOrphanFiles = 0;
  if (store.list) {
    const cutoff = Date.now() - graceDays * 86_400_000;
    for await (const entry of store.list('sha256/')) {
      if (entry.modifiedAt.getTime() >= cutoff) continue;
      const sha256 = entry.key.split('/').at(-1) ?? '';
      if (!/^[0-9a-f]{64}$/.test(sha256) || contentKey(sha256) !== entry.key) continue;
      const known = await db
        .selectFrom('blobs')
        .select('sha256')
        .where('sha256', '=', sha256)
        .executeTakeFirst();
      if (!known) {
        await store.delete(entry.key);
        deletedOrphanFiles++;
      }
    }
  }
  // Account images use unique keys, so failed uploads/replacements cannot overwrite
  // another account's image. Reap bytes left behind by a crash or failed deletion.
  if (store.list) {
    const cutoff = Date.now() - graceDays * 86_400_000;
    for await (const entry of store.list('avatars/')) {
      if (entry.modifiedAt.getTime() >= cutoff) continue;
      const known = await db
        .selectFrom('accounts')
        .select('id')
        .where((eb) =>
          eb.or([eb('avatar_key', '=', entry.key), eb('gravatar_key', '=', entry.key)]),
        )
        .executeTakeFirst();
      if (!known) {
        await store.delete(entry.key);
        deletedOrphanFiles++;
      }
    }
  }
  return { deletedBlobs, deletedOrphanFiles };
}
