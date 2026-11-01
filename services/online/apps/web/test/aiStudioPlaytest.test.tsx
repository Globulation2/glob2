// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import Playtest from '../src/pages/aiStudio/Playtest.tsx';
const run = { runId: 'run', revision: 3, source: 'source', seed: 19, opponent: 'numbi' };
function setup() {
  const result = vi.fn();
  const view = render(<Playtest run={run} onResult={result} />);
  const frame = view.container.querySelector('iframe')!;
  vi.spyOn(frame.contentWindow!, 'postMessage').mockImplementation(() => {});
  const message = (body: object) =>
    fireEvent(
      window,
      new MessageEvent('message', {
        origin: location.origin,
        source: frame.contentWindow,
        data: {
          channel: 'glob2-ai-studio',
          version: 1,
          runId: run.runId,
          revision: run.revision,
          ...body,
        },
      }),
    );
  return { result, message };
}
afterEach(() => {
  cleanup();
  vi.useRealTimers();
  vi.unstubAllGlobals();
});
it('records map-fetch launch failures once instead of only changing the label', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('', { status: 503 })));
  const { result, message } = setup();
  await act(async () => {
    message({ type: 'ready' });
  });
  expect(result).toHaveBeenCalledOnce();
  expect(JSON.parse(result.mock.calls[0]?.[0])).toMatchObject({
    revision: 3,
    result: 'Error: Could not load test map.',
  });
  message({ type: 'progress', tick: 100 });
  expect(screen.getByRole('status').textContent).toContain('Playtest failed');
});
it('reports startup timeout as a bounded run failure', () => {
  vi.useFakeTimers();
  const { result } = setup();
  act(() => {
    vi.advanceTimersByTime(90000);
  });
  expect(result).toHaveBeenCalledOnce();
  expect(result.mock.calls[0]?.[0]).toContain('did not start in time');
});
it('keeps terminal status until a new iframe readiness handshake', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('map')));
  const { result, message } = setup();
  message({ type: 'complete', result: 'won', tick: 100 });
  message({ type: 'progress', tick: 101 });
  expect(screen.getByRole('status').textContent).toBe('Finished: won');
  await act(async () => {
    message({ type: 'ready' });
  });
  message({ type: 'progress', tick: 1 });
  expect(screen.getByRole('status').textContent).toContain('tick 1 · live');
  expect(result).toHaveBeenCalledOnce();
});
