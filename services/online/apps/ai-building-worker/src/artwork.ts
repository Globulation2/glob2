import sharp from 'sharp';
import { buildingAssetHash } from '@glob2/protocol/node';
import type { EntryArtwork } from '@glob2/building-studio';
export function imagePrompt(prompt: string, teamColor = false) {
  return (
    'Globulation 2 game building asset. Soft coarse painterly style, earthy palette, upper-left lighting, readable at 32-pixel tile scale. Camera: elevated approximately 45 degrees above the ground, looking diagonally downward in the same direction as the stock Glob2 building references. Use an orthographic-like game view with a clearly visible roof/top surface and foreshortened walls and ground footprint. Keep the whole building upright; the camera looks down, rather than tilting the building. No eye-level or low-angle front view, horizon, vanishing-point perspective or straight overhead view. Stock building references establish the camera angle; preserve that camera across finished and construction stages. One isolated building centered, its ground base near the bottom, transparent background, ample padding. No scenery, text, grid, checkerboard or watermark. Maintain identity and scale from reference buildings. References are visual direction only. ' +
    (teamColor
      ? ' Reserve a small pure magenta (#ff00ff) trim or banner for team color, and use no other magenta. '
      : '') +
    prompt
  );
}
/** Fit visible pixels to the engine footprint, with a consistent bottom anchor. */
export async function processArtwork(
  source: Uint8Array,
  key: string,
  width: number,
  height: number,
  teamColor = false,
): Promise<EntryArtwork> {
  if (
    !Number.isInteger(width) ||
    !Number.isInteger(height) ||
    width < 1 ||
    height < 1 ||
    width > 12 ||
    height > 12
  )
    throw Error('Generated artwork requires a footprint between 1 and 12 tiles per side.');
  const image = sharp(source, { failOn: 'warning', limitInputPixels: 2048 * 2048 });
  const metadata = await image.metadata();
  if (
    !['png', 'webp'].includes(metadata.format ?? '') ||
    (metadata.pages ?? 1) !== 1 ||
    !metadata.hasAlpha
  )
    throw Error('Building artwork must be a still transparent image.');
  const raw = await image.ensureAlpha().raw().toBuffer({ resolveWithObject: true });
  let left = raw.info.width,
    top = raw.info.height,
    right = -1,
    bottom = -1,
    transparent = false;
  for (let y = 0; y < raw.info.height; y++)
    for (let x = 0; x < raw.info.width; x++) {
      const alpha = raw.data[(y * raw.info.width + x) * 4 + 3] ?? 0;
      if (alpha < 16) transparent = true;
      if (alpha >= 16) {
        left = Math.min(left, x);
        top = Math.min(top, y);
        right = Math.max(right, x);
        bottom = Math.max(bottom, y);
      }
    }
  if (right < left || !transparent)
    throw Error('Artwork needs a visible building and a transparent background.');
  const frameWidth = width * 32,
    frameHeight = Math.min(512, height * 32 + 32);
  const cropped = await sharp(raw.data, { raw: raw.info })
    .extract({ left, top, width: right - left + 1, height: bottom - top + 1 })
    .png()
    .toBuffer();
  const fitted = await sharp(cropped)
    .resize({ width: Math.max(1, frameWidth - 4), height: frameHeight - 4, fit: 'inside' })
    .png()
    .toBuffer({ resolveWithObject: true });
  const game = await sharp({
    create: {
      width: frameWidth,
      height: frameHeight,
      channels: 4,
      background: { r: 0, g: 0, b: 0, alpha: 0 },
    },
  })
    .composite([
      {
        input: fitted.data,
        left: Math.floor((frameWidth - fitted.info.width) / 2),
        top: frameHeight - fitted.info.height - 2,
      },
    ])
    .toColourspace('srgb')
    .webp({ lossless: true, effort: 4 })
    .toBuffer();
  async function separate(bytes: Buffer) {
    if (!teamColor) return { bytes };
    const pixels = await sharp(bytes).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
    const base = Buffer.from(pixels.data),
      layer = Buffer.alloc(base.length);
    let found = false;
    for (let at = 0; at < base.length; at += 4) {
      const r = base[at] ?? 0,
        g = base[at + 1] ?? 0,
        b = base[at + 2] ?? 0,
        a = base[at + 3] ?? 0;
      if (a && r > 100 && b > 100 && g < Math.min(r, b) * 0.55) {
        found = true;
        const brightness = Math.max(r, b);
        layer[at] = Math.round(brightness * 0.2);
        layer[at + 1] = brightness;
        layer[at + 2] = Math.round(brightness * 0.6);
        layer[at + 3] = a;
        base[at + 3] = 0;
      }
    }
    if (!found) throw Error('Requested team-color artwork is missing its magenta accent.');
    return {
      bytes: await sharp(base, { raw: pixels.info }).webp({ lossless: true, effort: 4 }).toBuffer(),
      layer: await sharp(layer, { raw: pixels.info })
        .webp({ lossless: true, effort: 4 })
        .toBuffer(),
    };
  }
  // Identify team pixels before reducing the image. A tiny valid accent can
  // blend into its surrounding colors at miniature scale; classifying the
  // miniature independently would either lose the mask or reject good artwork.
  const gameParts = await separate(game);
  const miniature = (bytes: Buffer) =>
    sharp(bytes)
      .resize({ width: Math.round(frameWidth / 2), height: Math.round(frameHeight / 2) })
      .webp({ lossless: true, effort: 4 })
      .toBuffer();
  const miniParts = {
    bytes: await miniature(gameParts.bytes),
    ...(gameParts.layer ? { layer: await miniature(gameParts.layer) } : {}),
  };
  const gameHash = buildingAssetHash(gameParts.bytes),
    miniHash = buildingAssetHash(miniParts.bytes);
  return {
    game: {
      key: key + '-game',
      frames: [
        {
          imageHash: gameHash,
          ...(gameParts.layer ? { teamColorHash: buildingAssetHash(gameParts.layer) } : {}),
          width: frameWidth,
          height: frameHeight,
        },
      ],
    },
    mini: {
      key: key + '-mini',
      frames: [
        {
          imageHash: miniHash,
          ...(miniParts.layer ? { teamColorHash: buildingAssetHash(miniParts.layer) } : {}),
          width: Math.round(frameWidth / 2),
          height: Math.round(frameHeight / 2),
        },
      ],
    },
    assets: new Map<string, Buffer>([
      [gameHash, gameParts.bytes],
      [miniHash, miniParts.bytes],
      ...(gameParts.layer
        ? [[buildingAssetHash(gameParts.layer), gameParts.layer] as [string, Buffer]]
        : []),
      ...(miniParts.layer
        ? [[buildingAssetHash(miniParts.layer), miniParts.layer] as [string, Buffer]]
        : []),
    ]),
  };
}
