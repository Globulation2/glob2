import { readFileSync, readdirSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { Type } from 'typebox';
import { MATCH_SETUP_SCHEMA_VERSION } from '@glob2/protocol';
import {
  STORED_END_REPORT,
  STORED_MATCH_SETUP,
  STORED_REGION_RTTS,
  STORED_VERIFY_VERDICT,
  StoredDataError,
  readRegionRtts,
  readStored,
  readStoredOrNull,
  storedVersion,
  tryReadStored,
  type StoredFormat,
} from '../src/stored.ts';

const FIXTURES = new URL('./fixtures/stored/', import.meta.url);

/**
 * Stored documents frozen at the version that wrote them. They are copies, not
 * the generated protocol fixtures, so that a schema change cannot quietly move
 * them along: every version ever stored must keep decoding (through upgrades).
 */
function frozen(dir: string): [string, unknown][] {
  const url = new URL(`${dir}/`, FIXTURES);
  return readdirSync(url)
    .filter((name) => name.endsWith('.json'))
    .map((name) => [name, JSON.parse(readFileSync(new URL(name, url), 'utf8'))]);
}

function setupFixture(): Record<string, unknown> {
  return frozen('matches.setup')[0]![1] as Record<string, unknown>;
}

describe('stored documents of every historical version decode', () => {
  const problem = (result: { ok: true } | { ok: false; error: Error }) =>
    result.ok ? 'ok' : result.error.message;
  const cases: [string, (document: unknown) => string][] = [
    ['matches.setup', (d) => problem(tryReadStored(STORED_MATCH_SETUP, d))],
    ['matches.end_report', (d) => problem(tryReadStored(STORED_END_REPORT, d))],
    ['verify-verdict', (d) => problem(tryReadStored(STORED_VERIFY_VERDICT, d))],
  ];
  for (const [dir, decode] of cases) {
    const fixtures = frozen(dir);
    it(`${dir} (${fixtures.length} documents)`, () => {
      expect(fixtures.length).toBeGreaterThan(0);
      for (const [name, document] of fixtures)
        expect(`${name}: ${decode(document)}`).toBe(`${name}: ok`);
    });
  }

  it('keeps engine extensions of open schemas (orderRejections)', () => {
    const [, document] = frozen('verify-verdict')[0]!;
    const verdict = readStored(STORED_VERIFY_VERDICT, document);
    expect('orderRejections' in verdict).toBe(true);
  });

  it('has a frozen MatchSetup fixture for every version up to the current one', () => {
    const versions = new Set(frozen('matches.setup').map(([, d]) => storedVersion(d)));
    for (let v = 1; v <= MATCH_SETUP_SCHEMA_VERSION; v++) expect(versions.has(v)).toBe(true);
  });
});

describe('readStored', () => {
  it('rejects a missing document', () => {
    expect(() => readStored(STORED_MATCH_SETUP, null)).toThrow(StoredDataError);
    expect(readStoredOrNull(STORED_MATCH_SETUP, null)).toBeNull();
  });

  it('refuses a version newer than this code reads', () => {
    const newer = { ...setupFixture(), schemaVersion: MATCH_SETUP_SCHEMA_VERSION + 1 };
    const result = tryReadStored(STORED_MATCH_SETUP, newer);
    expect(result.ok).toBe(false);
    if (!result.ok) {
      expect(result.error.message).toMatch(/newer than this platform reads/);
      expect(result.error.version).toBe(MATCH_SETUP_SCHEMA_VERSION + 1);
    }
  });

  it('rejects a malformed schemaVersion', () => {
    expect(() => readStored(STORED_MATCH_SETUP, { ...setupFixture(), schemaVersion: 'x' })).toThrow(
      /bad schemaVersion/,
    );
  });

  it('reports schema issues with the column name', () => {
    const withoutRules = setupFixture();
    delete withoutRules['rules'];
    const result = tryReadStored(STORED_MATCH_SETUP, withoutRules);
    expect(result.ok).toBe(false);
    if (!result.ok) {
      expect(result.error.what).toBe('matches.setup');
      expect(result.error.issues.length).toBeGreaterThan(0);
    }
  });

  it('applies the semantic rules', () => {
    const setup = setupFixture();
    const teams = [...(setup['teams'] as unknown[])].reverse();
    expect(() => readStored(STORED_MATCH_SETUP, { ...setup, teams })).toThrow(/semantic rule/);
  });

  it('treats a document without schemaVersion as version 1', () => {
    expect(storedVersion({ a: 1 })).toBe(1);
    expect(storedVersion([1, 2])).toBe(1);
    expect(readRegionRtts([{ region: 'eu-west', rttMs: 40 }])).toEqual([
      { region: 'eu-west', rttMs: 40 },
    ]);
    expect(readRegionRtts(null)).toEqual([]);
    expect(tryReadStored(STORED_REGION_RTTS, [{ region: 'eu-west' }]).ok).toBe(false);
  });

  describe('upgrades', () => {
    const V2 = Type.Object({
      schemaVersion: Type.Literal(3),
      name: Type.String(),
      size: Type.Integer(),
    });
    const format: StoredFormat<typeof V2> = {
      what: 'test.doc',
      schema: V2,
      current: 3,
      upgrades: {
        // v1 had no schemaVersion and called the field `title`.
        1: ({ title, ...rest }) => ({ ...rest, schemaVersion: 2, name: title }),
        // v2 had no size.
        2: (doc) => ({ ...doc, schemaVersion: 3, size: 0 }),
      },
    };

    it('upgrades old versions step by step, unversioned documents from 1', () => {
      expect(readStored(format, { title: 'old' })).toEqual({
        schemaVersion: 3,
        name: 'old',
        size: 0,
      });
      expect(readStored(format, { schemaVersion: 2, name: 'mid' })).toEqual({
        schemaVersion: 3,
        name: 'mid',
        size: 0,
      });
      expect(readStored(format, { schemaVersion: 3, name: 'new', size: 4 })).toMatchObject({
        size: 4,
      });
    });

    it('fails without an upgrade step', () => {
      const gap = { ...format, upgrades: { 2: format.upgrades![2]! } };
      expect(() => readStored(gap, { title: 'old' })).toThrow(/no upgrade from version 1/);
    });

    it('fails when an upgrade does not produce the next version', () => {
      const wrong = { ...format, upgrades: { ...format.upgrades, 1: (doc: object) => doc } };
      expect(() => readStored(wrong, { title: 'old' })).toThrow(/produced version 1/);
    });
  });
});
