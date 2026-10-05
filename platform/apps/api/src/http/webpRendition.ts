// Persistent wire renditions preserve published source blobs and version IDs.
// Source hashes remain immutable; clients receive hashes of the exact WebP bytes.
import sharp from 'sharp';
import { putContent, type BlobStore } from '@glob2/core';
import type { Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import { apiError } from '../errors.ts';

const pending = new WeakMap<BlobStore, Map<string, Promise<string>>>();
let active = 0;
const waiting: Array<() => void> = [];
async function admitted<T>(work: () => Promise<T>): Promise<T> {
  if (active >= 4) await new Promise<void>((resolve) => waiting.push(resolve));
  else ++active;
  try {
    return await work();
  } finally {
    const next = waiting.shift();
    if (next) next();
    else --active;
  }
}
export async function webpRendition(
  db: Kysely<Database>,
  blobs: BlobStore,
  source: string,
): Promise<string> {
  let requests = pending.get(blobs);
  if (!requests) {
    requests = new Map();
    pending.set(blobs, requests);
  }
  const existing = requests.get(source);
  if (existing) return existing;
  const task = admitted(async () => {
    const row = await db
      .selectFrom('blobs')
      .selectAll()
      .where('sha256', '=', source)
      .executeTakeFirst();
    if (!row) throw apiError('not_found', 'Image not found.');
    if (row.content_type === 'image/webp') return source;
    const previous = await db
      .selectFrom('image_webp_renditions')
      .select('webp_sha256')
      .where('source_sha256', '=', source)
      .executeTakeFirst();
    if (previous) return previous.webp_sha256;
    const stream = await blobs.get(row.storage_key);
    if (!stream) throw apiError('not_found', 'Image is missing from storage.');
    const chunks: Buffer[] = [];
    let size = 0;
    for await (const chunk of stream) {
      size += chunk.length;
      if (size > 16 * 1024 * 1024) {
        stream.destroy();
        throw apiError('bad_request', 'Image is too large.');
      }
      chunks.push(Buffer.from(chunk));
    }
    const bytes = await sharp(Buffer.concat(chunks), {
      limitInputPixels: 64 * 1024 * 1024,
      failOn: 'warning',
    })
      .webp({ lossless: true, effort: 4 })
      .toBuffer();
    const stored = await putContent(blobs, bytes);
    return db.transaction().execute(async (trx) => {
      await trx
        .insertInto('blobs')
        .values({
          sha256: stored.sha256,
          size: stored.size,
          storage_key: stored.key,
          content_type: 'image/webp',
          visibility: row.visibility,
          owner_account_id: row.owner_account_id,
        })
        .onConflict((oc) => oc.column('sha256').doNothing())
        .execute();
      await trx
        .insertInto('image_webp_renditions')
        .values({ source_sha256: source, webp_sha256: stored.sha256 })
        .onConflict((oc) => oc.column('source_sha256').doNothing())
        .execute();
      // A concurrent replica may have won with another encoder build. Always
      // sign and serve the persisted winner, never a local conversion's hash.
      const winner = await trx
        .selectFrom('image_webp_renditions')
        .select('webp_sha256')
        .where('source_sha256', '=', source)
        .executeTakeFirstOrThrow();
      return winner.webp_sha256;
    });
  });
  requests.set(source, task);
  try {
    return await task;
  } finally {
    requests.delete(source);
  }
}
