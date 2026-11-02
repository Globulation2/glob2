// @vitest-environment jsdom
import {
  act,
  cleanup,
  renderHook,
  waitFor,
  render,
  screen,
  fireEvent,
} from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { getLocale, registerCatalog, setLocale } from '../../../packages/i18n/src/index.ts';
import bg from '../../../packages/i18n/locales/bg.json';
import { LocaleProvider } from '../src/i18n.tsx';
import { RouterProvider } from '../src/router.tsx';
import { MusicDetail, MusicLibrary } from '../src/music/Library.tsx';
import { useMusicCatalogue } from '../src/music/useMusicCatalogue.ts';
import * as api from '../src/api.ts';
import type * as AppState from '../src/state.tsx';
import type { MusicList, MusicRelease } from '@glob2/protocol';
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});
it('discards an old continuation after filters change and preserves the new cursor', async () => {
  const item = (id: string) => ({ id }) as MusicRelease;
  let resolveOld: ((page: MusicList) => void) | undefined;
  vi.spyOn(api, 'request')
    .mockResolvedValueOnce({ items: [item('old-first')], next: 'old-next' })
    .mockImplementationOnce(
      () =>
        new Promise<MusicList>((resolve) => {
          resolveOld = resolve;
        }),
    )
    .mockResolvedValueOnce({ items: [item('new-first')], next: 'new-next' });
  const { result, rerender } = renderHook(({ filter }) => useMusicCatalogue(filter), {
    initialProps: { filter: 'q=old' },
  });
  await waitFor(() => expect(result.current.next).toBe('old-next'));
  act(() => {
    void result.current.more();
  });
  await waitFor(() => expect(resolveOld).toBeDefined());
  rerender({ filter: 'q=new' });
  await waitFor(() => expect(result.current.next).toBe('new-next'));
  await act(async () => {
    resolveOld?.({ items: [item('old-second')], next: 'stale-next' });
  });
  expect(result.current.items.map((r) => r.id)).toEqual(['new-first']);
  expect(result.current.next).toBe('new-next');
  expect(result.current.loading).toBe(false);
});

const ownerDraft: MusicRelease = {
  id: '11111111-1111-4111-8111-111111111111',
  ownerId: '22222222-2222-4222-8222-222222222222',
  metadata: {
    title: 'Authored title',
    artist: 'Authored artist',
    description: '',
    license: 'CC0-1.0',
    credits: '',
    sources: [],
    tags: [],
    aiGenerated: false,
  },
  status: 'draft',
  createdAt: '2026-10-31T00:00:00Z',
  frames: 0,
  tracks: [],
  warnings: [],
  inspection: null,
  uploaded: [],
  coverUrl: null,
  likes: 0,
  downloads: 0,
  liked: false,
  hidden: false,
  error: null,
};

it('uploads an owner cover to the canonical endpoint and shows its Bulgarian uploaded label', async () => {
  const previousLocale = getLocale();
  registerCatalog('bg', bg);
  const draft = ownerDraft;
  const request = vi
    .spyOn(api, 'request')
    .mockResolvedValueOnce(draft)
    .mockResolvedValueOnce(undefined)
    .mockResolvedValueOnce({ ...draft, uploaded: ['cover'] });
  try {
    await setLocale('bg');
    render(
      <LocaleProvider>
        <RouterProvider>
          <MusicDetail id={draft.id} />
        </RouterProvider>
      </LocaleProvider>,
    );
    await screen.findByRole('heading', { name: 'Authored title' });
    const cover = screen.getByLabelText(/^Корица\s*$/) as HTMLInputElement;
    expect(cover.type).toBe('file');
    expect(cover.accept).toBe('image/*');
    const file = new File(['cover bytes'], 'my-cover.png', { type: 'image/png' });
    fireEvent.change(cover, { target: { files: [file] } });
    await waitFor(() =>
      expect(request).toHaveBeenCalledWith('PUT', `/api/v1/music/${draft.id}/uploads/cover`, {
        body: file,
      }),
    );
    await screen.findByLabelText(/^Корица\s+✓ качено$/);
    expect(screen.getByRole('status').textContent).toBe(bg.Saved);
    await act(async () => setLocale('en'));
    expect(screen.getByRole('status').textContent).toBe('Saved');
    await act(async () => setLocale('bg'));
    expect(screen.getByRole('status').textContent).toBe(bg.Saved);
    expect(request.mock.calls.filter(([method]) => method === 'PUT')).toHaveLength(1);
    expect(request.mock.calls.filter(([method]) => method === 'GET')).toHaveLength(2);
  } finally {
    cleanup();
    await setLocale(previousLocale);
  }
});

vi.mock('../src/state.tsx', async (importOriginal) => ({
  ...(await importOriginal<typeof AppState>()),
  useSession: () => ({
    account: { id: '22222222-2222-4222-8222-222222222222', kind: 'registered' },
  }),
}));

