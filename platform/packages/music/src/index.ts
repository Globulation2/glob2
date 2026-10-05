import { sql, type Kysely, type Selectable } from 'kysely';
import type { Database } from '@glob2/db';
import type { BlobStore } from '@glob2/core';
import type { MusicRelease, MusicTrack } from '@glob2/protocol';
export const MUSIC_INSPECT = 'music-inspect';
export const MUSIC_CONVERT = 'music-convert';
export const MUSIC_UPLOAD_BYTES = 512 * 1024 * 1024;
export type ReleaseRow = Selectable<Database['music_releases']>;
export interface ConversionResult {
  frames: number;
  tracks: MusicTrack[];
  warnings: string[];
}
export async function releaseView(
  db: Kysely<Database>,
  row: ReleaseRow,
  accountId?: string,
): Promise<MusicRelease> {
  const likes = await db
    .selectFrom('music_likes')
    .select(({ fn }) => fn.countAll<number>().as('count'))
    .where('release_id', '=', row.id)
    .executeTakeFirstOrThrow();
  const liked = accountId
    ? await db
        .selectFrom('music_likes')
        .select('account_id')
        .where('release_id', '=', row.id)
        .where('account_id', '=', accountId)
        .executeTakeFirst()
    : undefined;
  const assets = await db
    .selectFrom('music_assets')
    .selectAll()
    .where('release_id', '=', row.id)
    .execute();
  const result = row.result as unknown as ConversionResult | null;
  const prefix = `/api/v1/music/${row.id}`;
  return {
    id: row.id,
    ownerId: row.owner_id,
    metadata: row.metadata,
    status: row.status,
    createdAt: row.created_at.toISOString(),
    frames: result?.frames ?? 0,
    tracks: (result?.tracks ?? []).map((t) => ({ ...t, url: `${prefix}/tracks/${t.mood}` })),
    warnings: result?.warnings ?? [],
    inspection: row.inspection,
    uploaded: Object.keys(row.sources),
    coverUrl: assets.some((a) => a.kind === 'cover') ? `${prefix}/cover` : null,
    likes: Number(likes.count),
    downloads: row.downloads,
    liked: !!liked,
    hidden: row.hidden,
    error: row.error,
  };
}
/** Upload keys are private, never exposed through the public blob route. */
export function sourceKey(id: string, kind: string, nonce: string): string {
  return `music-uploads/${id}/${kind}-${nonce}`;
}
export async function expireMusic(db: Kysely<Database>, blobs: BlobStore): Promise<void> {
  const stale = await db
    .updateTable('music_releases')
    .set({
      status: 'failed',
      error: 'Upload expired. Please upload the files again.',
      updated_at: new Date(),
    })
    .where('status', 'in', ['draft', 'inspected', 'inspecting', 'converting'])
    .where('updated_at', '<', sql<Date>`now() - interval '24 hours'`)
    .returningAll()
    .execute();
  for (const row of stale) {
    for (const key of Object.values(row.sources)) await blobs.delete(key);
    await db
      .updateTable('music_releases')
      .set({ sources: '{}' })
      .where('id', '=', row.id)
      .where('status', '=', 'failed')
      .execute();
  }
  // Catch crashes between writing a source blob and linking it to the draft,
  // and failed deletes after a completed/cancelled job. Active inputs live <24h.
  if (blobs.list)
    for await (const blob of blobs.list('music-uploads/')) {
      if (blob.modifiedAt.getTime() < Date.now() - 24 * 60 * 60_000) await blobs.delete(blob.key);
    }
}
export { zipStream } from './zip.ts';
