// Content-addressed blob storage for maps, saves, previews, match records,
// replays and verifier results. The default stores files in a directory (a
// Docker volume in the compose stack); an S3-compatible store is optional.
// Metadata (size, content type, ownership) lives in the `blobs` table.
import { createHash, randomBytes } from 'node:crypto';
import { createReadStream } from 'node:fs';
import { mkdir, rename, rm, stat, writeFile } from 'node:fs/promises';
import { dirname, join, resolve, sep } from 'node:path';
import type { Readable } from 'node:stream';
import type { BlobStoreConfig } from './config.ts';

export interface BlobStore {
  readonly kind: string;
  /** Stores bytes under a key, replacing any previous value atomically. */
  put(key: string, data: Uint8Array): Promise<void>;
  /** Streams a blob, or undefined when absent. */
  get(key: string): Promise<Readable | undefined>;
  /** Size in bytes, or undefined when absent. */
  size(key: string): Promise<number | undefined>;
  delete(key: string): Promise<void>;
}

const KEY_PATTERN = /^[a-z0-9][a-z0-9/_.-]{0,255}$/;

export function assertBlobKey(key: string): void {
  if (!KEY_PATTERN.test(key) || key.includes('..') || key.includes('//') || key.endsWith('/')) {
    throw new Error(`invalid blob key ${JSON.stringify(key)}`);
  }
}

export function sha256Hex(data: Uint8Array): string {
  return createHash('sha256').update(data).digest('hex');
}

/** Storage key for content with this digest: sha256/ab/abcdef…. */
export function contentKey(sha256: string): string {
  if (!/^[0-9a-f]{64}$/.test(sha256)) throw new Error('invalid sha256');
  return `sha256/${sha256.slice(0, 2)}/${sha256}`;
}

/** Stores bytes by their SHA-256; storing identical bytes twice is a no-op. */
export async function putContent(
  store: BlobStore,
  data: Uint8Array,
): Promise<{ sha256: string; size: number; key: string }> {
  const sha256 = sha256Hex(data);
  const key = contentKey(sha256);
  if ((await store.size(key)) !== data.byteLength) await store.put(key, data);
  return { sha256, size: data.byteLength, key };
}

export class FsBlobStore implements BlobStore {
  readonly kind = 'fs';
  private readonly root: string;

  constructor(directory: string) {
    this.root = resolve(directory);
  }

  private path(key: string): string {
    assertBlobKey(key);
    const path = resolve(this.root, key);
    if (!path.startsWith(this.root + sep)) throw new Error('blob key escapes the store');
    return path;
  }

  async put(key: string, data: Uint8Array): Promise<void> {
    const target = this.path(key);
    await mkdir(dirname(target), { recursive: true });
    const temporary = join(dirname(target), `.tmp-${randomBytes(8).toString('hex')}`);
    try {
      await writeFile(temporary, data, { flag: 'wx' });
      await rename(temporary, target);
    } catch (error) {
      await rm(temporary, { force: true });
      throw error;
    }
  }

  async get(key: string): Promise<Readable | undefined> {
    const path = this.path(key);
    if ((await this.size(key)) === undefined) return undefined;
    return createReadStream(path);
  }

  async size(key: string): Promise<number | undefined> {
    try {
      const info = await stat(this.path(key));
      return info.isFile() ? info.size : undefined;
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code === 'ENOENT') return undefined;
      throw error;
    }
  }

  async delete(key: string): Promise<void> {
    await rm(this.path(key), { force: true });
  }
}

/**
 * Placeholder for an S3-compatible store. Selecting it fails at start-up until
 * it is implemented, so a misconfigured instance does not silently use disk.
 */
export class S3BlobStore implements BlobStore {
  readonly kind = 's3';
  constructor(config: BlobStoreConfig) {
    throw new Error(
      `BLOB_STORE=s3 is not implemented yet (bucket ${config.bucket ?? '(unset)'}); use BLOB_STORE=fs`,
    );
  }
  put(): Promise<void> {
    throw new Error('unreachable');
  }
  get(): Promise<Readable | undefined> {
    throw new Error('unreachable');
  }
  size(): Promise<number | undefined> {
    throw new Error('unreachable');
  }
  delete(): Promise<void> {
    throw new Error('unreachable');
  }
}

export function createBlobStore(config: BlobStoreConfig): BlobStore {
  return config.kind === 's3' ? new S3BlobStore(config) : new FsBlobStore(config.directory);
}
