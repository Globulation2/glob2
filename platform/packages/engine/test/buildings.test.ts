import { describe, expect, it } from 'vitest';
import { createHash } from 'node:crypto';
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
