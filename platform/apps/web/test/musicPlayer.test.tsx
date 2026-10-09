// @vitest-environment jsdom
import { StrictMode } from 'react';
import { cleanup, fireEvent, render, screen, waitFor, act } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { MusicPlayer } from '../src/music/Player.tsx';
import type { MusicRelease } from '@glob2/protocol';
import { GAME_FADE_SECONDS } from '../src/music/useMusicPlayback.ts';
let hidden: boolean;
let workers: FakeWorker[], contexts: FakeContext[], nodes: FakeNode[];
class FakeWorker {
  onmessage?: (event: { data: { ready?: boolean; error?: string } }) => void;
  onerror?: () => void;
  postMessage = vi.fn();
  terminate = vi.fn();
  constructor() {
    workers.push(this);
  }
}
class FakeContext {
  state = 'suspended';
  destination = {};
  gain = { gain: { value: 1 }, connect: vi.fn(), disconnect: vi.fn() };
  audioWorklet = { addModule: vi.fn(async () => {}) };
  onstatechange?: () => void;
  constructor() {
    contexts.push(this);
  }
  createGain() {
    return this.gain;
  }
  async resume() {
    this.state = 'running';
    this.onstatechange?.();
  }
  async suspend() {
    this.state = 'suspended';
    this.onstatechange?.();
  }
  close = vi.fn(async () => {
    this.state = 'closed';
  });
}
class FakeNode {
  port = {
    postMessage: vi.fn(),
    onmessage: null as null | ((event: { data: { position: number; weights: number[] } }) => void),
  };
  connect = vi.fn();
  disconnect = vi.fn();
  constructor() {
    nodes.push(this);
  }
}
const release = { id: 'music1', frames: 480000, tracks: [] } as unknown as MusicRelease;
beforeEach(() => {
  hidden = false;
  workers = [];
  contexts = [];
  nodes = [];
  vi.spyOn(document, 'hidden', 'get').mockImplementation(() => hidden);
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
});
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});
async function start() {
  fireEvent.click(screen.getByRole('button', { name: 'Play music' }));
  await waitFor(() => expect(workers).toHaveLength(1));
  act(() => workers[0]?.onmessage?.({ data: { ready: true } }));
}
it('applies a preselected mood and seek before starting; uses one volume gain', async () => {
  render(<MusicPlayer release={release} />);
  expect(workers).toHaveLength(0);
  fireEvent.click(screen.getByRole('button', { name: 'Crossfade to Combat' }));
  fireEvent.change(screen.getByRole('slider', { name: 'Playback position' }), {
    target: { value: '4' },
  });
  fireEvent.change(screen.getByRole('slider', { name: 'Volume' }), { target: { value: '0.4' } });
  await start();
  const calls = workers[0]?.postMessage.mock.calls;
  expect(calls).toContainEqual([{ command: 2, value: 4 }]);
  expect(calls).toContainEqual([{ command: 4, value: 2 }]);
  expect(calls?.at(-1)).toEqual([{ command: 0, value: 1 }]);
  expect(contexts[0]?.gain.gain.value).toBe(0.4);
  fireEvent.click(screen.getByRole('button', { name: 'Mute' }));
  expect(contexts[0]?.gain.gain.value).toBe(0);
  fireEvent.click(screen.getByRole('button', { name: 'Unmute' }));
  expect(contexts[0]?.gain.gain.value).toBe(0.4);
});
it('respects hidden-page loading and does not start automatically when visible again', async () => {
  render(<MusicPlayer release={release} />);
  fireEvent.click(screen.getByRole('button', { name: 'Play music' }));
  await waitFor(() => expect(workers).toHaveLength(1));
  act(() => {
    hidden = true;
    document.dispatchEvent(new Event('visibilitychange'));
  });
  act(() => workers[0]?.onmessage?.({ data: { ready: true } }));
  expect(screen.queryByRole('button', { name: 'Pause' })).toBeNull();
  expect(workers[0]?.postMessage).not.toHaveBeenCalledWith({ command: 0, value: 1 });
  act(() => {
    hidden = false;
    document.dispatchEvent(new Event('visibilitychange'));
  });
  expect(screen.getByRole('button', { name: 'Play music' })).toBeTruthy();
});
it('makes automatic preview, manual blend, and mood selection mutually exclusive', async () => {
  render(<MusicPlayer release={release} />);
  await start();
  fireEvent.change(screen.getByLabelText(/Fade duration/), { target: { value: '3' } });
  fireEvent.click(screen.getByRole('button', { name: 'Preview game transitions' }));
  expect(workers[0]?.postMessage).toHaveBeenCalledWith({ command: 6, value: 0 });
  expect((screen.getByLabelText(/Fade duration/) as HTMLInputElement).valueAsNumber).toBeCloseTo(
    GAME_FADE_SECONDS,
  );
  act(() => nodes[0]?.port.onmessage?.({ data: { position: 8, weights: [0, 1, 0] } }));
  expect(
    screen.getByRole('button', { name: 'Crossfade to Building' }).getAttribute('aria-pressed'),
  ).toBe('true');
  fireEvent.change(screen.getByLabelText(/Manual blend/), { target: { value: '1.5' } });
  expect(screen.queryByRole('button', { name: 'Stop transition preview' })).toBeNull();
  expect(screen.getByText('Custom mood blend')).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Crossfade to Calm' }));
  expect(screen.queryByText('Custom mood blend')).toBeNull();
  expect(workers[0]?.postMessage).toHaveBeenCalledWith({ command: 1, value: 0 });
});
it('starts guided preview on explicit intent and stops the sequence when paused', async () => {
  render(<MusicPlayer release={release} />);
  fireEvent.click(screen.getByRole('button', { name: 'Preview game transitions' }));
  await waitFor(() => expect(workers).toHaveLength(1));
  act(() => workers[0]?.onmessage?.({ data: { ready: true } }));
  expect(screen.getByRole('button', { name: 'Stop transition preview' })).toBeTruthy();
  expect(workers[0]?.postMessage).toHaveBeenCalledWith({ command: 5, value: 1 });
  act(() => nodes[0]?.port.onmessage?.({ data: { position: 4, weights: [1, 0, 0] } }));
  fireEvent.click(screen.getByRole('button', { name: 'Pause' }));
  expect(workers[0]?.postMessage).toHaveBeenCalledWith({ command: 5, value: 0 });
  expect(
    (screen.getByRole('slider', { name: 'Playback position' }) as HTMLInputElement).valueAsNumber,
  ).toBe(4);
  fireEvent.click(screen.getByRole('button', { name: 'Play music' }));
  await screen.findByRole('button', { name: 'Pause' });
  expect(workers).toHaveLength(1);
});
it('cleans up a failed decoder, retries, and ignores stale events from the old player', async () => {
  const view = render(<MusicPlayer release={release} />);
  await start();
  fireEvent.change(screen.getByLabelText(/Fade duration/), { target: { value: '3' } });
  act(() => workers[0]?.onmessage?.({ data: { error: 'Aborted(CompilerError: blocked by CSP)' } }));
  expect(screen.getByRole('alert').textContent).toContain('Music couldn’t be loaded');
  expect(screen.getByRole('alert').textContent).not.toContain('CompilerError');
  expect(workers[0]?.terminate).toHaveBeenCalled();
  expect(contexts[0]?.close).toHaveBeenCalled();
  expect((screen.getByLabelText(/Fade duration/) as HTMLInputElement).valueAsNumber).toBeCloseTo(
    GAME_FADE_SECONDS,
  );
  fireEvent.click(screen.getByRole('button', { name: 'Retry' }));
  await waitFor(() => expect(workers).toHaveLength(2));
  act(() => {
    workers[0]?.onmessage?.({ data: { error: 'old failure' } });
    workers[1]?.onmessage?.({ data: { ready: true } });
  });
  expect(screen.queryByRole('alert')).toBeNull();
  expect(screen.getByRole('button', { name: 'Pause' })).toBeTruthy();
  view.unmount();
  expect(workers[1]?.terminate).toHaveBeenCalled();
  expect(nodes[1]?.disconnect).toHaveBeenCalled();
  expect(contexts[1]?.gain.disconnect).toHaveBeenCalled();
});
it('reports comparison settings and resumes only an authorized visible comparison', async () => {
  const snapshot = vi.fn();
  render(
    <MusicPlayer
      release={release}
      initialSettings={{ position: 3, mood: 1, volume: 0.3, muted: true, playing: true }}
      onSnapshot={snapshot}
    />,
  );
  await waitFor(() => expect(workers).toHaveLength(1));
  act(() => workers[0]?.onmessage?.({ data: { ready: true } }));
  expect(snapshot).toHaveBeenLastCalledWith({
    position: 3,
    mood: 1,
    volume: 0.3,
    muted: true,
    playing: true,
  });
  expect(contexts[0]?.gain.gain.value).toBe(0);
});
it('cleans up initialization interrupted before the worklet loads', async () => {
  let finish: (() => void) | undefined;
  vi.spyOn(FakeContext.prototype, 'resume').mockImplementation(
    () =>
      new Promise<void>((resolve) => {
        finish = resolve;
      }),
  );
  const view = render(<MusicPlayer release={release} />);
  fireEvent.click(screen.getByRole('button', { name: 'Play music' }));
  view.unmount();
  await act(async () => finish?.());
  expect(contexts[0]?.close).toHaveBeenCalled();
  expect(workers).toHaveLength(0);
});
it('initializes an authorized comparison correctly through Strict Mode effect replay', async () => {
  render(
    <StrictMode>
      <MusicPlayer release={release} initialSettings={{ playing: true }} />
    </StrictMode>,
  );
  await waitFor(() => expect(workers).toHaveLength(1));
  act(() => workers[0]?.onmessage?.({ data: { ready: true } }));
  expect(screen.getByRole('button', { name: 'Pause' })).toBeTruthy();
  expect(contexts[0]?.close).toHaveBeenCalled();
});
it('preserves manual mixing when changing fade duration or pausing', async () => {
  render(<MusicPlayer release={release} />);
  await start();
  fireEvent.change(screen.getByLabelText(/Manual blend/), { target: { value: '0.5' } });
  fireEvent.change(screen.getByLabelText(/Fade duration/), { target: { value: '2' } });
  expect(screen.getByText('Custom mood blend')).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Pause' }));
  expect(screen.getByText('Custom mood blend')).toBeTruthy();
});
it('keeps the audible mood selected when stopping a guided transition', async () => {
  render(<MusicPlayer release={release} />);
  await start();
  fireEvent.click(screen.getByRole('button', { name: 'Preview game transitions' }));
  act(() => nodes[0]?.port.onmessage?.({ data: { position: 8.2, weights: [0.2, 0.8, 0] } }));
  fireEvent.click(screen.getByRole('button', { name: 'Stop transition preview' }));
  expect(workers[0]?.postMessage).toHaveBeenLastCalledWith({ command: 1, value: 1 });
  expect(
    screen.getByRole('button', { name: 'Crossfade to Building' }).getAttribute('aria-pressed'),
  ).toBe('true');
  expect(screen.getByRole('button', { name: 'Pause' })).toBeTruthy();
});
