import type { AiValidationReport } from '@glob2/protocol';
// Blob I/O for engine jobs. Blobs are content-addressed by the SHA-256 of
// their bytes; maps and saves are stored decompressed, so their hash is the
// hash every client computes over the bytes it loads. The agent reaches blobs
// only through platform-api, with the lease of the job that needs them
// (HttpJobBlobs); tests use a local store.
import { GzipError, gunzipBounded, isGzip, sha256Hex } from '@glob2/core';
import { EngineInputError } from './engineCli.ts';
import type { PlatformClient } from './platform.ts';

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

/** What a running job may do with blobs. */
export interface JobBlobs {
  progress?(report: AiValidationReport): Promise<void>;
  /**
   * Reads a blob by hash, refusing anything over maxBytes and anything whose
   * bytes do not hash to the name (a corrupted store must not reach the engine).
   */
  read(sha256: string, maxBytes: number): Promise<Uint8Array>;
  /** Stores bytes by content; returns the SHA-256. */
  write(bytes: Uint8Array, contentType: string, visibility?: 'public' | 'private'): Promise<string>;
}

/** Thrown by a blob read when the platform has no such blob. */
export class BlobNotFoundError extends Error {}

/** Checks bytes read from anywhere against their content address. */
export function checkBlob(sha256: string, bytes: Uint8Array): Uint8Array {
  if (sha256Hex(bytes) !== sha256) {
    // Not the job's fault and maybe transient (a partial copy): retry.
    throw new Error(`blob ${sha256} does not match its hash`);
  }
  return bytes;
}

/** Blob access through platform-api with one job's lease. */
export class HttpJobBlobs implements JobBlobs {
  private readonly client: PlatformClient;
  private readonly leaseToken: string;

  constructor(client: PlatformClient, leaseToken: string) {
    this.client = client;
    this.leaseToken = leaseToken;
  }

  progress(report: AiValidationReport): Promise<void> {
    return this.client.aiProgress(this.leaseToken, report);
  }

  async read(sha256: string, maxBytes: number): Promise<Uint8Array> {
    try {
      return checkBlob(sha256, await this.client.readBlob(this.leaseToken, sha256, maxBytes));
    } catch (error) {
      if (error instanceof BlobNotFoundError)
        throw new EngineInputError(`blob ${sha256} not found`);
      throw error;
    }
  }

  write(
    bytes: Uint8Array,
    contentType: string,
    visibility: 'public' | 'private' = 'private',
  ): Promise<string> {
    return this.client.writeBlob(this.leaseToken, bytes, contentType, visibility);
  }
}
