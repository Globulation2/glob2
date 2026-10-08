import type { SetPackage } from '@glob2/protocol';
import registry from '../../../../../data/resources/registry.json';
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
  'gravel',
  'flower_meadow',
  'mud',
  'marsh',
  'deep_snow',
  'scree',
  'dirt_track',
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
];
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
  return replace(copy) as SetPackage;
}
export async function sheetFromFile(file: File, frameWidth: number, frameHeight: number) {
  if (file.size > 12 * 1024 * 1024) throw Error('This PNG exceeds the sheet upload limit.');
  if (
    !Number.isInteger(frameWidth) ||
    !Number.isInteger(frameHeight) ||
    frameWidth < 1 ||
    frameHeight < 1 ||
    frameWidth > 64 ||
    frameHeight > 64
  )
    throw Error('Frame dimensions must be whole numbers between 1 and 64.');
  const bytes = new Uint8Array(await file.arrayBuffer());
  if (bytes.length < 33 || [137, 80, 78, 71, 13, 10, 26, 10].some((v, i) => bytes[i] !== v))
    throw Error('Choose a PNG spritesheet.');
  const view = new DataView(bytes.buffer),
    width = view.getUint32(16),
    height = view.getUint32(20);
  if (
    !width ||
    !height ||
    width > 2048 ||
    height > 2048 ||
    width % frameWidth ||
    height % frameHeight
  )
    throw Error('The sheet must be at most 2048×2048 and fit its frame grid exactly.');
  const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), (v) =>
    v.toString(16).padStart(2, '0'),
  ).join('');
  let binary = '';
  for (let i = 0; i < bytes.length; i += 8192)
    binary += String.fromCharCode(...bytes.subarray(i, i + 8192));
  return {
    hash,
    png: btoa(binary).replaceAll('+', '-').replaceAll('/', '_').replaceAll('=', ''),
    frameWidth,
    frameHeight,
  };
}
export function sheetUrl(png: string) {
  return 'data:image/png;base64,' + png.replaceAll('-', '+').replaceAll('_', '/');
}
