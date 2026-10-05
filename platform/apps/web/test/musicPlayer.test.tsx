// @vitest-environment jsdom
import { cleanup, fireEvent, render, screen, waitFor, act } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import { MusicPlayer } from '../src/music/Player.tsx';
import type { MusicRelease } from '@glob2/protocol';
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});
it('respects hidden-page loading and resets test controls after a decoder failure', async () => {
  let hidden = false;
  vi.spyOn(document, 'hidden', 'get').mockImplementation(() => hidden);
  const workers: FakeWorker[] = [];
  class FakeWorker {
    onmessage?: (event: { data: { ready?: boolean; error?: string } }) => void;
    postMessage = vi.fn();
    terminate = vi.fn();
    constructor() {
      workers.push(this);
    }
  }
  class FakeContext {
    state = 'suspended';
    destination = {};
    audioWorklet = { addModule: async () => {} };
    onstatechange?: () => void;
    async resume() {
      this.state = 'running';
      this.onstatechange?.();
    }
    async suspend() {
      this.state = 'suspended';
      this.onstatechange?.();
    }
    async close() {
      this.state = 'closed';
    }
  }
  class FakeNode {
    port = { postMessage: vi.fn(), onmessage: null };
    connect() {}
    disconnect() {}
  }
  vi.stubGlobal('Worker', FakeWorker);
  vi.stubGlobal('AudioContext', FakeContext);
  vi.stubGlobal('AudioWorkletNode', FakeNode);
  vi.stubGlobal(
    'MessageChannel',
    class {
      port1 = {};
      port2 = {};
    },
  );
  render(<MusicPlayer release={{ frames: 480000, tracks: [] } as unknown as MusicRelease} />);
  fireEvent.click(screen.getByRole('button', { name: 'Play' }));
  await waitFor(() => expect(workers).toHaveLength(1));
  act(() => {
    hidden = true;
    document.dispatchEvent(new Event('visibilitychange'));
  });
  act(() => {
    workers[0]?.onmessage?.({ data: { ready: true } });
  });
  expect(screen.queryByRole('button', { name: 'Pause' })).toBeNull();
  expect(screen.getByRole('button', { name: 'Play' })).toBeTruthy();
  expect(workers[0]?.postMessage).not.toHaveBeenCalledWith({ command: 0, value: 1 });
  fireEvent.change(screen.getByLabelText(/Fade duration/), { target: { value: '3' } });
  fireEvent.change(screen.getByLabelText(/Manual blend/), { target: { value: '1.5' } });
  fireEvent.click(screen.getByLabelText(/Audition each mood/));
  act(() => {
    workers[0]?.onmessage?.({ data: { error: 'Decoder failed' } });
  });
  expect((screen.getByLabelText(/Fade duration/) as HTMLInputElement).valueAsNumber).toBeCloseTo(
    17833 / 48000,
  );
  expect((screen.getByLabelText(/Manual blend/) as HTMLInputElement).valueAsNumber).toBe(0);
  expect((screen.getByLabelText(/Audition each mood/) as HTMLInputElement).checked).toBe(false);
  expect(screen.getByRole('alert').textContent).toBe('Decoder failed');
});
