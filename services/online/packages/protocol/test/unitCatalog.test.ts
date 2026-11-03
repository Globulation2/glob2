import { createHash } from 'node:crypto';
import { describe, expect, it } from 'vitest';
import { MatchSetup, matchSetupProblems, parse, unitCatalogExperimentKeys } from '../src/index.ts';
import { catalogRulesVersion, checkUnitCatalogHash } from '../src/node/index.ts';
import { SETUP_SAVE_SHARED, SIM_VERSION } from '../scripts/fixtureCases.ts';

const snapshot = JSON.stringify({
  schemaVersion: 1,
  experiments: [{ key: 'unit-fixture', label: 'Fixture unit', help: 'Unit fixture gate' }],
  units: ['worker', 'explorer', 'warrior', 'fixture:carrier'].map((key) => ({
    key,
    levels: [{}, {}, {}, {}],
    behaviors: {},
    requiredExperiment: key.startsWith('fixture:') ? 'unit-fixture' : '',
  })),
});
const unitCatalog = { snapshot, hash: createHash('sha256').update(snapshot).digest('hex') };

describe('unit catalog contracts', () => {
  it('preserves optional setup identity and embedded experiments', () => {
    const setup = parse(MatchSetup, {
      ...SETUP_SAVE_SHARED,
      unitCatalog,
      experiments: ['unit-fixture'],
    });
    expect(matchSetupProblems(setup)).toEqual([]);
    expect(setup.unitCatalog).toEqual(unitCatalog);
    expect(
      matchSetupProblems({ ...setup, unitCatalog: undefined }).some(
        (p) => p.path === '/experiments/0',
      ),
    ).toBe(true);
    expect(() => checkUnitCatalogHash(unitCatalog)).not.toThrow();
    expect(() => checkUnitCatalogHash({ ...unitCatalog, snapshot: snapshot + ' ' })).toThrow();
  });
  it('rejects invalid IDs, unresolved inheritance and gates', () => {
    for (const mutate of [
      (root: ReturnType<typeof JSON.parse>) => {
        root.units[0].key = 'warrior';
      },
      (root: ReturnType<typeof JSON.parse>) => {
        root.units[3].extends = 'worker';
      },
      (root: ReturnType<typeof JSON.parse>) => {
        root.units[3].requiredExperiment = 'missing';
      },
      (root: ReturnType<typeof JSON.parse>) => {
        root.units[3].levels.pop();
      },
    ]) {
      const root = JSON.parse(snapshot);
      mutate(root);
      expect(() =>
        unitCatalogExperimentKeys({ ...unitCatalog, snapshot: JSON.stringify(root) }),
      ).toThrow();
    }
  });
  it('partitions custom unit rules while preserving old building-only identities', () => {
    const units = catalogRulesVersion(SIM_VERSION, undefined, unitCatalog.hash);
    expect(units.dataHash).not.toBe(SIM_VERSION.dataHash);
    expect(units.versionMinor).toBe(SIM_VERSION.versionMinor);
    expect(units.netProtocol).toBe(SIM_VERSION.netProtocol);
    expect(catalogRulesVersion(SIM_VERSION)).toEqual(SIM_VERSION);
    expect(catalogRulesVersion(SIM_VERSION, '0'.repeat(64), unitCatalog.hash)).not.toEqual(units);
  });
});
