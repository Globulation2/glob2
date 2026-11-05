import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import {
  LEGACY_TERRAIN_PRESET_ALIASES,
  SetPackage,
  TERRAIN_PRESETS,
  isTerrainPreset,
  isValid,
  newPackage,
  newRelease,
  normalizeSetPackage,
  normalizeTerrainPreset,
  namespace,
} from '../src/index.ts';

describe('built-in terrain presets', () => {
  it('follow the engine terrain table in ID order', () => {
    const table = readFileSync(
      new URL('../../../../../src/map/TerrainTypeTable.h', import.meta.url),
      'utf8',
    );
    const names = [...table.matchAll(/^\s*(?:authored\(|\{)[A-Z_]+, G::\w+, "([a-z_]+)"/gm)].map(
      (m) => normalizeTerrainPreset(m[1] ?? ''),
    );
    expect(names.length).toBeGreaterThan(30);
    expect(TERRAIN_PRESETS).toEqual(names);
    expect(TERRAIN_PRESETS.slice(-17)).toEqual([
      'sandstone',
      'pumice',
      'frozen_lake',
      'wind_swept_snow',
      'weathered_sandstone',
      'windy_sand_dune',
      'hardpan',
      'tundra',
      'volcanic_ash',
      'glacier_ice',
      'overgrown_road',
      'laterite',
      'cliffs',
      'molten_lava_pool',
      'slate_ashlar',
      'basalt_causeway',
      'scoria_blocks',
    ]);
  });

  it('map every retired name to a current preset', () => {
    expect(LEGACY_TERRAIN_PRESET_ALIASES).toEqual({
      flower_meadow: 'heath',
      gravel: 'shale',
      dirt_track: 'brick_path',
      marsh: 'mudflat',
    });
    for (const [legacy, current] of Object.entries(LEGACY_TERRAIN_PRESET_ALIASES)) {
      expect(TERRAIN_PRESETS).not.toContain(legacy);
      expect(TERRAIN_PRESETS).toContain(current);
      expect(normalizeTerrainPreset(legacy)).toBe(current);
      expect(isTerrainPreset(legacy)).toBe(true);
    }
    expect(normalizeTerrainPreset('grass')).toBe('grass');
    expect(normalizeTerrainPreset('constructor')).toBe('constructor');
    expect(isTerrainPreset('swamp')).toBe(false);
  });

  it('accept a set saved with old names and emit the new ones', () => {
    const pack = newPackage('Old client');
    const key = namespace(pack);
    pack.terrains.push(
      { key: key + 'bog', name: 'Bog', base: 'marsh', appearance: 'flower_meadow', properties: {} },
      { key: key + 'lane', name: 'Lane', base: 'dirt_track', appearance: 'gravel', properties: {} },
      { key: key + 'sea', name: 'Sea', base: 'water', appearance: 'water', properties: {} },
    );
    const before = JSON.stringify(pack);
    expect(isValid(SetPackage, pack)).toBe(true);
    const current = normalizeSetPackage(pack);
    expect(isValid(SetPackage, current)).toBe(true);
    expect(current.terrains.map((t) => [t['base'], t['appearance']])).toEqual([
      ['mudflat', 'heath'],
      ['brick_path', 'shale'],
      ['water', 'water'],
    ]);
    // The input is never rewritten in place, and untouched entries keep their identity.
    expect(JSON.stringify(pack)).toBe(before);
    expect(current.terrains[2]).toBe(pack.terrains[2]);
    expect(normalizeSetPackage(current)).toBe(current);
    expect(newRelease(pack).terrains[0]).toMatchObject({ base: 'mudflat', appearance: 'heath' });
  });
});
