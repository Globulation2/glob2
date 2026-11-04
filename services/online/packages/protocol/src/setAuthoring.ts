import type { SetPackage } from './sets.ts';
import registry from '../../../../../content/data/resources/registry.json' with { type: 'json' };
export const TERRAIN_PRESETS = [
  'water',
  'sand',
  'grass',
  'ice',
  'road',
  'boulders',
  'hedge',
  'thicket',
  'ridge_rock',
  'outcrop',
  'dirt',
  'clay',
  'shale',
  'heath',
  'mud',
  'mudflat',
  'deep_snow',
  'scree',
  'brick_path',
  'boardwalk',
  'lava',
  'ember_field',
  'loam',
  'moss',
  'spring_meadow',
  'deep_water',
  'dark_water',
  'void_hole',
  'chasm',
  'lush_grass',
  'dry_grass',
  'dune_sand',
  'pale_sand',
  'sandstone',
  'pumice',
  'frozen_lake',
  'wind_swept_snow',
  'weathered_sandstone',
  'windy_sand_dune',
  'hardpan',
];
/**
 * Built-in terrain names retired by renames (old -> current). Numeric IDs and gameplay
 * groups are unchanged, so an old name means exactly the same terrain. Sets, drafts and
 * studio requests stored before the rename, and older clients, still send these names.
 */
export const LEGACY_TERRAIN_PRESET_ALIASES: Readonly<Record<string, string>> = Object.freeze({
  flower_meadow: 'heath',
  gravel: 'shale',
  dirt_track: 'brick_path',
  marsh: 'mudflat',
});
/** Current name for a built-in terrain preset; other values pass through unchanged. */
export function normalizeTerrainPreset(name: string): string {
  return Object.hasOwn(LEGACY_TERRAIN_PRESET_ALIASES, name)
    ? (LEGACY_TERRAIN_PRESET_ALIASES[name] as string)
    : name;
}
/** True for a current preset name or a legacy alias of one. */
export function isTerrainPreset(name: string): boolean {
  return TERRAIN_PRESETS.includes(normalizeTerrainPreset(name));
}
/**
 * Rewrites legacy built-in names in terrain `base` and `appearance` references. Returns
 * the same object when nothing changes. Call only where a package enters the platform
 * before it is stored or hashed: published releases and validated deliveries are
 * content-addressed by their exact bytes and must never be rewritten.
 */
export function normalizeSetPackage<T extends Pick<SetPackage, 'terrains'>>(pack: T): T {
  let changed = false;
  const terrains = pack.terrains.map((terrain) => {
    let next = terrain;
    for (const field of ['base', 'appearance']) {
      const value = terrain[field];
      if (typeof value !== 'string') continue;
      const current = normalizeTerrainPreset(value);
      if (current === value) continue;
      if (next === terrain) next = { ...terrain };
      next[field] = current;
      changed = true;
    }
    return next;
  });
  return changed ? { ...pack, terrains } : pack;
}
export const MATERIALS = [
  'wood',
  'food',
  'paper',
  'stone',
  'algae',
  'cherries',
  'oranges',
  'prunes',
  'gold',
  'metal',
  'glass',
  'fabric',
];
export const RESOURCE_PRESETS = registry.resources;
export const TERRAIN_PROPERTIES: Record<string, unknown> = {
  walkable: true,
  swimmable: false,
  flyable: true,
  resourcesGrow: true,
  fertilitySource: true,
  nonGrowingResources: true,
  buildable: true,
  projectileBlocks: false,
  shoreline: false,
  groundSpeedQ8: 256,
  airSpeedQ8: 256,
  groundHealthQ8: 0,
  airHealthQ8: 0,
  growthQ8: 256,
  fertilityQ8: 256,
  inhibitionQ8: 0,
  shoreSupportQ8: 256,
  farmMaterial: null,
};
export function namespace(pack: SetPackage) {
  return 's' + pack.setId.replaceAll('-', '') + pack.versionId.replaceAll('-', '') + ':';
}
export function newPackage(author: string): SetPackage {
  return {
    schemaVersion: 1,
    setId: crypto.randomUUID(),
    versionId: crypto.randomUUID(),
    title: 'Untitled set',
    description: '',
    tags: [],
    license: 'CC-BY-4.0',
    credits: [{ author, license: 'CC-BY-4.0' }],
    terrains: [],
    resources: [],
    experiments: [],
    assets: { schemaVersion: 1, sheets: [], terrains: {}, credits: [] },
  };
}
export function newRelease(pack: SetPackage): SetPackage {
  const copy = structuredClone(pack),
    old = namespace(copy);
  copy.versionId = crypto.randomUUID();
  const next = namespace(copy);
  const replace = (value: unknown): unknown => {
    if (typeof value === 'string')
      return value.startsWith(old) ? next + value.slice(old.length) : value;
    if (Array.isArray(value)) return value.map(replace);
    if (value && typeof value === 'object')
      return Object.fromEntries(
        Object.entries(value).map(([k, v]) => [
          k.startsWith(old) ? next + k.slice(old.length) : k,
          replace(v),
        ]),
      );
    return value;
  };
  // A release copy is a new draft, so it starts with current built-in terrain names.
  return normalizeSetPackage(replace(copy) as SetPackage);
}
