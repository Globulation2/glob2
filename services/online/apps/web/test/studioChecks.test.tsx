// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { act, cleanup, renderHook } from '@testing-library/react';
import { request } from '../src/api.ts';
import { useStudioChecks } from '../src/components/studio/useStudioChecks.ts';
vi.mock('../src/api.ts', async (original) => ({ ...(await original<object>()), request: vi.fn() }));
const api = vi.mocked(request);
afterEach(() => {
  cleanup();
  api.mockReset();
});
function deferred<T>() {
  let resolve!: (value: T) => void;
  const promise = new Promise<T>((done) => {
    resolve = done;
  });
  return { promise, resolve };
}
it('ignores an old empty report read after a newer pending report has arrived', async () => {
  const old = deferred<{ items: string[] }>();
  api.mockReturnValueOnce(old.promise).mockResolvedValueOnce({ items: ['pending'] });
  const project = { current: { revision: 3 } };
  const { result } = renderHook(() => useStudioChecks<string>('/project', project));
  let late!: Promise<void>;
  act(() => {
    late = result.current.refresh();
  });
  act(() => result.current.invalidate());
  await act(() => result.current.refresh());
  await act(async () => {
    old.resolve({ items: [] });
    await late;
  });
  expect(result.current.checks).toEqual(['pending']);
});
it('discards reports read for a superseded revision or project', async () => {
  const old = deferred<{ items: string[] }>();
  api.mockReturnValueOnce(old.promise);
  const project = { current: { revision: 3 } };
  const { result, rerender } = renderHook(({ url }) => useStudioChecks<string>(url, project), {
    initialProps: { url: '/old-project' },
  });
  let late!: Promise<void>;
  act(() => {
    late = result.current.refresh();
  });
  project.current.revision = 4;
  rerender({ url: '/new-project' });
  await act(async () => {
    old.resolve({ items: ['valid'] });
    await late;
  });
  expect(result.current.checks).toEqual([]);
});
