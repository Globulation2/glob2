import { createHash } from 'node:crypto';
import { inflateRawSync } from 'node:zlib';
import {
  BUILDING_PACKAGE_LIMITS,
  checkBuildingPackage,
  type BuildingPackage,
} from '../buildings.ts';

export const buildingAssetHash = (bytes: Uint8Array): string =>
  createHash('sha256').update(bytes).digest('hex');

/** Deterministic JavaScript JSON with sorted object keys; preserve these exact bytes across runtimes. */
export function canonicalBuildingJson(value: unknown): string {
  if (Array.isArray(value)) return '[' + value.map(canonicalBuildingJson).join(',') + ']';
  if (value && typeof value === 'object')
    return (
      '{' +
      Object.entries(value)
        .sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0))
        .map(([key, v]) => JSON.stringify(key) + ':' + canonicalBuildingJson(v))
        .join(',') +
      '}'
    );
  const encoded = JSON.stringify(value);
  if (encoded === undefined) throw new Error('Not JSON');
  return encoded;
}

/** Reject ambiguous duplicate keys before JSON.parse can discard them. */
export function parseBuildingJson(text: string): unknown {
  if (Buffer.byteLength(text) > BUILDING_PACKAGE_LIMITS.manifestBytes)
    throw new Error('Building manifest exceeds 8 MiB');
  let at = 0;
  const white = () => {
    while (/[\t\n\r ]/.test(text[at] ?? 'x')) at++;
  };
  const string = (): string => {
    const start = at++;
    while (at < text.length) {
      if (text[at] === '\\') {
        at += 2;
        continue;
      }
      if (text[at++] === '"') return JSON.parse(text.slice(start, at)) as string;
    }
    throw new Error('Unterminated JSON string');
  };
  const primitive = /(?:true|false|null|-?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?)/y;
  const value = (depth: number): void => {
    if (depth > 64) throw new Error('Building JSON nesting exceeds 64');
    white();
    const opener = text[at];
    if (opener === '"') {
      string();
      return;
    }
    if (opener === '{' || opener === '[') {
      at++;
      const close = opener === '{' ? '}' : ']';
      const keys = new Set<string>();
      white();
      if (text[at] === close) {
        at++;
        return;
      }
      for (;;) {
        white();
        if (opener === '{') {
          if (text[at] !== '"') throw new Error('Expected JSON object key');
          const key = string();
          if (keys.has(key)) throw new Error(`Duplicate JSON key: ${key}`);
          keys.add(key);
          white();
          if (text[at++] !== ':') throw new Error('Expected JSON colon');
        }
        value(depth + 1);
        white();
        if (text[at] === close) {
          at++;
          return;
        }
        if (text[at++] !== ',') throw new Error('Expected JSON comma');
      }
    }
    primitive.lastIndex = at;
    const token = primitive.exec(text);
    if (!token) throw new Error('Invalid JSON value');
    at = primitive.lastIndex;
  };
  value(0);
  white();
  if (at !== text.length) throw new Error('Trailing JSON data');
  return JSON.parse(text) as unknown;
}

const crcTable = Uint32Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let i = 0; i < 8; i++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of bytes) crc = (crcTable[(crc ^ byte) & 255] ?? 0) ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}
const assetName = /^assets\/([0-9a-f]{64})\.(png|webp)$/;
function safeName(name: string): void {
  if (name !== 'manifest.json' && !assetName.test(name))
    throw new Error('Unsafe building archive member');
}

