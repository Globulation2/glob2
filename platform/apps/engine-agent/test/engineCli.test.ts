// Engine CLI arguments and output parsing, against outputs captured from a
// real glob2 binary (test/fixtures: catalog excerpt, generation result, map
// report, game result and a map header).
import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { defaultMapPool } from '@glob2/core';
import {
  EngineInputError,
  EngineOutputError,
  generateMapArgs,
  parseCatalog,
  parseGameResult,
  parseGenerationResult,
  parseMapReport,
  parseSimVersionOutput,
  parseVerdict,
  readMapHeader,
  supportsSimVersionFlag,
  verifyMatchArgs,
} from '../src/engineCli.ts';
import { limitPrefix, runProcess } from '../src/process.ts';
import { detectSimVersion, resolveSimVersion, SimVersionError } from '../src/simVersion.ts';

const fixture = (name: string) =>
  readFileSync(new URL(`./fixtures/${name}`, import.meta.url), 'utf8');
const HASH = 'ab'.repeat(32);

describe('catalog and generation', () => {
  const catalog = parseCatalog(fixture('catalog-excerpt.json'));

  it('reads versions and generators from the real catalog', () => {
    expect(catalog.versionMinor).toBe(123);
    expect(catalog.netProtocol).toBe(46);
    expect(catalog.dataHash).toBeUndefined();
    expect(supportsSimVersionFlag(catalog)).toBe(false);
    const arena = catalog.generators.get('symmetric-arena')!;
    expect([arena.method, arena.revision, arena.editorOnly]).toEqual([15, 1, false]);
    expect(arena.controls.get('width')!.values).toEqual([6, 7, 8, 9]);
    expect(catalog.generators.get('uniform')!.editorOnly).toBe(true);
  });

  it('builds the documented structured generation command from a pool entry', () => {
    const entry = defaultMapPool('1v1').find((e) => e.generatorId === 'symmetric-arena')!;
    expect(generateMapArgs({ ...entry, seed: 42 }, catalog, '/tmp/out')).toEqual([
      '--generate-map',
      '--generator',
      '15',
      '--map-seed',
      '42',
      '--candidates',
      '5',
      '--write-map',
      'true',
      '--param',
      'height=7',
      '--param',
      'teams=2',
      '--param',
      'width=7',
      '--output-dir',
      '/tmp/out',
    ]);
    expect(() =>
      generateMapArgs({ ...entry, seed: 1, startingUnitLevel: 2 }, catalog, '/tmp/out'),
    ).toThrow(EngineInputError);
  });

  it('reads a real generation result and map report', () => {
    expect(parseGenerationResult(fixture('generation-result.json'))).toEqual({
      chosenSeed: 2614573286,
      map: { width: 128, height: 128, teamCount: 2 },
      startQuality: { fairness: 0.999913, score: 0.999913 },
    });
    expect(() =>
      parseGenerationResult('{"schema_version":1,"status":"invalid_request","diagnostic":"bad"}'),
    ).toThrow(/invalid_request.*bad/);
    expect(parseMapReport(fixture('save-report.json'))).toEqual({
      name: 'study-15-42-r0',
      width: 128,
      height: 128,
      teamCount: 2,
      savedGame: true,
      tick: 0,
    });
    expect(() => parseMapReport('{"schema_version":3}')).toThrow(EngineOutputError);
  });

  it('reads the header of a real generated map', () => {
    const bytes = Buffer.from(
      '0000000e73747564792d31352d34322d7230000000000000007b000000020006cf76007a6f4e',
      'hex',
    );
    expect(readMapHeader(bytes)).toEqual({
      name: 'study-15-42-r0',
      versionMajor: 0,
      versionMinor: 123,
      teamCount: 2,
      savedGame: false,
    });
    expect(readMapHeader(Buffer.from('nope'))).toBeUndefined();
    expect(readMapHeader(Buffer.from('ffffffff00', 'hex'))).toBeUndefined();
  });
});

describe('verify-match outputs', () => {
  it('reads the real HeadlessRunner result.json', () => {
    const game = parseGameResult(fixture('game-result.json'));
    expect(game.finalTick).toBe(2100);
    expect(game.teams).toHaveLength(2);
    expect(game.teams[0]).toMatchObject({ team: 0, outcome: 'unresolved', prestige: 0 });
    expect(game.teams[0]!.eliminatedTick).toBeUndefined();
    expect(game.teams[0]!.timeline).toHaveLength(5);
    expect(game.teams[1]!.statistics).toMatchObject({ alive: 1 });
    expect(() => parseGameResult('{"schema_version":1,"status":"artifact_failure"}')).toThrow(
      EngineOutputError,
    );
  });

  it('reads verdicts', () => {
    expect(parseVerdict('{"verdict":"verified"}')).toEqual({ verdict: 'verified' });
    expect(parseVerdict('{"verdict":"diverged","seats":[3,1,3]}')).toEqual({
      verdict: 'diverged',
      seats: [1, 3],
    });
    expect(parseVerdict('{"verdict":"unverifiable"}')).toEqual({
      verdict: 'unverifiable',
      reason: 'unverifiable',
    });
    expect(() => parseVerdict('{"verdict":"diverged"}')).toThrow(/without seats/);
    expect(() => parseVerdict('{"verdict":"maybe"}')).toThrow(/unknown verdict/);
    expect(() => parseVerdict('not json')).toThrow(EngineOutputError);
    expect(verifyMatchArgs('r', 'm', 'o')).toEqual([
      '--verify-match',
      'r',
      '--map',
      'm',
      '--out',
      'o',
    ]);
  });
});

