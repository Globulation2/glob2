import sharp from 'sharp';
import { apiError } from '../errors.ts';

export async function canonicalSkinImage(encoded: string): Promise<Buffer> {
  const bytes = Buffer.from(encoded, 'base64');
  if (bytes.length > 262144 || bytes.toString('base64') !== encoded) {
    throw apiError('bad_request', 'Use a PNG or WebP image no larger than 256 KiB.');
  }
  const png = bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
  const webp =
    bytes.toString('ascii', 0, 4) === 'RIFF' && bytes.toString('ascii', 8, 12) === 'WEBP';
  if (!png && !webp) throw apiError('bad_request', 'Use a PNG or WebP image.');
  try {
    const input = sharp(bytes, { failOn: 'warning', limitInputPixels: 65536 });
    const meta = await input.metadata();
    if (meta.width !== 256 || meta.height !== 256 || (meta.pages ?? 1) !== 1)
      throw new Error('dimensions');
    // Re-encode decoded pixels: discard metadata/animation and prevent invisible units.
    return await input
      .flatten({ background: '#ffffff' })
      .toColourspace('srgb')
      .png({ compressionLevel: 9 })
      .toBuffer();
  } catch {
    throw apiError('bad_request', 'Use a valid, still 256 by 256 pixel image.');
  }
}