/** ZIP32 only. All members stay in memory; nothing is extracted to the filesystem. */
export function readBuildingArchive(bytes: Buffer): {
  package: BuildingPackage;
  assets: Map<string, Buffer>;
} {
  if (bytes.length > BUILDING_PACKAGE_LIMITS.uploadBytes)
    throw new Error('Building archive exceeds 32 MiB');
  if (bytes.length < 22) throw new Error('Invalid building ZIP');
  let end = bytes.length - 22;
  const minimum = Math.max(0, bytes.length - 22 - 65535);
  while (
    end >= minimum &&
    (bytes.readUInt32LE(end) !== 0x06054b50 ||
      end + 22 + bytes.readUInt16LE(end + 20) !== bytes.length)
  )
    end--;
  if (end < minimum) throw new Error('Invalid ZIP directory');
  if (bytes.readUInt16LE(end + 4) || bytes.readUInt16LE(end + 6))
    throw new Error('Multi-disk ZIP is unsupported');
  const count = bytes.readUInt16LE(end + 10);
  if (!count || count > BUILDING_PACKAGE_LIMITS.images + 1 || count !== bytes.readUInt16LE(end + 8))
    throw new Error('Invalid ZIP member count');
  const directory = bytes.readUInt32LE(end + 16),
    directorySize = bytes.readUInt32LE(end + 12);
  if (directory + directorySize !== end) throw new Error('Invalid ZIP directory bounds');
  let at = directory,
    expanded = 0;
  const entries = new Map<string, Buffer>();
  const ranges: [number, number][] = [];
  for (let i = 0; i < count; i++) {
    if (at + 46 > end || bytes.readUInt32LE(at) !== 0x02014b50)
      throw new Error('Invalid ZIP member');
    const flags = bytes.readUInt16LE(at + 8),
      method = bytes.readUInt16LE(at + 10);
    if (flags & ~0x808 || (method !== 0 && method !== 8))
      throw new Error('Unsupported ZIP encoding');
    const crc = bytes.readUInt32LE(at + 16),
      compressed = bytes.readUInt32LE(at + 20),
      size = bytes.readUInt32LE(at + 24);
    const nameLength = bytes.readUInt16LE(at + 28),
      extra = bytes.readUInt16LE(at + 30),
      comment = bytes.readUInt16LE(at + 32);
    const next = at + 46 + nameLength + extra + comment;
    if (next > end || bytes.readUInt16LE(at + 34)) throw new Error('Invalid ZIP member bounds');
    const mode = bytes.readUInt32LE(at + 38) >>> 16;
    if ((mode & 0xf000) !== 0 && (mode & 0xf000) !== 0x8000)
      throw new Error('ZIP member is not a regular file');
    const name = bytes.toString('utf8', at + 46, at + 46 + nameLength);
    safeName(name);
    if (entries.has(name)) throw new Error('Duplicate ZIP member');
    expanded += size;
    if (
      !size ||
      size > BUILDING_PACKAGE_LIMITS.uploadBytes ||
      expanded > BUILDING_PACKAGE_LIMITS.uploadBytes
    )
      throw new Error('Expanded ZIP exceeds 32 MiB');
    const local = bytes.readUInt32LE(at + 42);
    if (
      local + 30 > directory ||
      bytes.readUInt32LE(local) !== 0x04034b50 ||
      bytes.readUInt16LE(local + 6) !== flags ||
      bytes.readUInt16LE(local + 8) !== method
    )
      throw new Error('Invalid local ZIP header');
    const localName = bytes.readUInt16LE(local + 26),
      localExtra = bytes.readUInt16LE(local + 28);
    const start = local + 30 + localName + localExtra;
    if (
      start + compressed > directory ||
      bytes.toString('utf8', local + 30, local + 30 + localName) !== name
    )
      throw new Error('Invalid local ZIP name or bounds');
    let finish = start + compressed;
    if (flags & 8) {
      if (finish + 12 > directory) throw new Error('Invalid ZIP descriptor');
      if (bytes.readUInt32LE(finish) === 0x08074b50) finish += 4;
      if (
        finish + 12 > directory ||
        bytes.readUInt32LE(finish) !== crc ||
        bytes.readUInt32LE(finish + 4) !== compressed ||
        bytes.readUInt32LE(finish + 8) !== size
      )
        throw new Error('Invalid ZIP descriptor');
      finish += 12;
    } else if (
      bytes.readUInt32LE(local + 14) !== crc ||
      bytes.readUInt32LE(local + 18) !== compressed ||
      bytes.readUInt32LE(local + 22) !== size
    )
      throw new Error('ZIP headers disagree');
    ranges.push([local, finish]);
    const source = bytes.subarray(start, start + compressed);
    const content =
      method === 0 ? Buffer.from(source) : inflateRawSync(source, { maxOutputLength: size });
    if (content.length !== size || crc32(content) !== crc)
      throw new Error('ZIP content checksum mismatch');
    entries.set(name, content);
    at = next;
  }
  if (at !== end) throw new Error('Unexpected ZIP directory data');
  ranges.sort(([a], [b]) => a - b);
  let offset = 0;
  for (const [start, finish] of ranges) {
    if (start !== offset) throw new Error('Overlapping or undeclared ZIP data');
    offset = finish;
  }
  if (offset !== directory) throw new Error('Undeclared ZIP data');
  const manifest = entries.get('manifest.json');
  if (!manifest) throw new Error('Missing building manifest');
  const pkg = checkBuildingPackage(
    parseBuildingJson(new TextDecoder('utf-8', { fatal: true }).decode(manifest)),
  );
  const required = buildingPackageAssetHashes(pkg);
  const assets = new Map<string, Buffer>();
  for (const [name, content] of entries) {
    if (name === 'manifest.json') continue;
    const hash = assetName.exec(name)?.[1];
    if (!hash || !required.has(hash) || assets.has(hash))
      throw new Error('Undeclared or duplicate building asset');
    if (buildingAssetHash(content) !== hash) throw new Error('Building image hash mismatch');
    assets.set(hash, content);
  }
  if (assets.size !== required.size) throw new Error('Missing building image');
  return { package: pkg, assets };
}

