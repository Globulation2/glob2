import sharp from 'sharp';
import { apiError } from '../errors.ts';

export const MAX_BYTES = 10 * 1024 * 1024;
export async function canonicalAvatar(bytes: Buffer): Promise<Buffer> {
  if (!bytes.length || bytes.length > MAX_BYTES)
    throw apiError('bad_request', 'Choose a photo no larger than 10 MiB.');
  try {
    const input = sharp(bytes, { limitInputPixels: 25_000_000, failOn: 'warning' });
    const meta = await input.metadata();
    if (!['jpeg', 'png', 'webp'].includes(meta.format ?? '') || (meta.pages ?? 1) !== 1)
      throw new Error('format');
    // libvips can read just the first frame of APNG without reporting pages.
    if (meta.format === 'png') {
      for (let at = 8; at + 12 <= bytes.length;) {
        if (bytes.toString('ascii', at + 4, at + 8) === 'acTL') throw new Error('animation');
        at += 12 + bytes.readUInt32BE(at);
      }
    }
    return await input
      .rotate()
      .resize(512, 512, { fit: 'cover' })
      .toColourspace('srgb')
      .webp({ quality: 85 })
      .toBuffer();
  } catch {
    throw apiError('bad_request', 'Choose a still JPEG, PNG or WebP photo, at most 25 megapixels.');
  }
}
