import { expect, it } from 'vitest';
import { newPackage, namespace } from '@glob2/protocol';
import {
  assemble,
  validatePlan,
  plannerPrompt,
  preparePlan,
  type TerrainPlan,
  type EntryArtwork,
} from '../src/assembly.ts';
const base = () => newPackage('Test author');
const entry = {
  kind: 'terrain' as const,
  operation: 'upsert' as const,
  key: 'marsh',
  name: 'Fungal marsh',
  preset: 'marsh',
  propertiesJson: '{"groundSpeedQ8":128}',
  yieldsJson: '{}',
  presentationJson: '{}',
  allowedResourceKeys: null,
  regenerateArt: true,
  artPrompt: 'Purple fungal soil',
  decorPrompt: '',
  animationFrames: 1,
};
const plan: TerrainPlan = {
  action: 'build',
  text: 'Made a marsh.',
  brief: 'Fungal swamp',
  title: 'Fungal swamp',
  description: 'Soft soil',
  entries: [entry],
};
const art: EntryArtwork = {
  sheet: { hash: 'a'.repeat(64), png: 'aA', frameWidth: 32, frameHeight: 32 },
  color: [60, 90, 50],
};
it('assembles normal terrain packages with shared appearance bindings', () => {
  const b = base(),
    key = namespace(b) + 'marsh',
    p = assemble(b, plan, { [key]: art });
  expect(p.terrains[0]).toMatchObject({ key, base: 'marsh', properties: { groundSpeedQ8: 128 } });
  expect(p.assets.terrains[key]).toMatchObject({
    profile: 'soft',
    sprite: 'data/sets/' + art.sheet.hash,
  });
  expect(p.credits).toEqual(b.credits);
});
it('preserves untouched entries and exact artwork bytes during focused revisions', () => {
  const b = base(),
    key = namespace(b) + 'marsh';
  const first = assemble(b, plan, { [key]: art });
  first.terrains.push({ key: namespace(b) + 'road', name: 'Path', base: 'road', properties: {} });
  const next = assemble(
    first,
    {
      ...plan,
      entries: [{ ...entry, regenerateArt: false, propertiesJson: '{"groundSpeedQ8":192}' }],
    },
    {},
  );
  expect(next.terrains[1]).toEqual(first.terrains[1]);
  expect(next.assets).toEqual(first.assets);
});
it('rejects missing artwork, duplicate entries, oversized scope and new inventory materials', () => {
  const b = base();
  expect(() => assemble(b, plan, {})).toThrow('missing');
  expect(() => validatePlan({ ...plan, entries: [entry, entry] }, b)).toThrow('Duplicate');
  expect(() => validatePlan({ ...plan, entries: Array(13).fill(entry) }, b)).toThrow();
  expect(() =>
    validatePlan({ ...plan, entries: [{ ...entry, yieldsJson: '{"mana":{}}' }] }, b),
  ).toThrow('inventory');
});
it('rejects discussion edits and foreign namespaces', () => {
  expect(() => validatePlan({ ...plan, action: 'discuss' }, base())).toThrow('Discussion');
  expect(() =>
    validatePlan({ ...plan, entries: [{ ...entry, key: 'other:set' }] }, base()),
  ).toThrow('slug');
});
it('identifies unsupported appearance fields without accepting provider sprite mappings', () => {
  const b = base();
  for (const field of ['color', 'sprite', 'variants']) {
    expect(() =>
      validatePlan(
        {
          ...plan,
          entries: [{ ...entry, presentationJson: JSON.stringify({ [field]: true }) }],
        },
        b,
      ),
    ).toThrow(`Unsupported terrain appearance overrides: ${field}`);
  }
});
it('gives the planner the exact appearance keys and value forms accepted by the engine', () => {
  const prompt = plannerPrompt(base(), [], 'Arctic tundra');
  expect(prompt).toContain('ONLY profile,edges,preview,minimap,seam');
  expect(prompt).toContain('RGB arrays of three integers 0-255');
  expect(prompt).toContain('seam is an OBJECT, never a boolean');
  expect(prompt).toContain('ONLY name,minimap,animationFrames,animationStride,animationTicks');
});
it('repairs the observed color and boolean-seam planner output before artwork', async () => {
  const b = base();
  const invalid = {
    ...plan,
    entries: [{ ...entry, presentationJson: '{"color":"#e8edf0","seam":true}' }],
  };
  const errors: string[] = [];
  const result = await preparePlan(JSON.stringify(invalid), b, async (_proposal, error) => {
    errors.push(error);
    return JSON.stringify(plan);
  });
  expect(result).toEqual(plan);
  expect(errors).toHaveLength(1);
  expect(errors[0]).toContain('Unsupported terrain appearance overrides: color');
  expect(() =>
    validatePlan({ ...plan, entries: [{ ...entry, presentationJson: '{"seam":true}' }] }, b),
  ).toThrow('terrain appearance');
});
it('bounds planner repairs and prevents them changing authorization or entry scope', async () => {
  const b = base();
  const invalid = { ...plan, entries: [{ ...entry, presentationJson: '{"color":"white"}' }] };
  let calls = 0;
  await expect(
    preparePlan(JSON.stringify(invalid), b, async () => {
      calls++;
      return JSON.stringify(invalid);
    }),
  ).rejects.toThrow('Unsupported terrain appearance');
  expect(calls).toBe(2);
  for (const changed of [
    { ...plan, action: 'discuss', entries: [] },
    { ...plan, entries: [{ ...entry, key: 'extra' }] },
    { ...plan, entries: [{ ...entry, operation: 'remove' }] },
  ]) {
    await expect(
      preparePlan(JSON.stringify(invalid), b, async () => JSON.stringify(changed)),
    ).rejects.toThrow('Repair changed the requested scope');
  }
  let repaired = false;
  expect(
    await preparePlan(JSON.stringify(plan), b, async () => {
      repaired = true;
      return JSON.stringify(plan);
    }),
  ).toEqual(plan);
  expect(repaired).toBe(false);
});
it('retains source credits and stock-stage resource frame mappings', () => {
  const b = base(),
    key = namespace(b) + 'mushroom';
  const e = {
    ...entry,
    kind: 'resource' as const,
    key: 'mushroom',
    preset: 'wheat',
    name: 'Mushrooms',
    propertiesJson: '{}',
    yieldsJson: '{}',
    animationFrames: 4,
  };
  const p = assemble(
    b,
    { ...plan, entries: [e] },
    { [key]: { ...art, sheet: { ...art.sheet, frameWidth: 64, frameHeight: 64 } } },
  );
  expect(p.resources[0]?.['presentation']).toMatchObject({
    animationFrames: 4,
    animationStride: 6,
    levels: [{ stock: 0 }, { stock: 2 }, { stock: 5 }],
  });
  expect(p.credits).toEqual(b.credits);
});

it('keeps builtin resource permissions and binds newly named custom resources', () => {
  const b = base(),
    key = namespace(b) + 'marsh';
  const p = assemble(
    b,
    { ...plan, entries: [{ ...entry, allowedResourceKeys: ['wheat'] }] },
    { [key]: art },
  );
  expect(p.terrains[0]?.['allowedResourceKeys']).toEqual(['wheat']);
  const resource = {
    ...entry,
    kind: 'resource' as const,
    key: 'mushroom',
    name: 'Mushrooms',
    preset: 'wheat',
  };
  const mixed = assemble(
    b,
    { ...plan, entries: [{ ...entry, allowedResourceKeys: ['mushroom', 'wheat'] }, resource] },
    {
      [key]: art,
      [namespace(b) + 'mushroom']: {
        ...art,
        sheet: { ...art.sheet, hash: 'b'.repeat(64), frameWidth: 64, frameHeight: 64 },
      },
    },
  );
  expect(mixed.terrains[0]?.['allowedResourceKeys']).toEqual([namespace(b) + 'mushroom', 'wheat']);
});