export function buildingPackageAssetHashes(pkg: BuildingPackage): Set<string> {
  return new Set(
    pkg.sprites.flatMap((s) =>
      s.frames.flatMap((f) => (f.teamColorHash ? [f.imageHash, f.teamColorHash] : [f.imageHash])),
    ),
  );
}

/** Deterministic stored ZIP: fixed timestamps, no extras or platform-dependent attributes. */
export function writeBuildingArchive(value: unknown, assets: ReadonlyMap<string, Buffer>): Buffer {
  const pkg = checkBuildingPackage(value),
    required = buildingPackageAssetHashes(pkg);
  if (assets.size !== required.size)
    throw new Error('Building asset set does not match the manifest');
  const files: [string, Buffer][] = [['manifest.json', Buffer.from(canonicalBuildingJson(pkg))]];
  for (const hash of [...required].sort()) {
    const bytes = assets.get(hash);
    if (!bytes || buildingAssetHash(bytes) !== hash)
      throw new Error('Missing or corrupt building asset');
    const png = bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
    const webp =
      bytes.toString('ascii', 0, 4) === 'RIFF' && bytes.toString('ascii', 8, 12) === 'WEBP';
    if (!png && !webp) throw new Error('Building assets must be PNG or WebP');
    files.push([`assets/${hash}.${png ? 'png' : 'webp'}`, bytes]);
  }
  const parts: Buffer[] = [],
    records: Buffer[] = [];
  let offset = 0;
  for (const [name, content] of files) {
    const encoded = Buffer.from(name),
      crc = crc32(content),
      header = Buffer.alloc(30);
    header.writeUInt32LE(0x04034b50);
    header.writeUInt16LE(20, 4);
    header.writeUInt16LE(0x21, 12); // 1980-01-01, independent of local timezone
    header.writeUInt32LE(crc, 14);
    header.writeUInt32LE(content.length, 18);
    header.writeUInt32LE(content.length, 22);
    header.writeUInt16LE(encoded.length, 26);
    parts.push(header, encoded, content);
    const record = Buffer.alloc(46);
    record.writeUInt32LE(0x02014b50);
    record.writeUInt16LE(20, 4);
    record.writeUInt16LE(20, 6);
    record.writeUInt16LE(0x21, 14);
    record.writeUInt32LE(crc, 16);
    record.writeUInt32LE(content.length, 20);
    record.writeUInt32LE(content.length, 24);
    record.writeUInt16LE(encoded.length, 28);
    record.writeUInt32LE(offset, 42);
    records.push(record, encoded);
    offset += header.length + encoded.length + content.length;
    if (offset > BUILDING_PACKAGE_LIMITS.uploadBytes)
      throw new Error('Building archive exceeds 32 MiB');
  }
  const directory = Buffer.concat(records),
    end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50);
  end.writeUInt16LE(files.length, 8);
  end.writeUInt16LE(files.length, 10);
  end.writeUInt32LE(directory.length, 12);
  end.writeUInt32LE(offset, 16);
  if (offset + directory.length + end.length > BUILDING_PACKAGE_LIMITS.uploadBytes)
    throw new Error('Building archive exceeds 32 MiB');
  return Buffer.concat([...parts, directory, end]);
}

