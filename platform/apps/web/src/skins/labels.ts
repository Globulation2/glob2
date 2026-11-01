import type { ColonySkinMaterial, SwarmMeshId } from '@glob2/protocol';
import { getLocale, t } from '../messages.ts';
import type { Model } from './atlas.ts';

// Registry IDs/names stay canonical. Resolve display text at render time so an
// open painter follows locale changes without rewriting any saved document.
const modelLabels: Record<Model['id'], () => string> = {
  worker: () => t('Worker'),
  warrior: () => t('Warrior'),
  explorer: () => t('Explorer'),
  swarm: () => t('Swarm'),
};
export const skinModelLabel = (id: Model['id']): string => modelLabels[id]();

const materialLabels: Record<ColonySkinMaterial['key'], () => string> = {
  glossy: () => t('Classic glossy'),
  matte: () => t('Matte'),
  metallic: () => t('Riveted metal'),
  hairy: () => t('Hairy'),
  cartoon: () => t('Cartoon'),
  woven: () => t('Woven fabric'),
  goo: () => t('Oozy goo'),
  wood: () => t('Wood'),
  stone: () => t('Stone'),
  scales: () => t('Scales'),
  leather: () => t('Leather'),
  crystal: () => t('Crystal'),
  lava: () => t('Lava'),
  chrome: () => t('Chrome'),
  carbon: () => t('Carbon fibre'),
  velvet: () => t('Velvet'),
  honeycomb: () => t('Honeycomb'),
  ice: () => t('Ice'),
  cloud: () => t('Cloud'),
  hammered: () => t('Hammered metal'),
  candy: () => t('Candy'),
  slime: () => t('Slime'),
};
export const skinMaterialLabel = (key: ColonySkinMaterial['key']): string => materialLabels[key]();

const groupLabels: Record<ColonySkinMaterial['group'], () => string> = {
  Basics: () => t('Basics'),
  Metal: () => t('Metal'),
  Organic: () => t('Organic'),
  Craft: () => t('Craft'),
  Wet: () => t('Wet'),
  Mineral: () => t('Mineral'),
};
export const skinMaterialGroupLabel = (group: ColonySkinMaterial['group']): string =>
  groupLabels[group]();

const shapeLabels: Record<SwarmMeshId, { name: () => string; description: () => string }> = {
  classic: {
    name: () => t('Classic'),
    description: () => t('The original swarm: spires ringed around an egg.'),
  },
  crown: {
    name: () => t('Crown'),
    description: () => t('Seven smooth spires around a central egg.'),
  },
  clutch: {
    name: () => t('Clutch'),
    description: () => t('Six brood eggs nested in a low rim.'),
  },
  toadstool: {
    name: () => t('Toadstool'),
    description: () => t('A broad-capped toadstool with young caps.'),
  },
  coral: {
    name: () => t('Coral'),
    description: () => t('Branching spires rising from a mound.'),
  },
  skep: {
    name: () => t('Skep'),
    description: () => t('A coiled hive on a stand, its door facing you.'),
  },
  bloom: {
    name: () => t('Bloom'),
    description: () => t('Petals cupped open around a central bud.'),
  },
};
export const skinShapeLabel = (id: SwarmMeshId): string => shapeLabels[id].name();
export const skinShapeDescription = (id: SwarmMeshId): string => shapeLabels[id].description();

// These names are fixed server products/presets, never custom design names.
export function skinProductLabel(sku: string, fallback: string): string {
  switch (sku) {
    case 'designer':
      return t('Skin designer');
    case 'stripes':
      return t('Colony stripes');
    case 'spots':
      return t('Colony spots');
    default:
      return fallback;
  }
}
export function skinPresetLabel(name: string): string {
  switch (name) {
    case 'Colony stripes':
      return t('Colony stripes');
    case 'Colony spots':
      return t('Colony spots');
    default:
      return name;
  }
}

/** Keep the whole translated copy template within the API's UTF-16 name limit. */
export function skinCopyName(name: string): string {
  const copy = (base: string) => t('{value0} copy', { value0: base });
  const full = copy(name);
  if (full.length <= 64) return full;
  let base = '';
  const segmenter = new Intl.Segmenter(getLocale(), { granularity: 'grapheme' });
  for (const { segment } of segmenter.segment(name)) {
    const next = base + segment;
    if (copy(next).length > 64) break;
    base = next;
  }
  const shortened = copy(base);
  // A reviewed template normally leaves room for a name. A malformed template
  // must fail before sending an invalid name or silently cutting its wording.
  if (shortened.length > 64) throw new RangeError('Copy-name template exceeds the name limit');
  return shortened;
}
