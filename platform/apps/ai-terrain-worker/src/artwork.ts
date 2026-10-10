import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { mkdtemp, readFile, writeFile, rm } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { createHash } from 'node:crypto';
import type { SetSheet } from '@glob2/protocol';
export class ArtworkError extends Error {}
export class ArtworkInvalid extends ArtworkError {}
const conversionReasons = [
  'Terrain source must be fully opaque',
  'Resource/decor source needs a transparent background',
  'Missing resource/decor frame',
  'Sprite touches a frame edge; regenerate with more padding',
  'Source image exceeds 16 MiB',
  'Expected a bounded PNG source image',
];
export function conversionError(error: unknown): Error {
  const stderr = (error as { stderr?: unknown })?.stderr;
  if (typeof stderr === 'string') {
    const reason = stderr.match(/(?:^|\n)ValueError: ([^\r\n]+)\s*$/)?.[1];
    if (reason && conversionReasons.includes(reason)) return new ArtworkInvalid(reason);
  }
  return new ArtworkError('Artwork conversion could not complete.');
}
/** Image corrections preserve the plan and each successfully processed entry. */
export async function prepareArtwork<T>(
  generate: (attempt: number, reason?: string) => Promise<Uint8Array>,
  process: (source: Uint8Array) => Promise<T>,
  repaired: (attempt: number, reason: string) => Promise<void>,
): Promise<T> {
  let reason: string | undefined;
  for (let attempt = 0; attempt <= 2; attempt++) {
    const source = await generate(attempt, reason);
    try {
      return await process(source);
    } catch (error) {
      if (!(error instanceof ArtworkInvalid)) throw error;
      reason = error.message;
      if (attempt === 2)
        throw new ArtworkInvalid('Artwork failed after two image repair passes: ' + reason);
      await repaired(attempt + 1, reason);
    }
  }
  throw new ArtworkInvalid('Artwork could not be prepared.');
}
export async function processArtwork(
  source: Uint8Array,
  kind: 'terrain' | 'resource' | 'decor',
  phases: number,
  root: string,
  python: string,
  signal: AbortSignal,
): Promise<{ sheet: SetSheet; color: [number, number, number] }> {
  const directory = await mkdtemp(join(tmpdir(), 'glob2-terrain-'));
  try {
    const input = join(directory, 'source.png'),
      output = join(directory, 'sheet.png');
    await writeFile(input, source);
    const result = await promisify(execFile)(
      python,
      [
        join(root, 'tools/artwork/terrain_studio.py'),
        input,
        output,
        '--kind',
        kind,
        '--phases',
        String(phases),
      ],
      { signal, timeout: 60000, maxBuffer: 1024 * 1024 },
    ).catch((error: unknown) => {
      throw conversionError(error);
    });
    const metadata = JSON.parse(result.stdout) as {
      frameWidth: number;
      frameHeight: number;
      color: [number, number, number];
    };
    const png = await readFile(output);
    if (png.length > 1024 * 1024) throw Error('Processed sheet too large.');
    return {
      sheet: {
        hash: createHash('sha256').update(png).digest('hex'),
        png: png.toString('base64url'),
        frameWidth: metadata.frameWidth,
        frameHeight: metadata.frameHeight,
      },
      color: metadata.color,
    };
  } finally {
    await rm(directory, { recursive: true, force: true });
  }
}
export function imagePrompt(kind: 'terrain' | 'resource' | 'decor', prompt: string) {
  const style =
    'Game asset, coarse soft painterly Globulation 2 style unless the user explicitly requests another style. Readable at 32-pixel play scale; consistent palette and upper-left lighting. No text, labels, borders, checkerboards or watermarks. Reference images are visual direction only. ';
  if (kind === 'terrain')
    return (
      style +
      'One seamless opaque top-down material covering the entire square, statistically uniform, no horizon, no focal object, no grid, no perspective, no large shadows. It will be cut into four independently joining 32x32 tiles. ' +
      prompt
    );
  const layout =
    kind === 'resource'
      ? 'Exactly 2 equal columns and 3 equal rows. Each row shows the same resource at one stock stage: exhausted/stubble, half-grown, full-grown; two variations per row.'
      : 'Exactly 2 equal columns and 2 equal rows. Top row: two full raised obstacles. Bottom row: two smaller cluster-edge versions.';
  return (
    style +
    layout +
    ' Transparent background; one isolated object in each invisible grid cell, padded from every cell edge by at least 10%. Same scale and ground anchor in every cell, centered horizontally. Soft three-quarter view, grounded objects rise upward; no scenery. ' +
    prompt
  );
}
