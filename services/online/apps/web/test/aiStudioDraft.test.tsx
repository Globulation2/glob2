// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { act, cleanup, renderHook } from '@testing-library/react';
import type { AiStudioDetail, StudioDetail } from '@glob2/protocol';
import { request } from '../src/api.ts';
import { useProjectDraft } from '../src/pages/aiStudio/useProjectDraft.ts';
import { checkoutAttempt } from '../src/pages/aiStudio/checkoutAttempt.ts';
vi.mock('../src/api.ts', async (original) => ({ ...(await original<object>()), request: vi.fn() }));
const api = vi.mocked(request);
function detail(revision: number, source: string, cursor = String(revision)) {
  return {
    id: 'project',
    title: 'Colony',
    updated_at: '',
    revision,
    current: { revision, source, hash: source, reason: 'manual', created_at: '' },
    revisions: [],
    requests: [],
    cursor,
    runs: [],
  } as AiStudioDetail;
}
function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((done) => {
    resolve = done;
  });
  return { promise, resolve };
}
afterEach(() => {
  cleanup();
  vi.resetAllMocks();
  localStorage.clear();
  sessionStorage.clear();
});
it('rejects a pre-save refresh that arrives after the acknowledged revision', async () => {
  const old = deferred<AiStudioDetail>();
  api
    .mockResolvedValueOnce(detail(1, 'first'))
    .mockReturnValueOnce(old.promise)
    .mockResolvedValueOnce({ revision: 2 })
    .mockResolvedValueOnce(detail(2, 'edited'));
  const error = vi.fn();
  const { result } = renderHook(() => useProjectDraft('/project', 'draft', error));
  await act(() => result.current.refresh());
  let late!: Promise<StudioDetail | undefined>;
  act(() => {
    late = result.current.refresh();
    result.current.change('edited');
  });
  await act(() => result.current.save());
  await act(async () => {
    old.resolve(detail(1, 'first'));
    await late;
  });
  expect(result.current.source).toBe('edited');
  expect(result.current.project?.revision).toBe(2);
  expect(result.current.cursor.current).toBe('2');
  expect(error).not.toHaveBeenCalled();
});
it('keeps the newest request state when same-revision refreshes arrive out of order', async () => {
  const old = deferred<AiStudioDetail>();
  api
    .mockResolvedValueOnce(detail(1, 'first', '1'))
    .mockReturnValueOnce(old.promise)
    .mockResolvedValueOnce(detail(1, 'first', '3'));
  const { result } = renderHook(() => useProjectDraft('/project', 'draft', vi.fn()));
  await act(() => result.current.refresh());
  let late!: Promise<StudioDetail | undefined>;
  act(() => {
    late = result.current.refresh();
  });
  await act(() => result.current.refresh());
  await act(async () => {
    old.resolve(detail(1, 'first', '2'));
    await late;
  });
  expect(result.current.cursor.current).toBe('3');
});
it('serializes simultaneous save waiters and rebases recovery for edits typed during save', async () => {
  const firstSave = deferred<{ revision: number }>();
  api
    .mockResolvedValueOnce(detail(1, 'first'))
    .mockReturnValueOnce(firstSave.promise)
    .mockResolvedValueOnce(detail(2, 'second'))
    .mockResolvedValueOnce({ revision: 3 })
    .mockResolvedValueOnce(detail(3, 'third'));
  const { result } = renderHook(() => useProjectDraft('/project', 'draft', vi.fn()));
  await act(() => result.current.refresh());
  let saves!: Promise<void>[];
  act(() => {
    result.current.change('second');
    const first = result.current.save();
    result.current.change('third');
    saves = [first, result.current.save(), result.current.save()];
  });
  await act(async () => {
    firstSave.resolve({ revision: 2 });
    await Promise.all(saves);
  });
  expect(api.mock.calls.filter(([method]) => method === 'PATCH')).toHaveLength(2);
  expect(result.current.source).toBe('third');
  expect(result.current.project?.revision).toBe(3);
  expect(localStorage.getItem('draft')).toBeNull();
});
it('preserves an ambiguous checkout attempt across reloads without mixing accounts or packs', () => {
  const first = checkoutAttempt('owner', 'small');
  expect(checkoutAttempt('owner', 'small')).toEqual(first);
  expect(checkoutAttempt('other', 'small').id).not.toBe(first.id);
  expect(checkoutAttempt('owner', 'large').id).not.toBe(first.id);
});
