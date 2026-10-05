// @vitest-environment jsdom
import { act, cleanup, renderHook, waitFor } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { useMusicCatalogue } from '../src/music/useMusicCatalogue.ts';
import * as api from '../src/api.ts';
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
