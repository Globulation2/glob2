import { describe, expect, it } from 'vitest';
import sharp from 'sharp';
import { buildingNamespacePrefix } from '@glob2/protocol';
import { buildingAssetHash, writeBuildingArtworkBundle } from '@glob2/protocol/node';
import { normalizeBuildingArtwork } from '../src/buildings/images.ts';
const namespace = '11111111-1111-4111-8111-111111111111';
function fixture(hash: string, width = 16, height = 16) {
  return {
    schemaVersion: 1 as const,
    namespace,
    experiments: [],
    variants: [
      {
        key: buildingNamespacePrefix(namespace) + 'kitchen',
        properties: { gameSprite: 'package:kitchen' },
        semantics: {},
      },
    ],
    sprites: [{ key: 'kitchen', frames: [{ imageHash: hash, width, height }] }],
  };
}
describe('building image normalization', () => {
  it('preserves alpha, removes metadata, and produces stable lossless bytes', async () => {
    const image = await sharp({
      create: {
        width: 16,
        height: 16,
        channels: 4,
        background: { r: 120, g: 40, b: 80, alpha: 0.5 },
      },
    })
      .png()
      .toBuffer();
    const hash = buildingAssetHash(image),
      source = fixture(hash);
    const a = await normalizeBuildingArtwork(source, new Map([[hash, image]]));
    const b = await normalizeBuildingArtwork(source, new Map([[hash, image]]));
    expect(a.package).toEqual(b.package);
    expect(source.sprites[0]?.frames[0]?.imageHash).toBe(hash);
    const frame = a.package.sprites[0]?.frames[0];
    expect(frame).toBeTruthy();
    const bytes = a.assets.get(frame!.imageHash)!;
    expect(bytes).toEqual(b.assets.get(frame!.imageHash));
    const info = await sharp(bytes).metadata();
    expect(info.format).toBe('webp');
    expect(info.hasAlpha).toBe(true);
    expect(info.exif).toBeUndefined();
    expect(await sharp(bytes).raw().toBuffer()).toEqual(await sharp(image).raw().toBuffer());
    const normalized = await normalizeBuildingArtwork(a.package, a.assets);
    expect(normalized.package).toEqual(a.package);
  });
  it('writes a self-contained content-addressed bundle after normalization', async () => {
    const image = await sharp({
      create: { width: 16, height: 16, channels: 4, background: '#ffffff' },
    })
      .png()
      .toBuffer();
    const hash = buildingAssetHash(image);
    const normalized = await normalizeBuildingArtwork(fixture(hash), new Map([[hash, image]]));
    const bundle = writeBuildingArtworkBundle([normalized.package], normalized.assets);
    expect(bundle.toString('ascii', 0, 8)).toBe('G2BA0001');
    expect(writeBuildingArtworkBundle([normalized.package], normalized.assets)).toEqual(bundle);
    expect(
      writeBuildingArtworkBundle([normalized.package, normalized.package], normalized.assets),
    ).toEqual(bundle);
    const manifestLength = bundle.readUInt32LE(8);
    const manifest = JSON.parse(bundle.toString('utf8', 12, 12 + manifestLength));
    expect(manifest).toEqual(normalized.package.sprites);
    expect(bundle.readUInt32LE(12 + manifestLength)).toBe(normalized.assets.size);
    expect(() => writeBuildingArtworkBundle([normalized.package], new Map())).toThrow(/asset set/);
    expect(() => writeBuildingArtworkBundle([fixture(hash)], new Map([[hash, image]]))).toThrow(
      /normalized/,
    );
  });
  it('rejects forged dimensions, corrupt bytes, wrong hashes and oversize frames', async () => {
    const image = await sharp({
      create: { width: 16, height: 16, channels: 4, background: '#ffffff' },
    })
      .png()
      .toBuffer();
    const hash = buildingAssetHash(image);
    const animation = Buffer.alloc(20);
    animation.writeUInt32BE(8);
    animation.write('acTL', 4, 'ascii');
    animation.writeUInt32BE(2, 8);
    const apng = Buffer.concat([image.subarray(0, 33), animation, image.subarray(33)]);
    const apngHash = buildingAssetHash(apng);
    await expect(
      normalizeBuildingArtwork(fixture(apngHash), new Map([[apngHash, apng]])),
    ).rejects.toThrow(/animated PNG/);
    await expect(
      normalizeBuildingArtwork(fixture(hash, 15), new Map([[hash, image]])),
    ).rejects.toThrow(/dimensions/);
    await expect(
      normalizeBuildingArtwork(fixture(hash), new Map([[hash, Buffer.from('broken')]])),
    ).rejects.toThrow(/corrupt/);
    const huge = await sharp({
      create: { width: 513, height: 16, channels: 4, background: '#ffffff' },
    })
      .png()
      .toBuffer();
    const bigHash = buildingAssetHash(huge);
    await expect(
      normalizeBuildingArtwork(fixture(bigHash), new Map([[bigHash, huge]])),
    ).rejects.toThrow(/512/);
    const bad = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10, 0]);
    const badHash = buildingAssetHash(bad);
    await expect(
      normalizeBuildingArtwork(fixture(badHash), new Map([[badHash, bad]])),
    ).rejects.toThrow();
  });
});