it.each(['raw', 'structured'] as const)(
  'keeps %s errors separate from generated Saved success across locales',
  async (kind) => {
    const previousLocale = getLocale();
    registerCatalog('bg', bg);
    const body = {
      code: 'unavailable' as const,
      message: 'Saved',
      messageKey: 'Music is unavailable.',
    };
    const originalBody = { ...body };
    const error = kind === 'raw' ? new Error('Saved') : new api.ApiError(503, body);
    vi.spyOn(api, 'request').mockResolvedValueOnce(ownerDraft).mockRejectedValueOnce(error);
    try {
      await setLocale('bg');
      render(
        <LocaleProvider>
          <RouterProvider>
            <MusicDetail id={ownerDraft.id} />
          </RouterProvider>
        </LocaleProvider>,
      );
      const cover = await screen.findByLabelText(/^Корица\s*$/);
      fireEvent.change(cover, {
        target: { files: [new File(['bytes'], 'cover.png', { type: 'image/png' })] },
      });
      const expected =
        kind === 'raw'
          ? bg['Something went wrong. Please try again.']
          : bg['Music is unavailable.'];
      expect((await screen.findByRole('alert')).textContent).toBe(expected);
      expect(screen.queryByRole('status')).toBeNull();
      expect(screen.getByRole('alert').textContent).not.toBe(bg.Saved);
      await act(async () => setLocale('en'));
      expect(screen.getByRole('alert').textContent).toBe(
        kind === 'raw' ? 'Saved' : 'Music is unavailable.',
      );
      await act(async () => setLocale('bg'));
      expect(screen.getByRole('alert').textContent).toBe(expected);
      expect(body).toEqual(originalBody);
      if (kind === 'raw') expect(error.message).toBe('Saved');
      else expect((error as api.ApiError).body).toBe(body);
    } finally {
      cleanup();
      await setLocale(previousLocale);
    }
  },
);

it.each([false, true])(
  'preserves raw download diagnostics outside Bulgarian; explicit key=%s',
  async (structured) => {
    const previousLocale = getLocale();
    registerCatalog('bg', bg);
    const body = {
      code: 'unavailable',
      message: 'Opaque server detail: fixture-734',
      ...(structured ? { messageKey: 'Music is unavailable.' } : {}),
    };
    const originalBody = { ...body };
    vi.spyOn(api, 'request').mockResolvedValue({
      items: [{ ...ownerDraft, status: 'published' }],
      next: null,
    });
    const download = vi
      .spyOn(globalThis, 'fetch')
      .mockResolvedValue({ ok: false, status: 503, json: async () => body } as Response);
    try {
      await setLocale('en');
      render(
        <LocaleProvider>
          <RouterProvider>
            <MusicLibrary />
          </RouterProvider>
        </LocaleProvider>,
      );
      fireEvent.click(
        await screen.findByRole('checkbox', { name: 'Select for download Authored title' }),
      );
      fireEvent.click(screen.getByRole('button', { name: 'Download selected' }));
      expect((await screen.findByRole('alert')).textContent).toBe(body.message);
      expect(download).toHaveBeenCalledWith('/api/v1/music/download', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ids: [ownerDraft.id] }),
      });
      await act(async () => setLocale('bg'));
      expect(screen.getByRole('alert').textContent).toBe(
        structured ? bg['Music is unavailable.'] : bg['Something went wrong. Please try again.'],
      );
      await act(async () => setLocale('en'));
      expect(screen.getByRole('alert').textContent).toBe(body.message);
      expect(download).toHaveBeenCalledTimes(1);
      expect(body).toEqual(originalBody);
    } finally {
      cleanup();
      await setLocale(previousLocale);
    }
  },
);

it.each([false, true])(
  'uses the translated Bulgarian Like/Unlike verb in the actual catalog card aria label, liked=%s',
  async (liked) => {
    const previousLocale = getLocale();
    registerCatalog('bg', bg);
    const release = { ...ownerDraft, status: 'published' as const, likes: 2, liked };
    const metadata = { ...release.metadata };
    vi.spyOn(api, 'request').mockResolvedValue({ items: [release], next: null });
    try {
      await setLocale('bg');
      render(
        <LocaleProvider>
          <RouterProvider>
            <MusicLibrary />
          </RouterProvider>
        </LocaleProvider>,
      );
      const expected = bg['{value0} {value1} · {value2} likes']
        .replace('{value0}', liked ? bg.Unlike : bg.Like)
        .replace('{value1}', release.metadata.title)
        .replace('{value2}', '2');
      const button = await screen.findByRole('button', { name: expected });
      expect(button.getAttribute('aria-label')).toBe(expected);
      expect(button.getAttribute('aria-label')).not.toMatch(/Unlike|Like/);
      expect(button.getAttribute('aria-pressed')).toBe(String(liked));
      expect(button.textContent).toContain('2');
      await act(() => setLocale('en'));
      expect(button.getAttribute('aria-label')).toBe(
        `${liked ? 'Unlike' : 'Like'} Authored title · 2 likes`,
      );
      expect(release.metadata).toEqual(metadata);
      expect(release.liked).toBe(liked);
      expect(release.likes).toBe(2);
    } finally {
      cleanup();
      await setLocale(previousLocale);
    }
  },
);
