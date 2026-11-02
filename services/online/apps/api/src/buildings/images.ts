import sharp from 'sharp';
import {
  BUILDING_PACKAGE_LIMITS,
  checkBuildingPackage,
  type BuildingPackage,
} from '@glob2/protocol';
import { buildingAssetHash, buildingPackageAssetHashes } from '@glob2/protocol/node';

/** Decode still pixels once and retain only normalized lossless image bytes. */
export async function normalizeBuildingArtwork(
  value: unknown,
  assets: ReadonlyMap<string, Buffer>,
): Promise<{ package: BuildingPackage; assets: Map<string, Buffer> }> {
  const pkg = structuredClone(checkBuildingPackage(value));
  const required = buildingPackageAssetHashes(pkg);
  if (required.size !== assets.size) throw new Error('Building assets do not match manifest');
  const dimensions = new Map<string, { width: number; height: number }>();
  for (const sprite of pkg.sprites)
    for (const frame of sprite.frames) {
      for (const hash of [frame.imageHash, frame.teamColorHash]) {
        if (!hash) continue;
        const previous = dimensions.get(hash);
        if (previous && (previous.width !== frame.width || previous.height !== frame.height))
          throw new Error('Building asset has conflicting declared dimensions');
        dimensions.set(hash, { width: frame.width, height: frame.height });
      }
    }
  let sourceBytes = 0;
  const normalized = new Map<
    string,
    { hash: string; width: number; height: number; bytes: Buffer }
  >();
  for (const hash of required) {
    const bytes = assets.get(hash);
    if (!bytes || buildingAssetHash(bytes) !== hash)
      throw new Error('Missing or corrupt building image');
    sourceBytes += bytes.length;
    if (sourceBytes > BUILDING_PACKAGE_LIMITS.uploadBytes)
      throw new Error('Building images exceed 32 MiB');
    const png = bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
    const webp =
      bytes.toString('ascii', 0, 4) === 'RIFF' && bytes.toString('ascii', 8, 12) === 'WEBP';
    if (!png && !webp) throw new Error('Use PNG or WebP building frames');
    // Some PNG decoders expose only APNG's first frame; do not silently flatten it.
    if (png) {
      let at = 8;
      while (at + 12 <= bytes.length) {
        const length = bytes.readUInt32BE(at);
        if (length > bytes.length - at - 12) throw new Error('Truncated PNG chunk');
        const kind = bytes.toString('ascii', at + 4, at + 8);
        if (kind === 'acTL')
          throw new Error('Use still building frames; animated PNG is unsupported');
        at += length + 12;
        if (kind === 'IEND') break;
      }
    }
    const image = sharp(bytes, {
      failOn: 'warning',
      limitInputPixels: BUILDING_PACKAGE_LIMITS.frameSide ** 2,
    });
    const info = await image.metadata();
    if (
      !info.width ||
      !info.height ||
      info.width > BUILDING_PACKAGE_LIMITS.frameSide ||
      info.height > BUILDING_PACKAGE_LIMITS.frameSide ||
      (info.pages ?? 1) !== 1
    )
      throw new Error('Use still building frames no larger than 512 by 512');
    const expected = dimensions.get(hash);
    if (!expected || info.width !== expected.width || info.height !== expected.height)
      throw new Error('Building frame dimensions do not match manifest');
    const canonical = await image
      .toColourspace('srgb')
      .ensureAlpha()
      .webp({ lossless: true, effort: 4 })
      .toBuffer();
    normalized.set(hash, {
      hash: buildingAssetHash(canonical),
      bytes: canonical,
      width: info.width,
      height: info.height,
    });
  }
  const output = new Map<string, Buffer>();
  for (const sprite of pkg.sprites)
    for (const frame of sprite.frames) {
      for (const field of ['imageHash', 'teamColorHash'] as const) {
        const hash = frame[field];
        if (!hash) continue;
        const image = normalized.get(hash);
        if (!image || image.width !== frame.width || image.height !== frame.height)
          throw new Error('Building frame dimensions do not match manifest');
        frame[field] = image.hash;
        output.set(image.hash, image.bytes);
      }
    }
  return { package: checkBuildingPackage(pkg), assets: output };
}
