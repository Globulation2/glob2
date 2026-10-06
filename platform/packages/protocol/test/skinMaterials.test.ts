import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import { COLONY_SKIN_MATERIALS, COLONY_SKIN_SHELLS } from '../src/skins.ts';

// The game builds its material table from libgag/shaders/skin-materials.json;
// the platform ships a copy because its runtime images hold only platform/.
const registry = join(
  dirname(fileURLToPath(import.meta.url)),
  '../../../../libgag/shaders/skin-materials.json',
);

describe('colony skin materials', () => {
  it('mirror the game registry', () => {
    const expected = JSON.parse(readFileSync(registry, 'utf8')) as {
      shells: number;
      materials: unknown[];
    };
    expect(COLONY_SKIN_MATERIALS).toEqual(expected.materials);
    expect(COLONY_SKIN_SHELLS).toBe(expected.shells);
  });
  it('are indexed by id', () => {
    COLONY_SKIN_MATERIALS.forEach((material, index) => expect(material.id).toBe(index));
  });
});
