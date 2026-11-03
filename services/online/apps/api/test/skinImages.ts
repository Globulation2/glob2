// Colony-v2 skin upload fixtures: a 512x512 colour atlas and material map.
import sharp from 'sharp';

/** A colour atlas (RGBA, so publishing must flatten it). */
export function colourAtlas(
  background: string | { r: number; g: number; b: number; alpha?: number } = '#ee224488',
): Promise<Buffer> {
  return sharp({ create: { width: 512, height: 512, channels: 4, background } })
    .png()
    .toBuffer();
}

/** A material map: material id per pixel (by quadrant by default), as an RGB(A) PNG. */
export function materialMap(
  options: {
    id?: (x: number, y: number) => number;
    channels?: 1 | 3 | 4;
    size?: number;
    /** Overrides one pixel's channel values (e.g. a non-grey or transparent pixel). */
    pixel?: { x: number; y: number; value: number[] };
  } = {},
): Promise<Buffer> {
  const size = options.size ?? 512;
  const channels = options.channels ?? 3;
  const id = options.id ?? ((x, y) => (y >= 256 ? 2 : 0) + (x >= 256 ? 1 : 0));
  const data = Buffer.alloc(size * size * channels);
  for (let y = 0; y < size; y++)
    for (let x = 0; x < size; x++) {
      const at = (y * size + x) * channels;
      const value = id(x, y);
      data.fill(value, at, at + Math.min(channels, 3));
      if (channels === 4) data[at + 3] = 255;
    }
  if (options.pixel) {
    const { x, y, value } = options.pixel;
    value.forEach((v, i) => (data[(y * size + x) * channels + i] = v));
  }
  return sharp(data, { raw: { width: size, height: size, channels } })
    .png()
    .toBuffer();
}

/** Upload fields for a valid colony-v2 skin. */
export async function skinImages() {
  return {
    imageBase64: (await colourAtlas()).toString('base64'),
    materialBase64: (await materialMap()).toString('base64'),
  };
}
