import { afterEach, expect, it } from 'vitest';
import { setLocale } from '../../../packages/i18n/src/index.ts';
import { t } from '../src/messages.ts';
import { MOODS, musicMoodLabel } from '../src/music/Player.tsx';

afterEach(async () => {
  await setLocale('en');
});
it('distinguishes building objects, construction music and generation progress across switches', async () => {
  const identifiers = JSON.stringify(MOODS);
  await setLocale('en');
  expect(musicMoodLabel('Building')).toBe('Building');
  await setLocale('th');
  expect(t('Building')).toBe('อาคาร');
  expect(musicMoodLabel('Building')).toBe('การก่อสร้าง');
  expect(t('Building (generation progress)')).toBe('กำลังสร้าง');
  expect(t('Broken')).toBe('ใช้งานไม่ได้');
  await setLocale('fr');
  expect(musicMoodLabel('Building')).toBe(t('Building'));
  expect(JSON.stringify(MOODS)).toBe(identifiers);
});
