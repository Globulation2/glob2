import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';

/** Enqueue inside the skin publication transaction; worker startup backfills
 * versions published before any renderer registered. */
export async function enqueueSkinSprites(
  db: Kysely<Database>,
  versionId: string,
  revision?: string,
) {
  const renderer =
    revision ??
    (
      await db
        .selectFrom('skin_render_revisions')
        .select('revision')
        .where('last_seen_at', '>', sql<Date>`now() - interval '90 seconds'`)
        .orderBy('created_at', 'desc')
        .orderBy('revision')
        .executeTakeFirst()
    )?.revision;
  if (!renderer) return;
  const row = await db
    .insertInto('colony_skin_sprites')
    .values({ version_id: versionId, render_revision: renderer })
    .onConflict((oc) => oc.columns(['version_id', 'render_revision']).doNothing())
    .returning('id')
    .executeTakeFirst();
  if (!row) return;
  await sql`SELECT graphile_worker.add_job(${`skin:render:${renderer}`}, ${JSON.stringify({ id: row.id })}::json,
    job_key := ${`skin:render:${row.id}`}, max_attempts := 3)`.execute(db);
}
