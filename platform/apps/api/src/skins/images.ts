import sharp, { type OutputInfo } from 'sharp';
import { COLONY_SKIN_MATERIALS } from '@glob2/protocol';
import { apiError } from '../errors.ts';

/** Layout colony-v2: 512x512 images of four 256x256 model quadrants. */
export const SKIN_ATLAS_SIZE = 512;
/** Material ids are 0 to this exclusive bound, in COLONY_SKIN_MATERIALS order. */
export const SKIN_MATERIAL_COUNT = COLONY_SKIN_MATERIALS.length;

function decodeUpload(encoded: string, maxBytes: number, what: string): Buffer {
  const bytes = Buffer.from(encoded, 'base64');
  if (bytes.length > maxBytes || bytes.toString('base64') !== encoded) {
    throw apiError(
      'bad_request',
      `Use a PNG or WebP ${what} no larger than ${Math.round(maxBytes / 1024)} KiB.`,
    );
  }
  const png = bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]));
  const webp =
    bytes.toString('ascii', 0, 4) === 'RIFF' && bytes.toString('ascii', 8, 12) === 'WEBP';
  if (!png && !webp) throw apiError('bad_request', `Use a PNG or WebP ${what}.`);
  return bytes;
}

async function openStill(bytes: Buffer) {
  const input = sharp(bytes, {
    failOn: 'warning',
    limitInputPixels: SKIN_ATLAS_SIZE * SKIN_ATLAS_SIZE,
  });
  const meta = await input.metadata();
  if (meta.width !== SKIN_ATLAS_SIZE || meta.height !== SKIN_ATLAS_SIZE || (meta.pages ?? 1) !== 1)
    throw new Error('dimensions');
  return input;
}

/** The colour atlas, re-encoded as an opaque sRGB WebP. */
export async function canonicalSkinImage(encoded: string): Promise<Buffer> {
  const bytes = decodeUpload(encoded, 1048576, 'image');
  try {
    const input = await openStill(bytes);
    // Re-encode decoded pixels: discard metadata/animation and prevent invisible units.
    return await input
      .flatten({ background: '#ffffff' })
      .toColourspace('srgb')
      .webp({ lossless: true, effort: 4 })
      .toBuffer();
  } catch {
    throw apiError('bad_request', 'Use a valid, still 512 by 512 pixel image.');
  }
}

/** The material map, validated and re-encoded as a lossless WebP of material ids. */
export async function canonicalMaterialMap(encoded: string): Promise<Buffer> {
  const bytes = decodeUpload(encoded, 262144, 'material map');
  const invalid = () =>
    apiError('bad_request', 'Use a valid, still 512 by 512 pixel 8-bit material map.');
  let raw: { data: Buffer; info: OutputInfo };
  try {
    raw = await (await openStill(bytes)).raw().toBuffer({ resolveWithObject: true });
  } catch {
    throw invalid();
  }
  const { data, info } = raw;
  const channels = info.channels;
  const pixels = info.width * info.height;
  // Rejects 16-bit input too: its raw buffer holds two bytes per sample.
  if (info.width !== SKIN_ATLAS_SIZE || info.height !== SKIN_ATLAS_SIZE) throw invalid();
  if (channels < 1 || channels > 4 || data.length !== pixels * channels) throw invalid();
  // Channels: 1 grey, 2 grey and alpha, 3 RGB, 4 RGBA.
  const rgb = channels >= 3;
  const alpha = channels === 2 || channels === 4;
  const ids = Buffer.alloc(pixels);
  for (let i = 0; i < pixels; i++) {
    const at = i * channels;
    const value = data[at] ?? 0;
    if (rgb && (data[at + 1] !== value || data[at + 2] !== value))
      throw apiError(
        'bad_request',
        'Material map pixels must be grey: red, green and blue equal to the material id.',
      );
    if (alpha && data[at + channels - 1] !== 255)
      throw apiError('bad_request', 'Material map pixels must be fully opaque.');
    if (value >= SKIN_MATERIAL_COUNT)
      throw apiError(
        'bad_request',
        `Material map pixels must be material ids 0 (${COLONY_SKIN_MATERIALS[0].key}) to ${SKIN_MATERIAL_COUNT - 1} (${COLONY_SKIN_MATERIALS.at(-1)?.key}).`,
      );
    ids[i] = value;
  }
  return sharp(ids, { raw: { width: info.width, height: info.height, channels: 1 } })
    .toColourspace('b-w')
    .webp({ lossless: true, effort: 4 })
    .toBuffer();
}
