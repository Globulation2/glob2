import { t, getLocale } from '../messages.ts';

// Closed built-in presentation vocabulary. Functions defer lookup until render so
// switching locale never freezes translated text in model/material registries.
const labels: Record<string, () => string> = {
  Worker: () => t('Worker'),
  Warrior: () => t('Warrior'),
  Explorer: () => t('Explorer'),
  Swarm: () => t('Swarm'),
  'Classic glossy': () => t('Classic glossy'),
  Basics: () => t('Basics'),
  Matte: () => t('Matte'),
  'Riveted metal': () => t('Riveted metal'),
  Metal: () => t('Metal'),
  Hairy: () => t('Hairy'),
  Organic: () => t('Organic'),
  Cartoon: () => t('Cartoon'),
  'Woven fabric': () => t('Woven fabric'),
  Craft: () => t('Craft'),
  'Oozy goo': () => t('Oozy goo'),
  Wet: () => t('Wet'),
  Wood: () => t('Wood'),
  Stone: () => t('Stone'),
  Mineral: () => t('Mineral'),
  Scales: () => t('Scales'),
  Leather: () => t('Leather'),
  Crystal: () => t('Crystal'),
  Lava: () => t('Lava'),
  Chrome: () => t('Chrome'),
  'Carbon fibre': () => t('Carbon fibre'),
  Velvet: () => t('Velvet'),
  Honeycomb: () => t('Honeycomb'),
  Ice: () => t('Ice'),
  Cloud: () => t('Cloud'),
  'Hammered metal': () => t('Hammered metal'),
  Candy: () => t('Candy'),
  Slime: () => t('Slime'),
  Classic: () => t('Classic'),
  'The original swarm: spires ringed around an egg.': () =>
    t('The original swarm: spires ringed around an egg.'),
  Crown: () => t('Crown'),
  'Seven smooth spires around a central egg.': () => t('Seven smooth spires around a central egg.'),
  Clutch: () => t('Clutch'),
  'Six brood eggs nested in a low rim.': () => t('Six brood eggs nested in a low rim.'),
  Toadstool: () => t('Toadstool'),
  'A broad-capped toadstool with young caps.': () => t('A broad-capped toadstool with young caps.'),
  Coral: () => t('Coral'),
  'Branching spires rising from a mound.': () => t('Branching spires rising from a mound.'),
  Skep: () => t('Skep'),
  'A coiled hive on a stand, its door facing you.': () =>
    t('A coiled hive on a stand, its door facing you.'),
  Bloom: () => t('Bloom'),
  'Petals cupped open around a central bud.': () => t('Petals cupped open around a central bud.'),
  Walk: () => t('Walk'),
  Swim: () => t('Swim'),
  Harvest: () => t('Harvest'),
  Fight: () => t('Fight'),
  Fly: () => t('Fly'),
  Front: () => t('Front'),
  Back: () => t('Back'),
  Left: () => t('Left'),
  Right: () => t('Right'),
  'Skin designer': () => t('Skin designer'),
  'Colony stripes': () => t('Colony stripes'),
  'Colony spots': () => t('Colony spots'),
};
export function skinLabel(source: string): string {
  const lookup = Object.hasOwn(labels, source) ? labels[source] : undefined;
  return lookup ? lookup() : source;
}
/** English uses lower case in prose; other languages retain their translated form. */
export function modelProseLabel(source: string): string {
  const label = skinLabel(source);
  return getLocale() === 'en' ? label.toLowerCase() : label;
}
export function skinProductLabel(sku: string, fallback: string): string {
  switch (sku) {
    case 'designer':
      return skinLabel('Skin designer');
    case 'stripes':
      return skinLabel('Colony stripes');
    case 'spots':
      return skinLabel('Colony spots');
    default:
      return fallback;
  }
}
export function skinPresetLabel(skinId: string, fallback: string): string {
  switch (skinId) {
    case '97b6e116-918a-490c-a59d-c9a7a3e97201':
      return skinLabel('Colony stripes');
    case '97b6e116-918a-490c-a59d-c9a7a3e97202':
      return skinLabel('Colony spots');
    default:
      return fallback;
  }
}
