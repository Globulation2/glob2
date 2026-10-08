import { readFile } from 'node:fs/promises';
import { join } from 'node:path';
import sharp from 'sharp';

/** Provider references carry roles so a player's frontal photo cannot replace the game camera. */
export interface ArtworkReference {
  bytes: Uint8Array;
  role: string;
}

async function readFirst(paths: string[]): Promise<Buffer | undefined> {
  for (const path of paths) {
    try {
      return await readFile(path);
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code !== 'ENOENT') throw error;
    }
  }
  return undefined;
}

/** Docker ships explicit camera references; source checkouts use the original sprite files. */
export async function stockReferences(root: string): Promise<ArtworkReference[]> {
  const references: ArtworkReference[] = [];
  for (const stem of ['inn0b0', 'hosp0b0', 'defencetower0b0']) {
    const base = await readFirst([
      join(root, 'studio/building-references', stem + '.png'),
      join(root, 'data/gfx', stem + '.png'),
      join(root, 'data/gfx', stem + '.webp'),
    ]);
    if (!base) continue;
    const team = await readFirst([
      join(root, 'studio/building-references', stem + 'r.png'),
      join(root, 'data/gfx', stem + 'r.png'),
    ]);
    const decoder = team ? sharp(base).composite([{ input: team }]) : sharp(base);
    references.push({
      bytes: await decoder.png().toBuffer(),
      role: 'stock Glob2 building: authoritative elevated camera, painterly style and game scale',
    });
  }
  if (!references.length) throw Error('Stock building camera references are unavailable.');
  return references;
}

/** Four uploads, three stock sprites and one family image fit within the provider's input limit. */
export function referenceInstructions(references: readonly ArtworkReference[]) {
  return '\nReference roles:\n' + references.map((r, i) => `Image ${i + 1}: ${r.role}.`).join('\n');
}
