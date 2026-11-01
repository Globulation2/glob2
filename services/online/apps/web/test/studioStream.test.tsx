// @vitest-environment jsdom
import { act, cleanup, renderHook } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import type { StudioEvent, StudioRequest, StudioThread } from '@glob2/protocol';
import { request } from '../src/api.ts';
import { useStudioStream } from '../src/pages/studio/useStudioStream.ts';
vi.mock('../src/api.ts', () => ({ request: vi.fn() }));
const api = vi.mocked(request);
class FakeEventSource extends EventTarget {
  static instances: FakeEventSource[] = [];
  readonly close = vi.fn();
  onmessage: ((event: MessageEvent<string>) => void) | null = null;
  onopen: (() => void) | null = null;
  onerror: (() => void) | null = null;
  readonly url: string;
  constructor(url: string) {
    super();
    this.url = url;
    FakeEventSource.instances.push(this);
  }
  message(event: StudioEvent) {
    this.onmessage?.(
      new MessageEvent('message', { data: JSON.stringify(event), lastEventId: event.id }),
    );
  }
}
function snapshot(
  cursor = '10',
  requests: { id: string; status: StudioRequest['status'] }[] = [
    { id: 'generation-1', status: 'processing' },
  ],
): StudioThread {
  return {
    id: 'thread',
    title: 'River',
    cursor,
    messages: [],
    requests: requests.map(({ id, status }) => ({
      id,
      status,
      thread_id: 'thread',
      kind: 'generate',
      input: { brief: 'River', messages: [], pipelineVersion: 'v1' },
      map_id: null,
      map_hash: null,
      error: null,
      charged: status === 'ready',
      created_at: '2026-10-04T12:00:00Z',
    })),
  };
}
function event(
  id: string,
  requestId = 'generation-1',
  type: 'state' | 'complete' = 'complete',
  status: StudioRequest['status'] = 'ready',
): StudioEvent {
  return { id, requestId, type, payload: { status }, createdAt: '2026-10-04T12:00:00Z' };
}
async function mount() {
  const onChange = vi.fn();
  const hook = renderHook(() => useStudioStream('thread', onChange));
  await act(async () => {});
  return { ...hook, onChange, stream: FakeEventSource.instances[0]! };
}
async function advance(ms: number) {
  await act(async () => {
    await vi.advanceTimersByTimeAsync(ms);
  });
}
beforeEach(() => {
  vi.useFakeTimers();
  api.mockReset();
  FakeEventSource.instances = [];
  vi.stubGlobal('EventSource', FakeEventSource);
});
afterEach(() => {
  cleanup();
  vi.clearAllTimers();
  vi.useRealTimers();
  vi.unstubAllGlobals();
});

it('subscribes after the consistent initial snapshot and ignores events already covered by its cursor', async () => {
  api.mockResolvedValue(snapshot('21', [{ id: 'old', status: 'ready' }]));
  const { result, stream, onChange } = await mount();
  expect(api).toHaveBeenCalledWith('GET', '/api/v1/map-studio/threads/thread', {
    signal: expect.any(AbortSignal),
  });
  expect(stream.url).toBe('/api/v1/map-studio/threads/thread/events?cursor=21');
  expect(result.current.thread?.cursor).toBe('21');
  expect(result.current.celebrate).toBeUndefined();
  act(() => {
    stream.onopen?.();
    stream.message(event('21', 'old'));
  });
  await advance(100);
  expect(api).toHaveBeenCalledTimes(1);
  expect(onChange).not.toHaveBeenCalled();
});

it('deduplicates event IDs and refreshes once for the fresh event', async () => {
  api.mockResolvedValue(snapshot());
  const { stream, onChange, result } = await mount();
  act(() => {
    stream.onopen?.();
    stream.message(event('11'));
  });
  await advance(80);
  expect(result.current.celebrate).toBe('generation-1');
  expect(api).toHaveBeenCalledTimes(2);
  act(() => {
    stream.message(event('11'));
    stream.message(event('9'));
  });
  await advance(100);
  expect(api).toHaveBeenCalledTimes(2);
  expect(onChange).toHaveBeenCalledTimes(1);
});

it('suppresses completion replay during reconnection and celebrates a later live delivery', async () => {
  api.mockResolvedValueOnce(snapshot());
  const { stream, result } = await mount();
  act(() => {
    stream.onopen?.();
    stream.onerror?.();
  });
  let restore!: (value: StudioThread) => void;
  api.mockImplementationOnce(
    () =>
      new Promise((resolve) => {
        restore = resolve;
      }),
  );
  act(() => {
    stream.onopen?.();
    stream.message(event('11'));
  });
  expect(result.current.celebrate).toBeUndefined();
  await act(async () => {
    restore(snapshot('11', [{ id: 'generation-1', status: 'ready' }]));
  });
  api.mockResolvedValue(
    snapshot('12', [
      { id: 'generation-1', status: 'ready' },
      { id: 'generation-2', status: 'ready' },
    ]),
  );
  act(() => {
    stream.message(event('12', 'generation-2'));
  });
  expect(result.current.celebrate).toBe('generation-2');
  await advance(80);
  expect(result.current.thread?.requests.find((r) => r.id === 'generation-2')?.status).toBe(
    'ready',
  );
});

