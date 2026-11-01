// Shared first step of every map and save upload (room uploads at
// /api/v1/uploads and catalog versions at /api/v1/maps/:id/versions): unpack
// gzip the way the game's loader does, check the header, and answer obvious
// problems at once in words a player understands. The engine agent's check
// still decides whether the game can really load the file.
import { checkMapFileAsync } from '@glob2/core';
import type { SimVersion } from '@glob2/protocol';
import { apiError } from '../errors.ts';

/** The newest sim version this instance serves (for uploads that name none, e.g. from the web). */
export function newestSimVersion(versions: readonly SimVersion[]): SimVersion | undefined {
  return [...versions].sort(
    (a, b) => a.versionMinor - b.versionMinor || a.netProtocol - b.netProtocol,
  )[versions.length - 1];
}

/**
 * The bytes to store and hash (decompressed), or an HTTP 400 whose message
 * says what is wrong and whose details carry a stable `problem` code. Gzip is
 * unpacked off the event loop, bounded in size and ratio; callers take the
 * sender's upload quota first.
 */
export async function checkedUpload(
  body: unknown,
  format: 'map' | 'save',
  newestVersionMinor: number,
): Promise<Buffer> {
  if (!(body instanceof Buffer) || body.length === 0) {
    throw apiError(
      'bad_request',
      'Send the file as the request body (Content-Type: application/octet-stream).',
      { problem: 'empty' },
    );
  }
  const check = await checkMapFileAsync(body, { format, newestVersionMinor });
  if (!check.ok) throw apiError('bad_request', check.message, { problem: check.problem });
  return check.bytes;
}
