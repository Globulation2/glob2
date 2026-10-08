import { describe, expect, it, vi } from 'vitest';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import type { EngineCatalog } from '../src/engineCli.ts';
import { GlobEngine, DEFAULT_LIMITS } from '../src/engine.ts';
import type { RunOptions } from '../src/process.ts';
import { parseBuildingComposition } from '../src/engineCli.ts';
const baseHash = 'a'.repeat(64);
const snapshot = JSON.stringify({
  schemaVersion: 1,
  catalogKey: 'stock',
  variants: [],
  experiments: [],
});
const hash = createHash('sha256').update(snapshot).digest('hex');
const result = { schemaVersion: 1, baseHash, catalog: { snapshot, hash } };
describe('engine building composition output', () => {
  it('keeps package and artwork decoding behind the configured process isolation boundary', async () => {
    const namespace = '11111111-1111-4111-8111-111111111111';
    const pkg = {
      schemaVersion: 1 as const,
      namespace,
      variants: [{ key: `b-${namespace}-kitchen`, properties: {}, semantics: {} }],
      experiments: [],
      sprites: [],
    };
    const artwork = Buffer.from('checked by the isolated engine');
    const artworkHash = createHash('sha256').update(artwork).digest('hex');
    const signal = new AbortController().signal;
    const launcher = vi.fn(async (options: RunOptions, scratch: string) => {
      expect(options.args[0]).toBe('--compose-buildings');
      expect(options.signal).toBe(signal);
      expect(options.maxCaptureBytes).toBe(32 * 1024 * 1024);
      const packagePath = options.args[options.args.indexOf('--package') + 1]!;
      const artworkPath = options.args[options.args.indexOf('--artwork-bundle') + 1]!;
      expect(packagePath.startsWith(scratch + '/')).toBe(true);
      expect(artworkPath.startsWith(scratch + '/')).toBe(true);
      expect(JSON.parse(await readFile(packagePath, 'utf8'))).toEqual(pkg);
      expect(await readFile(artworkPath)).toEqual(artwork);
      return {
        code: 0,
        signal: null,
        timedOut: false,
        stdout: JSON.stringify({ ...result, artworkHash }),
        stderr: '',
        ms: 1,
      };
    });
    const engine = new GlobEngine({
      binary: '/unused',
      workdir: '/unused',
      limits: DEFAULT_LIMITS,
      maxOutputBytes: 72 * 1024 * 1024,
      processLauncher: launcher,
    });
    vi.spyOn(engine, 'catalog').mockResolvedValue({
      commands: ['compose_buildings'],
      buildingCatalogHash: baseHash,
    } as EngineCatalog);
    expect(await engine.composeBuildings([pkg], signal, artwork)).toEqual({
      ...result,
      artworkHash,
    });
    expect(launcher).toHaveBeenCalledOnce();
  });
  it('retains exact canonical bytes and binds the base catalog', () => {
    expect(parseBuildingComposition(JSON.stringify(result), baseHash)).toEqual(result);
  });
  it('requires the engine to check the exact optional artwork bundle', () => {
    const artworkHash = 'd'.repeat(64);
    expect(
      parseBuildingComposition(JSON.stringify({ ...result, artworkHash }), baseHash, artworkHash)
        .artworkHash,
    ).toBe(artworkHash);
    expect(() => parseBuildingComposition(JSON.stringify(result), baseHash, artworkHash)).toThrow(
      /artwork/,
    );
    expect(() =>
      parseBuildingComposition(JSON.stringify({ ...result, artworkHash: 'bad' }), baseHash),
    ).toThrow(/artwork/);
  });
  it('rejects wrong bases, schemas, digests, and oversized output', () => {
    for (const bad of [
      { ...result, baseHash: 'b'.repeat(64) },
      { ...result, schemaVersion: 2 },
      { ...result, catalog: { snapshot, hash: 'c'.repeat(64) } },
      { ...result, catalog: { snapshot: 'x'.repeat(8 * 1024 * 1024 + 1), hash } },
    ])
      expect(() => parseBuildingComposition(JSON.stringify(bad), baseHash)).toThrow();
  });
});
