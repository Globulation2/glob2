import { expect, it } from 'vitest';
import { pathAvailable } from '../src/features.tsx';
it('keeps community and account pages available while hiding disabled paid workspaces', () => {
  for (const path of ['/maps', '/music', '/ais', '/generators', '/buildings', '/account', '/rooms'])
    expect(pathAvailable(path, []), path).toBe(true);
  for (const path of [
    '/map-studio',
    '/music-studio/id?tab=credits',
    '/terrain-studio',
    '/ai-building-studio',
    '/ai-studio',
    '/generator-studio',
    '/commander',
    '/skins/store',
  ])
    expect(pathAvailable(path, []), path).toBe(false);
  expect(pathAvailable('/ai-studio/project', ['ai-studio'])).toBe(true);
  expect(pathAvailable('/skins/store', ['skins.sales'])).toBe(false);
  expect(pathAvailable('/skins/store', ['skins.designer'])).toBe(true);
});
