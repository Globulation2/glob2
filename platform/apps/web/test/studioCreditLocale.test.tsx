// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { registerCatalog, setLocale } from '../../../packages/i18n/src/index.ts';
import { LocaleProvider } from '../src/i18n.tsx';
import bg from '../../../packages/i18n/locales/bg.json';
import type { ReactNode } from 'react';
import { MapStudio } from '../src/pages/MapStudio.tsx';
import { request } from '../src/api.ts';
import { CreditPanel } from '../src/components/studio/Studio.tsx';

const walletFixture = vi.hoisted(() => ({ available: 0 }));
vi.mock('../src/state.tsx', () => {
  const account = { id: 'credit-owner', kind: 'registered' };
  return { useSession: () => ({ account }) };
});
vi.mock('../src/router.tsx', () => ({
  useRouter: () => ({ navigate: vi.fn() }),
  Link: ({ children, to }: { children: ReactNode; to: string }) => <a href={to}>{children}</a>,
}));
// The actual MapStudio header/dialog render; the unrelated map preview stays out of this fixture.
vi.mock('../src/pages/studio/StudioWorkspace.tsx', () => ({ StudioWorkspace: () => null }));
vi.mock('../src/api.ts', async (importOriginal) => ({
  ...(await importOriginal<object>()),
  request: vi.fn(async (_method: string, path: string) => {
    if (path.endsWith('/account'))
      return {
        enabled: true,
        available: walletFixture.available,
        reserved: 0,
        packs: [],
        usage: [],
      };
    if (path.endsWith('/threads'))
      return { items: [{ id: 'map-project', title: 'Authored map title' }] };
    throw new Error('Unexpected credit fixture read: ' + path);
  }),
}));

const dialogMethods = ['showModal', 'close'] as const;
const nativeDialogMethods = dialogMethods.map((method) =>
  Object.getOwnPropertyDescriptor(HTMLDialogElement.prototype, method),
);
beforeEach(() => {
  for (const method of dialogMethods)
    Object.defineProperty(HTMLDialogElement.prototype, method, {
      configurable: true,
      writable: true,
      value: function (this: HTMLDialogElement) {
        this.open = method === 'showModal';
      },
    });
});
afterEach(async () => {
  cleanup();
  dialogMethods.forEach((method, index) => {
    const descriptor = nativeDialogMethods[index];
    if (descriptor) Object.defineProperty(HTMLDialogElement.prototype, method, descriptor);
    else Reflect.deleteProperty(HTMLDialogElement.prototype, method);
  });
  sessionStorage.clear();
  vi.clearAllMocks();
  registerCatalog('bg', bg);
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

it.each([0, 1, 2])(
  'renders the map-wallet balance %i in Bulgarian visibly and accessibly while keeping its dialog category distinct',
  async (available) => {
    walletFixture.available = available;
    registerCatalog('bg', {
      ...bg,
      'Map credits': 'Кредити за карти',
      '{value0} Map credits': 'Кредити за карти: {value0}',
    });
    await setLocale('bg');
    render(
      <LocaleProvider>
        <MapStudio />
      </LocaleProvider>,
    );
    const formatted = `Кредити за карти: ${available}`;
    const wallet = await screen.findByRole('button', { name: formatted });
    expect(wallet.textContent?.trim()).toBe(formatted);
    expect(wallet.getAttribute('aria-expanded')).toBe('false');
    expect(screen.getByText('Authored map title', { exact: true })).toBeTruthy();
    fireEvent.click(wallet);
    expect(wallet.getAttribute('aria-expanded')).toBe('true');
    expect(screen.getByRole('dialog', { name: 'Кредити за карти' })).toBeTruthy();
    expect(screen.getByRole('heading', { name: 'Кредити за карти' })).toBeTruthy();
    await act(() => setLocale('en'));
    expect(wallet.textContent?.trim()).toBe(`${available} Map credits`);
    expect(wallet.getAttribute('aria-label')).toBe(`${available} Map credits`);
    expect(screen.getByRole('dialog', { name: 'Map credits' })).toBeTruthy();
    expect(vi.mocked(request).mock.calls.every(([method]) => method === 'GET')).toBe(true);
  },
);