/** GameHeader's portable artwork section (G2BA0001, little-endian lengths). */
export function writeBuildingArtworkBundle(
  values: readonly BuildingPackage[],
  assets: ReadonlyMap<string, Buffer>,
): Buffer {
  const sprites = new Map<string, BuildingPackage['sprites'][number]>();
  const required = new Set<string>();
  let decoded = 0;
  for (const value of values) {
    const pkg = checkBuildingPackage(value);
    for (const sprite of pkg.sprites) {
      const key = buildingAssetHash(Buffer.from(canonicalBuildingJson(sprite)));
      if (sprites.has(key)) continue;
      sprites.set(key, sprite);
      for (const frame of sprite.frames) {
        required.add(frame.imageHash);
        if (frame.teamColorHash) required.add(frame.teamColorHash);
        // Identical descriptors share a runtime sprite. Distinct frames and
        // team layers still allocate pixels even when their image hashes match.
        decoded += frame.width * frame.height * 4 * (frame.teamColorHash ? 2 : 1);
      }
    }
  }
  if (decoded > BUILDING_PACKAGE_LIMITS.decodedBytes || sprites.size > 4096 || required.size > 4096)
    throw new Error('Composed building artwork exceeds its limits');
  if (assets.size !== required.size)
    throw new Error('Artwork bundle asset set differs from manifest');
  if (!sprites.size) return Buffer.alloc(0);
  const manifest = Buffer.from(
    canonicalBuildingJson(
      [...sprites.entries()]
        .sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0))
        .map(([, sprite]) => sprite),
    ),
  );
  if (manifest.length > BUILDING_PACKAGE_LIMITS.manifestBytes)
    throw new Error('Artwork manifest exceeds 8 MiB');
  const header = Buffer.alloc(12);
  header.write('G2BA0001');
  header.writeUInt32LE(manifest.length, 8);
  const count = Buffer.alloc(4);
  count.writeUInt32LE(required.size);
  const parts: Buffer[] = [header, manifest, count];
  let size = header.length + manifest.length + count.length;
  for (const hash of [...required].sort()) {
    const image = assets.get(hash);
    if (
      !image ||
      buildingAssetHash(image) !== hash ||
      image.length > BUILDING_PACKAGE_LIMITS.uploadBytes ||
      image.toString('ascii', 0, 4) !== 'RIFF' ||
      image.toString('ascii', 8, 12) !== 'WEBP'
    )
      throw new Error('Missing or invalid normalized artwork');
    const length = Buffer.alloc(4);
    length.writeUInt32LE(image.length);
    parts.push(Buffer.from(hash, 'ascii'), length, image);
    size += 68 + image.length;
    if (size > 72 * 1024 * 1024) throw new Error('Artwork bundle exceeds 72 MiB');
  }
  return Buffer.concat(parts);
}
