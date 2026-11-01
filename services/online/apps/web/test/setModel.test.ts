import { expect, it } from 'vitest';
import { namespace, newPackage, newRelease, sheetFromFile } from '../src/sets/model.ts';
it('new releases preserve artwork and credit while remapping every custom dependency', () => {
  const p = newPackage('Artist'),
    key = namespace(p) + 'moss';
  p.terrains = [{ key, name: 'Moss', allowedResourceKeys: [namespace(p) + 'tree'] }];
  p.resources = [{ key: namespace(p) + 'tree' }];
  p.assets.terrains[key] = { sprite: 'data/sets/' + 'ab'.repeat(32) };
  const next = newRelease(p);
  expect(next.setId).toBe(p.setId);
  expect(next.versionId).not.toBe(p.versionId);
  expect(next.credits).toEqual(p.credits);
  expect(next.terrains[0]?.['key']).toBe(namespace(next) + 'moss');
  expect(next.terrains[0]?.['allowedResourceKeys']).toEqual([namespace(next) + 'tree']);
  expect(Object.keys(next.assets.terrains)).toEqual([namespace(next) + 'moss']);
  expect(p.terrains[0]?.['key']).toBe(key);
});
it('rejects invalid and oversized sheet grids before upload', async () => {
  const png = new Uint8Array(33);
  png.set([137, 80, 78, 71, 13, 10, 26, 10]);
  const view = new DataView(png.buffer);
  view.setUint32(16, 32);
  view.setUint32(20, 32);
  const file = { size: 33, arrayBuffer: async () => png.buffer } as File;
  for (const grid of [
    [0, 32],
    [33, 32],
    [1.5, 32],
    [65, 32],
  ])
    await expect(sheetFromFile(file, grid[0]!, grid[1]!)).rejects.toThrow();
  const sheet = await sheetFromFile(file, 32, 32);
  expect(sheet.hash).toMatch(/^[0-9a-f]{64}$/);
  expect(sheet.png).not.toMatch(/[+/=]/);
});
