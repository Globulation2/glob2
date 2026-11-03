import { afterEach, expect, it } from 'vitest';
import { setLocale } from '../../../packages/i18n/src/index.ts';
import { displayMessage, message, sourceMessage, statusText, t } from '../src/messages.ts';

afterEach(async () => {
  await setLocale('en');
});

it('resolves an owned status in the current locale without recreating it', async () => {
  const status = sourceMessage('Private');
  await setLocale('en');
  expect(statusText(status)).toBe('Private');
  await setLocale('th');
  expect(statusText(status)).toBe('ส่วนตัว');
  await setLocale('en');
  expect(statusText(status)).toBe('Private');
});

it('resolves owned parameter labels at display time and preserves authored values', async () => {
  const status = sourceMessage('Status: {slot0}', () => ({ slot0: t('Private') }));
  await setLocale('en');
  expect(statusText(status)).toBe('Status: Private');
  await setLocale('th');
  expect(statusText(status)).toBe('สถานะ : ส่วนตัว');
  const authored = 'Private / ผู้แต่ง 🐜';
  expect(statusText(sourceMessage('Status: {slot0}', { slot0: authored }))).toBe(
    'สถานะ : ' + authored,
  );
});

it('never translates or rewrites raw runtime diagnostics, even if they match a catalog key', async () => {
  await setLocale('th');
  expect(statusText('Private')).toBe('Private');
  expect(statusText('code_key\nraw ผู้แต่ง 🐜')).toBe('code_key\nraw ผู้แต่ง 🐜');
});

it('preserves the existing message and displayMessage contracts', async () => {
  await setLocale('th');
  expect(message('Private')).toBe('Private');
  expect(displayMessage('Private')).toBe('ส่วนตัว');
});
