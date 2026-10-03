// Map catalog bookkeeping that follows engine jobs and matches: a version's
// validate-map and render-preview results, the map's latest valid version,
// and play counts when a match on a catalog map ends. The API side (routes,
// visibility, moderation) is apps/api/src/maps/.
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import type { RenderPreviewResult, ValidateMapResult } from '@glob2/protocol';

type Db = Kysely<Database>;

/** Points each map at its newest valid version (or none). */
export async function refreshLatestVersions(db: Db, mapIds: readonly string[]): Promise<void> {
  if (mapIds.length === 0) return;
  await db
    .updateTable('maps')
    .set({
      latest_version_id: sql<string | null>`(
        SELECT v.id FROM map_versions v
        WHERE v.map_id = maps.id AND v.validation = 'valid'
        ORDER BY v.created_at DESC, v.id DESC LIMIT 1)`,
      updated_at: sql<Date>`now()`,
    })
    .where('id', 'in', [...new Set(mapIds)])
    .execute();
}

/**
 * Applies a validate-map result to every catalog version waiting on the job.
 * `result` is undefined when the job failed; `failure` then says why.
 */
export async function applyCatalogValidation(
  db: Db,
  jobId: string,
  blobHash: string,
  result: ValidateMapResult | undefined,
  failure: string | undefined,
): Promise<number> {
  const base = db
    .updateTable('map_versions')
    .where('validate_job_id', '=', jobId)
    .where('validation', '=', 'pending');
  let rows: { map_id: string }[];
  if (failure === undefined && result?.valid === true && result.mapHash === blobHash) {
    rows = await base
      .set({
        validation: 'valid',
        validation_error: null,
        width: result.map.width,
        height: result.map.height,
        team_count: result.map.teamCount,
        min_version_minor: result.versionMinor,
        file_title: result.title ?? null,
      })
      .returning('map_id')
      .execute();
  } else {
    const reason =
      failure ??
      (result?.valid === false
        ? result.reason
        : 'This file could not be checked. Upload it again; if that fails too, save it again in the game first.');
    rows = await base
      .set({ validation: 'invalid', validation_error: reason.slice(0, 2000) })
      .returning('map_id')
      .execute();
  }
  await refreshLatestVersions(
    db,
    rows.map((r) => r.map_id),
  );
  return rows.length;
}

/** Applies a render-preview result (or failure) to the catalog versions waiting on the job. */
export async function applyCatalogPreview(
  db: Db,
  jobId: string,
  result: RenderPreviewResult | undefined,
): Promise<number> {
  const base = db
    .updateTable('map_versions')
    .where('preview_job_id', '=', jobId)
    .where('preview_status', '=', 'pending');
  const updated = result
    ? await base
        .set({
          preview_status: 'ready',
          preview_hash: result.previewHash,
          preview_width: result.width,
          preview_height: result.height,
        })
        .executeTakeFirst()
    : await base.set({ preview_status: 'failed' }).executeTakeFirst();
  return Number(updated.numUpdatedRows);
}

/** Counts one play for every catalog map with a version of these bytes. */
export async function countCatalogPlay(db: Db, mapHash: string): Promise<void> {
  await db
    .updateTable('maps')
    .set((eb) => ({ play_count: eb('play_count', '+', 1) }))
    .where('id', 'in', db.selectFrom('map_versions').select('map_id').where('hash', '=', mapHash))
    .execute();
}
