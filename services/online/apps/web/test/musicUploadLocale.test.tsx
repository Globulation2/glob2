// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import type { MusicRelease } from '@glob2/protocol';
import { registerCatalog, setLocale } from '../../../packages/i18n/src/index.ts';
import { LocaleProvider } from '../src/i18n.tsx';
import { RouterProvider } from '../src/router.tsx';
import { MusicDetail } from '../src/music/Library.tsx';
import * as api from '../src/api.ts';

const release: MusicRelease = {
  id: 'release',
  ownerId: 'owner',
  status: 'draft',
  createdAt: '',
  frames: 0,
  metadata: {
    title: 'My music',
    artist: 'Artist',
    description: '',
    license: 'CC0-1.0',
    credits: '',
    sources: [],
    tags: [],
    aiGenerated: false,
  },
  tracks: [],
  warnings: [],
  inspection: null,
  uploaded: ['calm', 'cover'],
  coverUrl: null,
  likes: 0,
  downloads: 0,
  liked: false,
  hidden: false,
  error: null,
};
const reload = vi.fn();
vi.mock('../src/state.tsx', () => ({
  useSession: () => ({ account: { id: 'owner' } }),
  useLoad: () => ({ status: 'ready', data: release, reload }),
}));

afterEach(async () => {
  cleanup();
  vi.restoreAllMocks();
  reload.mockClear();
  release.inspection = null;
  await setLocale('en');
});

it('uses canonical upload paths and uploaded markers with translated mood and cover labels', async () => {
  registerCatalog('ms', {
    Calm: 'Tenang',
    Building: 'Pembinaan',
    'Building (music mood)': 'Pembinaan',
    Combat: 'Pertempuran',
    Cover: 'Kulit muka',
    '✓ uploaded': '✓ dimuat naik',
  });
  await setLocale('ms');
  const request = vi.spyOn(api, 'request').mockResolvedValue({});
  render(
    <LocaleProvider>
      <RouterProvider>
        <MusicDetail id="release" />
      </RouterProvider>
    </LocaleProvider>,
  );
  for (const [kind, label] of [
    ['calm', 'Tenang'],
    ['building', 'Pembinaan'],
    ['combat', 'Pertempuran'],
    ['cover', 'Kulit muka'],
  ]) {
    const input = screen.getByLabelText(new RegExp(`^${label}`));
    expect(input.closest('label')?.textContent?.includes('✓ dimuat naik')).toBe(
      kind === 'calm' || kind === 'cover',
    );
    const file = new File(['sample'], 'sample', {
      type: kind === 'cover' ? 'image/png' : 'audio/wav',
    });
    fireEvent.change(input, { target: { files: [file] } });
    await waitFor(() =>
      expect(request).toHaveBeenCalledWith('PUT', `/api/v1/music/release/uploads/${kind}`, {
        body: file,
      }),
    );
    await waitFor(() => expect(input.hasAttribute('disabled')).toBe(false));
  }
  expect(request).toHaveBeenCalledTimes(4);
  expect(reload).toHaveBeenCalledTimes(4);
});

it('translates mood labels in inspection and length-repair summaries', async () => {
  registerCatalog('ms', {
    Calm: 'Tenang',
    Building: 'Pembinaan',
    'Building (music mood)': 'Pembinaan',
    Combat: 'Pertempuran',
  });
  await setLocale('ms');
  release.inspection = { frames: [480000, 528000, 576000], seconds: [10, 11, 12], equal: false };
  render(
    <LocaleProvider>
      <RouterProvider>
        <MusicDetail id="release" />
      </RouterProvider>
    </LocaleProvider>,
  );
  expect(
    screen.getByText('Tenang: 10.000s · Pembinaan: 11.000s · Pertempuran: 12.000s'),
  ).toBeTruthy();
  fireEvent.change(screen.getByLabelText('Length repair'), { target: { value: 'trim' } });
  expect(screen.getByText(/Tenang: 0.000s removed from end/)).toBeTruthy();
});
