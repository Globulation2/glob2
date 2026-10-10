import { createHash } from 'node:crypto';
import { describe, expect, it } from 'vitest';
import { parseMapReport } from '../src/engineCli.ts';

const snapshot = JSON.stringify({
  schemaVersion: 1,
  experiments: [{ key: 'unit-fixture', label: 'Fixture unit', help: 'Foundation fixture.' }],
  units: ['worker', 'explorer', 'warrior', 'fixture:carrier'].map((key) => ({
    key,
    levels: [{}, {}, {}, {}],
    behaviors: {},
    requiredExperiment: key.startsWith('fixture:') ? 'unit-fixture' : '',
  })),
});
const unitCatalog = { snapshot, hash: createHash('sha256').update(snapshot).digest('hex') };
const report = {
  schema_version: 2,
  map: {
    width: 64,
    height: 128,
    player_slots: 2,
    unitCatalog,
    requiredUnitExperiments: ['unit-fixture'],
  },
};

describe('unit catalogs in authoritative map reports', () => {
  it('retains exact catalog bytes and required gates', () => {
    expect(parseMapReport(JSON.stringify(report))).toMatchObject({
      unitCatalog,
      requiredUnitExperiments: ['unit-fixture'],
      width: 64,
      height: 128,
      teamCount: 2,
    });
  });
  it('rejects modified bytes and undeclared required gates', () => {
    expect(() =>
      parseMapReport(
        JSON.stringify({
          ...report,
          map: {
            ...report.map,
            unitCatalog: { ...unitCatalog, snapshot: snapshot + ' ' },
          },
        }),
      ),
    ).toThrow(/hash/);
    expect(() =>
      parseMapReport(
        JSON.stringify({
          ...report,
          map: {
            ...report.map,
            requiredUnitExperiments: ['unknown'],
          },
        }),
      ),
    ).toThrow(/required unit experiments/);
    expect(() =>
      parseMapReport(
        JSON.stringify({
          ...report,
          map: {
            ...report.map,
            unitCatalog: undefined,
          },
        }),
      ),
    ).toThrow(/required unit experiments/);
  });
});
