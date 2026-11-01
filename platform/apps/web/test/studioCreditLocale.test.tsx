// @vitest-environment jsdom
import { afterEach, expect, it } from 'vitest';
import { cleanup, render, screen } from '@testing-library/react';
import { registerCatalog, setLocale } from '../../../packages/i18n/src/index.ts';
import { LocaleProvider } from '../src/i18n.tsx';
import { CreditPanel } from '../src/components/studio/Studio.tsx';

afterEach(async () => {
  cleanup();
  await setLocale('en');
});

it.each([
  ['Building', 'Pembinaan'],
  ['Terrain', 'Rupa bumi'],
])(
  'translates the %s credit domain in both the heading and accessible name',
  async (domain, label) => {
    registerCatalog('ms', {
      Building: 'Pembinaan',
      Terrain: 'Rupa bumi',
      '{value0} credits': 'Kredit {value0}',
      '{slot0}  {slot1} credit available': { other: '{slot0} kredit {slot1} tersedia' },
    });
    await setLocale('ms');
    render(
      <LocaleProvider>
        <CreditPanel domain={domain} available={2} />
      </LocaleProvider>,
    );
    expect(screen.getByRole('region', { name: `Kredit ${label}` })).toBeTruthy();
    expect(screen.getByRole('heading', { name: `2 kredit ${label} tersedia` })).toBeTruthy();
  },
);

it('selects the credit plural form using the available balance and preserves unknown balances', async () => {
  await setLocale('en');
  const view = render(<CreditPanel domain="Terrain" available={1} />);
  expect(screen.getByRole('heading', { name: '1 Terrain credit available' })).toBeTruthy();
  view.rerender(<CreditPanel domain="Terrain" available={2} />);
  expect(screen.getByRole('heading', { name: '2 Terrain credits available' })).toBeTruthy();
  view.rerender(<CreditPanel domain="Terrain" />);
  expect(screen.getByRole('heading', { name: '… Terrain credits available' })).toBeTruthy();
});
