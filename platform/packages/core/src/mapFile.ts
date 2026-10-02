// Map and save files as players have them: the game writes maps gzip-compressed
// (maps/*.map.gz, the editor's saves) or plain, and loads either. The platform
// stores, hashes and validates the decompressed bytes, because that is what
// every client hashes after loading a file. These checks run before anything
// is stored, so a file that is obviously not a map gets a plain-language
// answer at once instead of a catalog entry that turns invalid later.
import { gunzipSync } from 'node:zlib';

/** Largest decompressed map or save the platform accepts (the engine agent's default limit). */
export const MAX_DECOMPRESSED_MAP_BYTES = 64 * 1024 * 1024;

/** VERSION_MAJOR and MINIMUM_VERSION_MINOR in src/Version.h: older files no longer load. */
export const MAP_VERSION_MAJOR = 0;
export const MINIMUM_MAP_VERSION_MINOR = 58;

export interface MapHeader {
  name: string;
  versionMajor: number;
  versionMinor: number;
  teamCount: number;
  savedGame: boolean;
}

export function isGzip(bytes: Uint8Array): boolean {
  return bytes.length >= 2 && bytes[0] === 0x1f && bytes[1] === 0x8b;
}

/** Why gunzipBounded failed. */
export class GzipError extends Error {
  readonly tooLarge: boolean;
  constructor(message: string, tooLarge: boolean) {
    super(message);
    this.tooLarge = tooLarge;
  }
}

/** Decompresses gzip data, refusing output over maxBytes. */
export function gunzipBounded(bytes: Uint8Array, maxBytes: number): Buffer {
  try {
    return gunzipSync(bytes, { maxOutputLength: maxBytes });
  } catch (error) {
    const code = (error as NodeJS.ErrnoException).code;
    if (code === 'ERR_BUFFER_TOO_LARGE' || /larger than/i.test((error as Error).message)) {
      throw new GzipError(`decompressed file exceeds ${maxBytes} bytes`, true);
    }
    throw new GzipError(`corrupt gzip data: ${(error as Error).message}`, false);
  }
}

/**
 * Reads the first fields of a decompressed map or save (MapHeader::loadFields
 * in src/map/io/MapHeader.cpp): name (u32 length + bytes), versionMajor,
 * versionMinor, numberOfTeams (big-endian s32), mapOffset (u32), isSavedGame
 * (u8). Returns undefined when the bytes cannot be a header. It does not prove
 * the engine can load the file; only the engine agent's check does that.
 */
export function readMapHeader(bytes: Uint8Array): MapHeader | undefined {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (bytes.byteLength < 4) return undefined;
  const nameLength = view.getUint32(0);
  const fixed = 4 + nameLength;
  if (nameLength > 1024 || bytes.byteLength < fixed + 17) return undefined;
  const name = new TextDecoder('utf-8', { fatal: false }).decode(bytes.subarray(4, fixed));
  const saved = view.getUint8(fixed + 16);
  if (saved > 1) return undefined;
  return {
    name,
    versionMajor: view.getInt32(fixed),
    versionMinor: view.getInt32(fixed + 4),
    teamCount: view.getInt32(fixed + 8),
    savedGame: saved === 1,
  };
}

export type MapFileProblem =
  | 'empty'
  | 'too_large'
  | 'corrupt_gzip'
  | 'not_a_map'
  | 'newer_version'
  | 'older_version'
  | 'save_not_map'
  | 'map_not_save';

export type MapFileCheck =
  | { ok: true; bytes: Buffer; header: MapHeader; compressed: boolean }
  | { ok: false; problem: MapFileProblem; message: string };

/** "64 MB", "1.5 MB", "300 KB", "200 bytes". */
export function readableSize(bytes: number): string {
  if (bytes >= 1024 * 1024) {
    const mb = bytes / (1024 * 1024);
    return `${Number.isInteger(mb) ? mb : mb.toFixed(1)} MB`;
  }
  if (bytes >= 1024) return `${Math.round(bytes / 1024)} KB`;
  return `${bytes} bytes`;
}

/**
 * Decompresses (when gzip) and checks an uploaded map or save before it is
 * stored. `newestVersionMinor` is the newest file format the engines this
 * instance runs can read.
 */
export function checkMapFile(
  input: Uint8Array,
  options: {
    format: 'map' | 'save';
    newestVersionMinor: number;
    maxBytes?: number;
  },
): MapFileCheck {
  const maxBytes = options.maxBytes ?? MAX_DECOMPRESSED_MAP_BYTES;
  const what = options.format === 'save' ? 'saved game' : 'map';
  if (input.byteLength === 0) {
    return { ok: false, problem: 'empty', message: 'This file is empty.' };
  }
  let bytes = Buffer.from(input.buffer, input.byteOffset, input.byteLength);
  const compressed = isGzip(bytes);
  if (compressed) {
    try {
      bytes = gunzipBounded(bytes, maxBytes);
    } catch (error) {
      if (error instanceof GzipError && error.tooLarge) {
        return {
          ok: false,
          problem: 'too_large',
          message: `This ${what} is too big: unpacked, it is over the ${readableSize(maxBytes)} limit.`,
        };
      }
      return {
        ok: false,
        problem: 'corrupt_gzip',
        message: `This file is damaged: it is compressed, but it could not be unpacked. Save the ${what} again in the game and upload the new file.`,
      };
    }
  } else if (bytes.byteLength > maxBytes) {
    return {
      ok: false,
      problem: 'too_large',
      message: `This ${what} is too big: the limit is ${readableSize(maxBytes)}.`,
    };
  }
  const header = readMapHeader(bytes);
  if (!header || header.versionMajor !== MAP_VERSION_MAJOR || header.teamCount < 0) {
    return {
      ok: false,
      problem: 'not_a_map',
      message: `This file isn't a Globulation 2 ${what}. Upload a .map or .map.gz file saved by the game or its map editor.`,
    };
  }
  if (header.versionMinor < MINIMUM_MAP_VERSION_MINOR) {
    return {
      ok: false,
      problem: 'older_version',
      message: `This ${what} was made with a very old version of Globulation 2 that the game can no longer load.`,
    };
  }
  if (header.versionMinor > options.newestVersionMinor) {
    return {
      ok: false,
      problem: 'newer_version',
      message: `This ${what} was made with a newer version of Globulation 2 than this server runs, so it can't be checked or played here yet.`,
    };
  }
  if (options.format === 'map' && header.savedGame) {
    return {
      ok: false,
      problem: 'save_not_map',
      message: 'This file is a saved game, not a map. Upload a map from the map editor instead.',
    };
  }
  if (options.format === 'save' && !header.savedGame) {
    return {
      ok: false,
      problem: 'map_not_save',
      message: 'This file is a map, not a saved game.',
    };
  }
  return { ok: true, bytes, header, compressed };
}