describe('sim version', () => {
  const catalog = { versionMinor: 125, netProtocol: 49 };

  it('parses --sim-version output in either key style', () => {
    expect(
      parseSimVersionOutput(`noise\n{"versionMinor":125,"netProtocol":49,"dataHash":"${HASH}"}\n`),
    ).toEqual({ versionMinor: 125, netProtocol: 49, dataHash: HASH });
    expect(
      parseSimVersionOutput(`{"version_minor":125,"net_protocol":49,"data_hash":"${HASH}"}`),
    ).toEqual({ versionMinor: 125, netProtocol: 49, dataHash: HASH });
    expect(parseSimVersionOutput('Settings::load error')).toBeUndefined();
    expect(parseSimVersionOutput('{"versionMinor":125}')).toBeUndefined();
  });

  it('prefers the binary, falls back to the environment, and refuses disagreement', () => {
    const reported = { ...catalog, dataHash: HASH };
    expect(resolveSimVersion({ catalog, reported, env: {} })).toEqual({
      simVersion: reported,
      dataHashSource: 'binary',
    });
    expect(resolveSimVersion({ catalog, env: { ENGINE_DATA_HASH: HASH.toUpperCase() } })).toEqual({
      simVersion: reported,
      dataHashSource: 'ENGINE_DATA_HASH',
    });
    expect(
      resolveSimVersion({ catalog, env: { ENGINE_SIM_VERSION: `125-49-${HASH}` } }).dataHashSource,
    ).toBe('ENGINE_SIM_VERSION');
    expect(() => resolveSimVersion({ catalog, env: {} })).toThrow(/ENGINE_DATA_HASH/);
    expect(() =>
      resolveSimVersion({ catalog, env: { ENGINE_SIM_VERSION: `124-49-${HASH}` } }),
    ).toThrow(/does not match the binary/);
    expect(() =>
      resolveSimVersion({ catalog, reported, env: { ENGINE_DATA_HASH: 'cd'.repeat(32) } }),
    ).toThrow(SimVersionError);
    expect(() =>
      resolveSimVersion({ catalog, reported: { ...reported, netProtocol: 48 }, env: {} }),
    ).toThrow(/--sim-version reports/);
  });

  it('only runs --sim-version when the catalog advertises it or the operator asks', async () => {
    let probes = 0;
    const engine = {
      reportedSimVersion: async () => {
        probes++;
        return { ...catalog, dataHash: HASH };
      },
    };
    const base = { ...catalog, commands: ['game'], generators: new Map() };
    await expect(detectSimVersion(engine, base, {})).rejects.toThrow(SimVersionError);
    expect(probes).toBe(0);
    expect(
      (await detectSimVersion(engine, { ...base, commands: ['sim_version'] }, {})).dataHashSource,
    ).toBe('binary');
    expect(
      (await detectSimVersion(engine, base, { ENGINE_PROBE_SIM_VERSION: '1' })).dataHashSource,
    ).toBe('binary');
    expect(probes).toBe(2);
    expect(
      (await detectSimVersion(engine, { ...base, dataHash: HASH }, {})).simVersion.dataHash,
    ).toBe(HASH);
    expect(probes).toBe(2);
  });
});

describe('process limits', () => {
  it('applies CPU and file-size limits and drops the agent environment', async () => {
    process.env['DATABASE_URL'] = 'postgres://secret@example/db';
    try {
      const result = await runProcess({
        binary: '/bin/sh',
        args: ['-c', 'ulimit -t; ulimit -f; echo "db=${DATABASE_URL:-none} home=$HOME"'],
        cwd: '/',
        env: { HOME: '/nowhere' },
        limits: { timeoutMs: 10_000, cpuSeconds: 7, fileSizeMb: 1 },
      });
      expect(result.code).toBe(0);
      const [cpu, file, env] = result.stdout.trim().split('\n');
      expect(cpu).toBe('7');
      // 512-byte blocks in POSIX sh; 1024-byte ones would only make the limit looser.
      expect(['2048', '1024']).toContain(file);
      expect(env).toBe('db=none home=/nowhere');
    } finally {
      delete process.env['DATABASE_URL'];
    }
    expect(limitPrefix({ timeoutMs: 1 })).toEqual([]);
  });

  it('kills the whole process group on timeout', async () => {
    const result = await runProcess({
      binary: '/bin/sh',
      args: ['-c', 'sleep 30 & sleep 30'],
      cwd: '/',
      limits: { timeoutMs: 300 },
    });
    expect(result.timedOut).toBe(true);
    expect(result.signal).toBe('SIGKILL');
  });
});
