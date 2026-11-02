// @vitest-environment jsdom
import { act, cleanup, renderHook } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { useLibrarySearch } from '../src/components/library.tsx';

afterEach(() => {
  cleanup();
  vi.useRealTimers();
});

it('debounces trimmed search and immediately flushes Enter without a duplicate request', () => {
  vi.useFakeTimers();
  const apply = vi.fn();
  const { result, rerender } = renderHook(
    ({ draft, query }) => useLibrarySearch(draft, query, apply),
    {
      initialProps: { draft: '', query: '' },
    },
  );
  rerender({ draft: '  moss ', query: '' });
  act(() => {
    vi.advanceTimersByTime(249);
  });
  expect(apply).not.toHaveBeenCalled();
  act(() => {
    vi.advanceTimersByTime(1);
  });
  expect(apply).toHaveBeenLastCalledWith('moss');
  rerender({ draft: 'forest', query: 'moss' });
  act(() => result.current());
  expect(apply).toHaveBeenLastCalledWith('forest');
  const calls = apply.mock.calls.length;
  act(() => {
    vi.advanceTimersByTime(300);
  });
  expect(apply).toHaveBeenCalledTimes(calls);
});

it('cancels pending typing on reset, external navigation, and unmount', () => {
  vi.useFakeTimers();
  const apply = vi.fn();
  const { rerender, unmount } = renderHook(
    ({ draft, query }) => useLibrarySearch(draft, query, apply),
    {
      initialProps: { draft: 'old', query: '' },
    },
  );
  rerender({ draft: '', query: '' });
  act(() => {
    vi.advanceTimersByTime(300);
  });
  expect(apply).not.toHaveBeenCalled();
  rerender({ draft: 'pending', query: '' });
  rerender({ draft: 'restored', query: 'restored' });
  act(() => {
    vi.advanceTimersByTime(300);
  });
  expect(apply).not.toHaveBeenCalled();
  rerender({ draft: 'another', query: 'restored' });
  unmount();
  act(() => {
    vi.advanceTimersByTime(300);
  });
  expect(apply).not.toHaveBeenCalled();
});
