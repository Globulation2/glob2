import { afterEach, describe, expect, it } from 'vitest';
import { registerCatalog, setLocale } from '../../../packages/i18n/src/index.ts';
import {
  skinLabel,
  modelProseLabel,
  skinProductLabel,
  skinPresetLabel,
} from '../src/skins/labels.ts';
import { MODELS, MATERIAL_GROUPS } from '../src/skins/atlas.ts';
import { SWARM_SHAPES } from '../src/skins/swarmShapes.ts';

afterEach(async () => {
  await setLocale('en');
});
describe('built-in skin presentation labels', () => {
  it('looks up the current locale without changing model/material/shape registry identities', async () => {
    const before = JSON.stringify({ MODELS, MATERIAL_GROUPS, SWARM_SHAPES });
    registerCatalog('fr', {
      Worker: 'Ouvrière',
      Walk: 'Marche',
      Basics: 'Bases',
      Classic: 'Classique',
    });
    await setLocale('en');
    expect(skinLabel(MODELS[0].name)).toBe('Worker');
    expect(modelProseLabel(MODELS[0].name)).toBe('worker');
    await setLocale('fr');
    expect(skinLabel(MODELS[0].name)).toBe('Ouvrière');
    expect(modelProseLabel(MODELS[0].name)).toBe('Ouvrière');
    expect(skinLabel('Walk')).toBe('Marche');
    expect(skinLabel('Basics')).toBe('Bases');
    expect(skinLabel(SWARM_SHAPES.classic.name)).toBe('Classique');
    expect(JSON.stringify({ MODELS, MATERIAL_GROUPS, SWARM_SHAPES })).toBe(before);
    expect(skinLabel('constructor')).toBe('constructor');
  });
  it('localizes known product/preset IDs and preserves unknown or custom authored names', async () => {
    registerCatalog('fr', { 'Colony stripes': 'Rayures de colonie', 'Skin designer': 'Atelier' });
    await setLocale('fr');
    expect(skinProductLabel('designer', 'Skin designer')).toBe('Atelier');
    expect(skinProductLabel('stripes', 'Original')).toBe('Rayures de colonie');
    expect(skinPresetLabel('97b6e116-918a-490c-a59d-c9a7a3e97201', 'Original')).toBe(
      'Rayures de colonie',
    );
    expect(skinProductLabel('operator-item', 'Colony stripes')).toBe('Colony stripes');
    expect(skinPresetLabel('custom-id', 'Colony stripes')).toBe('Colony stripes');
  });
});
