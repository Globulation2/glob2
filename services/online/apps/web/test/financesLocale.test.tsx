// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { registerCatalog, setLocale } from '../../../packages/i18n/src/index.ts';
import { Finances } from '../src/admin/Finances.tsx';
import { RouterProvider } from '../src/router.tsx';
import { request } from '../src/api.ts';
import { measureName, productName, libraryName } from '../src/admin/presentation.ts';

vi.mock('../src/api.ts', () => ({ request: vi.fn() }));
afterEach(async () => {
  cleanup();
  vi.mocked(request).mockReset();
  await setLocale('en');
  window.history.replaceState(null, '', '/');
});

it('keeps financial totals and submitted modes canonical when their display labels change', async () => {
  registerCatalog('ms', {
    Payment: 'Pembayaran',
    Refund: 'Bayaran balik',
    'Live payments': 'Pembayaran sebenar',
    'Test payments': 'Pembayaran ujian',
    'Unclassified payments': 'Pembayaran tidak dikelaskan',
  });
  await setLocale('ms');
  vi.mocked(request).mockResolvedValue({
    generatedAt: '2026-01-02T12:00:00Z',
    days: 30,
    mode: 'live',
    historicalIncomplete: false,
    unknownPurchases: 0,
    credits: [],
    costs: [],
    cash: [
      {
        product: 'maps',
        currency: 'usd',
        mode: 'live',
        period: 'current',
        kind: 'payment',
        amount: 12345,
        events: 1,
      },
      {
        product: 'maps',
        currency: 'usd',
        mode: 'live',
        period: 'current',
        kind: 'refund',
        amount: 1000,
        events: 1,
      },
      {
        product: 'maps',
        currency: 'usd',
        mode: 'live',
        period: 'previous',
        kind: 'payment',
        amount: 90000,
        events: 1,
      },
      {
        product: 'music',
        currency: 'jpy',
        mode: 'live',
        period: 'current',
        kind: 'payment',
        amount: 500,
        events: 1,
      },
    ],
  } as never);
  const view = render(
    <RouterProvider>
      <Finances />
    </RouterProvider>,
  );
  await screen.findAllByText('USD', { selector: 'td' });
  const summaries = [...view.container.querySelectorAll('p')].map((p) =>
    p.textContent?.replace(/\s/g, ''),
  );
  expect(summaries).toContain(
    'USD:collectedUSD123.45·refundedUSD10.00·collectedlessrefundsUSD113.45',
  );
  expect(summaries).toContain('JPY:collectedJPY500·refundedJPY0·collectedlessrefundsJPY500');
  const mode = screen.getByLabelText('Payment mode') as HTMLSelectElement;
  expect([...mode.options].map((option) => [option.value, option.textContent])).toEqual([
    ['live', 'Pembayaran sebenar'],
    ['test', 'Pembayaran ujian'],
    ['unclassified', 'Pembayaran tidak dikelaskan'],
  ]);
  fireEvent.change(mode, { target: { value: 'test' } });
  await waitFor(() =>
    expect(request).toHaveBeenLastCalledWith(
      'GET',
      '/api/v1/admin/finances',
      expect.objectContaining({ query: expect.objectContaining({ mode: 'test' }) }),
    ),
  );
});

it('translates known report captions without changing unknown diagnostic identifiers', async () => {
  registerCatalog('ms', {
    'Map Studio': 'Studio Peta',
    AI: 'AI',
    'Terrain & resources': 'Rupa bumi & sumber',
    'Matches started': 'Perlawanan dimulakan',
    '{value0} publications': 'Penerbitan {value0}',
    '{value0} recorded downloads': 'Muat turun {value0} yang direkodkan',
    '{value0} completion time (seconds)': 'Masa penyiapan {value0} (saat)',
    '{value0} completion samples': 'Sampel penyiapan {value0}',
  });
  await setLocale('ms');
  expect(productName('maps')).toBe('Studio Peta');
  expect(libraryName('sets')).toBe('Rupa bumi & sumber');
  expect(measureName('matches.started', '')).toBe('Perlawanan dimulakan');
  expect(measureName('library.published', 'maps')).toBe('Penerbitan Maps');
  expect(measureName('duration.maps', 'seconds')).toBe('Masa penyiapan Studio Peta (saat)');
  expect(measureName('duration.maps', 'samples')).toBe('Sampel penyiapan Studio Peta');
  expect(measureName('future.metric', 'future.dimension')).toBe('future.metric future.dimension');
});
