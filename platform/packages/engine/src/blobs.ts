// Blob I/O for engine jobs. Blobs are content-addressed by the SHA-256 of
// their bytes; maps and saves are stored decompressed, so their hash is the
// hash every client computes over the bytes it loads. Stored blobs are also
// registered in the `blobs` table, which match artifacts and map versions
// reference.
import type { Kysely } from 'kysely';
import {
  GzipError,
  contentKey,
  gunzipBounded,
  isGzip,
  putContent,
  sha256Hex,
  type BlobStore,
} from '@glob2/core';
import type { Database } from '@glob2/db';
import { EngineInputError } from './engineCli.ts';

export const CONTENT_TYPES = {
  map: 'application/x-glob2-map',
  save: 'application/x-glob2-save',
  record: 'application/x-glob2-match-record',
  replay: 'application/x-glob2-replay',
  result: 'application/json',
  png: 'image/png',
} as const;

export { isGzip };

/** Decompresses gzip input (as the engine's loader does transparently), bounded. */
export function decompressIfGzip(bytes: Uint8Array, maxBytes: number): Uint8Array {
  if (!isGzip(bytes)) return bytes;
  try {
    return gunzipBounded(bytes, maxBytes);
  } catch (error) {
    if (error instanceof GzipError) throw new EngineInputError(error.message);
    throw error;
  }
}

export class AgentBlobs {
  private readonly store: BlobStore;
  private readonly db: Kysely<Database>;

  constructor(store: BlobStore, db: Kysely<Database>) {
    this.store = store;
    this.db = db;
  }

  /**
   * Reads a blob by hash, refusing anything over maxBytes and anything whose
   * bytes do not hash to the name (a corrupted store must not reach the engine).
   */
  async read(sha256: string, maxBytes: number): Promise<Uint8Array> {
    const key = contentKey(sha256);
    const size = await this.store.size(key);
    if (size === undefined) throw new EngineInputError(`blob ${sha256} not found`);
    if (size > maxBytes)
      throw new EngineInputError(`blob ${sha256} is ${size} bytes; limit ${maxBytes}`);
    const stream = await this.store.get(key);
    if (!stream) throw new EngineInputError(`blob ${sha256} not found`);
    const chunks: Buffer[] = [];
    let total = 0;
    for await (const chunk of stream) {
      const buffer = chunk as Buffer;
      total += buffer.length;
      if (total > maxBytes) {
        stream.destroy();
        throw new EngineInputError(`blob ${sha256} exceeds ${maxBytes} bytes`);
      }
      chunks.push(buffer);
    }
    const bytes = Buffer.concat(chunks);
    if (sha256Hex(bytes) !== sha256) {
      // Not the job's fault and maybe transient (a partial copy): retry.
      throw new Error(`blob ${sha256} does not match its hash`);
    }
    return bytes;
  }

  /** Stores bytes by content and registers them; returns the SHA-256. */
  async write(
    bytes: Uint8Array,
    contentType: string,
    visibility: 'public' | 'private' = 'private',
  ): Promise<string> {
    const { sha256, size, key } = await putContent(this.store, bytes);
    await this.db
      .insertInto('blobs')
      .values({ sha256, size, content_type: contentType, storage_key: key, visibility })
      .onConflict((oc) => oc.column('sha256').doNothing())
      .execute();
    return sha256;
  }
}
