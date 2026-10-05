import { createHash } from 'node:crypto';
import { describe, expect, it } from 'vitest';
import { MatchSetup, matchSetupProblems, parse, simVersionKey } from '../src/index.ts';
import { catalogRulesVersion, checkBuildingCatalogHash } from '../src/node/index.ts';
import { SETUP_SAVE_SHARED, SIM_VERSION } from '../scripts/fixtureCases.ts';

const snapshot = JSON.stringify({
  catalogKey: 'fixture',
  schemaVersion: 1,
  variants: [],
  experiments: [{ key: 'new-building', label: 'New building', help: 'Enables this building.' }],
});
const buildingCatalog = { snapshot, hash: createHash('sha256').update(snapshot).digest('hex') };

describe('building catalog contracts', () => {
  it('preserves old schema-1 documents and validates embedded feature keys', () => {
    expect(matchSetupProblems(parse(MatchSetup, SETUP_SAVE_SHARED))).toEqual([]);
    const setup = parse(MatchSetup, {
      ...SETUP_SAVE_SHARED,
      buildingCatalog,
      experiments: ['new-building'],
    });
    expect(matchSetupProblems(setup)).toEqual([]);
    expect(
      matchSetupProblems({ ...setup, buildingCatalog: undefined }).some(
        (p) => p.path === '/experiments/0',
      ),
    ).toBe(true);
    expect(
      matchSetupProblems({ ...setup, experiments: ['unknown-building'] }).some(
        (p) => p.path === '/experiments/0',
      ),
    ).toBe(true);
    expect(
      matchSetupProblems({
        ...setup,
        buildingCatalog: { ...buildingCatalog, snapshot: '{}' },
      }).some((p) => p.path === '/buildingCatalog/snapshot'),
    ).toBe(true);
  });

  it('rejects snapshot substitution and separates rules from engine routing identity', () => {
    expect(() => checkBuildingCatalogHash(buildingCatalog)).not.toThrow();
    expect(() =>
      checkBuildingCatalogHash({ ...buildingCatalog, snapshot: snapshot + ' ' }),
    ).toThrow();
    expect(catalogRulesVersion(SIM_VERSION)).toEqual(SIM_VERSION);
    const rules = catalogRulesVersion(SIM_VERSION, buildingCatalog.hash);
    expect(rules.dataHash).toBe(
      createHash('sha256')
        .update(`glob2-building-rules-v1\n${simVersionKey(SIM_VERSION)}\n${buildingCatalog.hash}`)
        .digest('hex'),
    );
    expect(rules).not.toEqual(SIM_VERSION);
    expect(rules.versionMinor).toBe(SIM_VERSION.versionMinor);
    expect(rules.netProtocol).toBe(SIM_VERSION.netProtocol);
    expect(catalogRulesVersion(SIM_VERSION, 'c'.repeat(64))).not.toEqual(rules);
    expect(() => catalogRulesVersion(SIM_VERSION, 'bad')).toThrow();
  });
});