it('retries a failed terminal snapshot even when no more stream events arrive', async () => {
  api
    .mockResolvedValueOnce(snapshot())
    .mockRejectedValueOnce(new Error('temporary connection failure'))
    .mockResolvedValueOnce(snapshot('11', [{ id: 'generation-1', status: 'ready' }]));
  const { stream, result } = await mount();
  act(() => {
    stream.onopen?.();
    stream.message(event('11'));
  });
  await advance(80);
  expect(result.current.connection).toContain('Reconnecting');
  expect(result.current.thread?.requests[0]?.status).toBe('processing');
  await advance(3000);
  expect(result.current.thread?.requests[0]?.status).toBe('ready');
  expect(result.current.connection).toBe('');
  expect(api).toHaveBeenCalledTimes(3);
});

it('does not celebrate failed generation completion', async () => {
  api.mockResolvedValue(snapshot());
  const { stream, result } = await mount();
  act(() => {
    stream.onopen?.();
    stream.message(event('11', 'generation-1', 'complete', 'failed'));
  });
  expect(result.current.celebrate).toBeUndefined();
});

it('closes the stream, aborts outstanding reads, and cancels scheduled refreshes on unmount', async () => {
  api.mockResolvedValue(snapshot());
  const { stream, unmount } = await mount();
  const signal = api.mock.calls[0]![2]?.signal;
  act(() => {
    stream.message(event('11'));
  });
  unmount();
  expect(stream.close).toHaveBeenCalledTimes(1);
  expect(signal?.aborted).toBe(true);
  await advance(5000);
  expect(api).toHaveBeenCalledTimes(1);
});

it('cancels retrying an unsuccessful initial snapshot when the project unmounts', async () => {
  api.mockRejectedValue(new Error('offline'));
  const { unmount } = await mount();
  expect(FakeEventSource.instances).toHaveLength(0);
  unmount();
  await advance(5000);
  expect(api).toHaveBeenCalledTimes(1);
});

it('returns to live celebrations after the reconnect baseline initially fails', async () => {
  api.mockResolvedValueOnce(snapshot());
  const { stream, result } = await mount();
  act(() => {
    stream.onopen?.();
    stream.onerror?.();
  });
  api
    .mockRejectedValueOnce(new Error('baseline temporarily unavailable'))
    .mockResolvedValueOnce(snapshot('11', [{ id: 'generation-1', status: 'ready' }]));
  await act(async () => {
    stream.onopen?.();
  });
  api.mockResolvedValue(
    snapshot('12', [
      { id: 'generation-1', status: 'ready' },
      { id: 'generation-2', status: 'ready' },
    ]),
  );
  act(() => {
    stream.message(event('12', 'generation-2'));
  });
  expect(result.current.celebrate).toBe('generation-2');
});

it('preserves reconnect baseline intent when a normal snapshot is already in flight', async () => {
  api.mockResolvedValueOnce(snapshot());
  const { stream, result } = await mount();
  act(() => {
    stream.onopen?.();
  });
  let restore!: (value: StudioThread) => void;
  api.mockImplementationOnce(
    () =>
      new Promise((resolve) => {
        restore = resolve;
      }),
  );
  act(() => {
    stream.message(event('11', 'generation-1', 'state', 'processing'));
  });
  await advance(80);
  act(() => {
    stream.onerror?.();
    stream.onopen?.();
  });
  api.mockResolvedValue(snapshot('12', [{ id: 'generation-1', status: 'ready' }]));
  await act(async () => {
    restore(snapshot('12', [{ id: 'generation-1', status: 'ready' }]));
  });
  act(() => {
    stream.message(event('12'));
  });
  expect(result.current.celebrate).toBeUndefined();
});

it('recovers when the initial EventSource connection fails before its first open', async () => {
  api.mockResolvedValue(snapshot());
  const { stream, result } = await mount();
  act(() => {
    stream.onerror?.();
  });
  await act(async () => {
    stream.onopen?.();
  });
  act(() => {
    stream.message(event('11'));
  });
  expect(result.current.celebrate).toBe('generation-1');
});
it('does not schedule recovery after an in-flight reset snapshot rejects during unmount', async () => {
  api.mockResolvedValueOnce(snapshot());
  const { stream, unmount } = await mount();
  let reject!: (error: Error) => void;
  api.mockImplementationOnce(
    () =>
      new Promise((_resolve, fail) => {
        reject = fail;
      }),
  );
  act(() => stream.dispatchEvent(new Event('reset')));
  unmount();
  await act(async () => {
    reject(new DOMException('Aborted', 'AbortError'));
  });
  expect(vi.getTimerCount()).toBe(0);
  await advance(5000);
  expect(api).toHaveBeenCalledTimes(2);
});
