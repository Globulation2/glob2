import { describe, it, expect } from 'vitest';
import { randomUUID } from 'node:crypto';
import { newBuildingPackage, assemble, validatePlan, type BuildingPlan } from '../src/assembly.ts';
function fixture() {
  const base = newBuildingPackage(randomUUID());
  base.variants[0]!.semantics = {
    healing: { enabled: true, duration: 20, unitMask: 7, cost: { food: 1 } },
    assignmentLimit: 4,
  };
  const plan: BuildingPlan = {
    action: 'build',
    scope: 'properties',
    text: 'Cheaper',
    brief: 'Hospital',
    title: 'Hospital',
    experimentsJson: null,
    entries: [
      {
        key: 'building',
        operation: 'upsert',
        next: null,
        previous: null,
        requiredExperiment: null,
        propertiesJson: '{"hpMax":500}',
        semanticsJson: '{"healing":{"duration":10}}',
        presentationJson: '{}',
        regenerateArt: false,
        teamColor: false,
        artPrompt: '',
      },
    ],
  };
  return { base, plan };
}
describe('building assembly', () => {
  it('merges focused edits without changing artwork, costs or other capability fields', () => {
    const { base, plan } = fixture(),
      original = structuredClone(base),
      result = assemble(base, plan, {});
    expect(result.variants[0]!.semantics['healing']).toEqual({
      enabled: true,
      duration: 10,
      unitMask: 7,
      cost: { food: 1 },
    });
    expect(result.variants[0]!.properties['gameSprite']).toBe(
      base.variants[0]!.properties['gameSprite'],
    );
    expect(base).toEqual(original);
  });
  it('rejects scope expansion, sprite injection and discussion mutations', () => {
    const { base, plan } = fixture();
    expect(() => validatePlan({ ...plan, scope: 'appearance' }, base)).toThrow('gameplay');
    expect(() => validatePlan({ ...plan, action: 'discuss' }, base)).toThrow('Discussion');
    plan.entries[0]!.propertiesJson = '{"gameSprite":"/etc/passwd"}';
    expect(() => validatePlan(plan, base)).toThrow('Artwork mappings');
  });
  it('resolves construction/upgrade links and retains unrelated variants', () => {
    const { base, plan } = fixture();
    plan.scope = 'both';
    plan.entries[0]!.previous = 'site';
    plan.entries.push({
      ...plan.entries[0]!,
      key: 'site',
      previous: '',
      next: 'building',
      propertiesJson: '{"width":2,"height":2,"isBuildingSite":1}',
      semanticsJson: '{"placeable":false}',
      regenerateArt: true,
      teamColor: false,
      artPrompt: 'Under construction',
    });
    const image = { imageHash: 'a'.repeat(64), width: 64, height: 96 };
    const result = assemble(base, plan, {
      [base.variants[0]!.key.replace(/building$/, 'site')]: {
        game: { key: 'site-game', frames: [image] },
        mini: { key: 'site-mini', frames: [image] },
        assets: new Map(),
      },
    });
    expect(result.variants[1]!.next).toBe(result.variants[0]!.key);
    expect(result.variants[0]!.previous).toBe(result.variants[1]!.key);
    expect(result.namespace).toBe(base.namespace);
  });
  it('rejects dangling links, new property-only variants and recursive prototype keys', () => {
    const { base, plan } = fixture();
    plan.entries[0]!.next = 'missing';
    expect(() => assemble(base, plan, {})).toThrow('Unresolved');
    plan.entries[0]!.key = 'new';
    expect(() => validatePlan(plan, base)).toThrow('New variants');
    plan.entries[0]!.key = 'building';
    plan.entries[0]!.next = null;
    plan.entries[0]!.semanticsJson = '{"healing":{"__proto__":{"x":1}}}';
    expect(() => assemble(base, plan, {})).toThrow('Invalid override');
  });
});
