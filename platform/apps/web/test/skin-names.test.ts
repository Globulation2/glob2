import { afterEach, expect, it } from 'vitest';
import { registerCatalog, setLocale } from '../../../packages/i18n/src/index.ts';
import { skinCopyName } from '../src/skins/skinNames.ts';

afterEach(async () => {
  await setLocale('en');
});

it('keeps short authored names and reserves the English copy suffix', async () => {
  await setLocale('en');
  expect(skinCopyName('Authored name')).toBe('Authored name copy');
  expect(skinCopyName('x'.repeat(64))).toBe('x'.repeat(59) + ' copy');
});

it('fits the translated suffix without splitting Thai combining clusters', async () => {
  registerCatalog('th', { '{value0} copy': '{value0} (สำเนา)' });
  await setLocale('th');
  expect(skinCopyName('ก้'.repeat(32))).toBe('ก้'.repeat(28) + ' (สำเนา)');
});

it('keeps emoji graphemes whole and includes RTL isolation in the length budget', async () => {
  registerCatalog('ar', { '{value0} copy': 'نسخة {value0}' });
  await setLocale('ar');
  const family = '👨‍👩‍👧‍👦';
  const result = skinCopyName(family.repeat(9));
  expect(result.length).toBeLessThanOrEqual(64);
  expect(Array.from(result).length).toBeLessThanOrEqual(64);
  expect(result.replaceAll(family, '')).not.toMatch(/[👨👩👧👦\u200d]/u);
  expect(result).toContain(family);
});
