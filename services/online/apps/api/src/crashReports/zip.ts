// A bounded reader for report bundles: zip archives the game writes with stored
// (method 0) entries, also accepting deflate (method 8) from other tools. It
// reads the central directory only, refuses anything unusual (encryption, zip64,
// spanned archives, overlapping or out-of-range entries, unsafe names) and
// never inflates past the declared size, so a hostile bundle cannot exhaust
// memory or disk.
import { inflateRawSync } from 'node:zlib';

export interface ZipEntry {
  name: string;
  method: number;
  compressedSize: number;
  size: number;
  offset: number;
}

export class ZipError extends Error {}

/** Entry names: one flat level of short, plain file names. */
const NAME = /^[a-z0-9][a-z0-9._-]{0,63}$/;
const EOCD = 0x06054b50;
const CENTRAL = 0x02014b50;
const LOCAL = 0x04034b50;

export const MAX_ENTRIES = 16;

/** Lists the entries of a bundle, or throws ZipError. */
export function listEntries(bytes: Buffer, maxTotal: number): ZipEntry[] {
  // The end record is 22 bytes plus a comment of at most 65535 bytes; the game
  // writes no comment, so look for it only at the very end.
  if (bytes.length < 22) throw new ZipError('not a zip archive');
  const end = bytes.length - 22;
  if (bytes.readUInt32LE(end) !== EOCD) throw new ZipError('not a zip archive or has a comment');
  if (bytes.readUInt16LE(end + 4) !== 0 || bytes.readUInt16LE(end + 6) !== 0)
    throw new ZipError('spanned archives are not supported');
  const count = bytes.readUInt16LE(end + 10);
  if (count !== bytes.readUInt16LE(end + 8)) throw new ZipError('inconsistent entry count');
  if (count === 0 || count > MAX_ENTRIES) throw new ZipError('too many or no entries');
  const directorySize = bytes.readUInt32LE(end + 12);
  let at = bytes.readUInt32LE(end + 16);
  if (at + directorySize !== end) throw new ZipError('central directory out of place');
  const entries: ZipEntry[] = [];
  const names = new Set<string>();
  let total = 0;
  for (let i = 0; i < count; i++) {
    if (at + 46 > end || bytes.readUInt32LE(at) !== CENTRAL)
      throw new ZipError('bad central directory');
    const flags = bytes.readUInt16LE(at + 8);
    const method = bytes.readUInt16LE(at + 10);
    const compressedSize = bytes.readUInt32LE(at + 20);
    const size = bytes.readUInt32LE(at + 24);
    const nameLength = bytes.readUInt16LE(at + 28);
    const extraLength = bytes.readUInt16LE(at + 30);
    const commentLength = bytes.readUInt16LE(at + 32);
    const offset = bytes.readUInt32LE(at + 42);
    if (flags & 0x0001) throw new ZipError('encrypted entries are not supported');
    if (method !== 0 && method !== 8) throw new ZipError('unsupported compression');
    if (method === 0 && compressedSize !== size) throw new ZipError('stored entry size mismatch');
    const name = bytes.toString('utf8', at + 46, at + 46 + nameLength);
    if (!NAME.test(name) || names.has(name)) throw new ZipError('bad or repeated entry name');
    names.add(name);
    total += size;
    if (total > maxTotal) throw new ZipError('bundle too large when unpacked');
    entries.push({ name, method, compressedSize, size, offset });
    at += 46 + nameLength + extraLength + commentLength;
  }
  if (at !== end) throw new ZipError('bad central directory');
  // Every entry's data lies before the central directory, and no two overlap.
  const directoryStart = end - directorySize;
  const spans = entries.map((entry) => {
    const start = entry.offset;
    if (start + 30 > directoryStart || bytes.readUInt32LE(start) !== LOCAL)
      throw new ZipError('bad local header');
    const dataStart = start + 30 + bytes.readUInt16LE(start + 26) + bytes.readUInt16LE(start + 28);
    const dataEnd = dataStart + entry.compressedSize;
    if (dataEnd > directoryStart) throw new ZipError('entry out of range');
    return [start, dataEnd] as const;
  });
  spans.sort((a, b) => a[0] - b[0]);
  let previousEnd = 0;
  for (const [start, dataEnd] of spans) {
    if (start < previousEnd) throw new ZipError('overlapping entries');
    previousEnd = dataEnd;
  }
  return entries;
}

/** The bytes of one entry listed by listEntries. */
export function readEntry(bytes: Buffer, entry: ZipEntry): Buffer {
  const start = entry.offset;
  const dataStart = start + 30 + bytes.readUInt16LE(start + 26) + bytes.readUInt16LE(start + 28);
  const data = bytes.subarray(dataStart, dataStart + entry.compressedSize);
  if (entry.method === 0) return data;
  try {
    const out = inflateRawSync(data, { maxOutputLength: Math.max(1, entry.size) });
    if (out.length !== entry.size) throw new ZipError('entry size mismatch');
    return out;
  } catch (error) {
    if (error instanceof ZipError) throw error;
    throw new ZipError('corrupt entry');
  }
}
