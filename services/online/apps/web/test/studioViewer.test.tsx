// @vitest-environment jsdom
import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { MapViewer } from '../src/pages/studio/MapViewer.tsx';
import type { StudioArtifact } from '@glob2/protocol';
class DeferredImage {
  static pending: DeferredImage[] = [];
  onload: (() => void) | null = null;
  onerror: (() => void) | null = null;
  src = '';
  naturalWidth = 512;
  naturalHeight = 256;
  decode = () => Promise.resolve();
  constructor() {
    DeferredImage.pending.push(this);
  }
}
const artifact = (id: string): StudioArtifact => ({
  id,
  requestId: 'request',
  stage: 'build',
  kind: 'categorical',
  label: id,
  url: `/${id}.png`,
});
beforeEach(() => {
  DeferredImage.pending = [];
  vi.stubGlobal('Image', DeferredImage);
});
afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});
it('retains the last decoded image and zoom during delayed stage loads, then retires the old layer', async () => {
  const view = render(<MapViewer artifact={artifact('first')} />);
  await act(async () => {
    DeferredImage.pending[0]?.onload?.();
  });
  await screen.findByRole('img', { name: 'first' });
  fireEvent.click(screen.getByRole('button', { name: 'Zoom in' }));
  view.rerender(<MapViewer artifact={artifact('second')} />);
  expect(screen.getByRole('img', { name: 'first' })).toBeTruthy();
  expect(screen.getByText('Loading second…')).toBeTruthy();
  expect(screen.getByText('125%')).toBeTruthy();
  expect(view.container.querySelector('figcaption')?.textContent).toContain('first');
  await act(async () => {
    DeferredImage.pending[1]?.onload?.();
  });
  await screen.findByRole('img', { name: 'second' });
  expect(view.container.querySelector('.ms-previous-image')).toBeTruthy();
  await waitFor(() => expect(view.container.querySelector('.ms-previous-image')).toBeNull());
  expect(screen.getByText('125%')).toBeTruthy();
});
it('does not carry an image error into a different stage and supports retry without losing the decoded image', async () => {
  const view = render(<MapViewer artifact={artifact('first')} />);
  await act(async () => {
    DeferredImage.pending[0]?.onload?.();
  });
  view.rerender(<MapViewer artifact={artifact('broken')} />);
  act(() => {
    DeferredImage.pending[1]?.onerror?.();
  });
  expect(screen.getByRole('alert')).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Retry image' }));
  expect(DeferredImage.pending).toHaveLength(3);
  act(() => {
    DeferredImage.pending[2]?.onerror?.();
  });
  view.rerender(<MapViewer artifact={artifact('next')} />);
  expect(screen.queryByRole('alert')).toBeNull();
  expect(screen.getByRole('img', { name: 'first' })).toBeTruthy();
  await act(async () => {
    DeferredImage.pending[3]?.onload?.();
  });
  await screen.findByRole('img', { name: 'next' });
});
it('keeps loaded-image metadata and hides native location markers until the native image decodes', async () => {
  const view = render(
    <MapViewer
      artifact={{
        ...artifact('reference'),
        kind: 'reference',
        stage: 'prepare',
        width: 512,
        height: 256,
      }}
    />,
  );
  await act(async () => {
    DeferredImage.pending[0]?.onload?.();
  });
  view.rerender(
    <MapViewer
      artifact={{ ...artifact('native'), kind: 'preview', stage: 'ready', width: 256, height: 128 }}
      marker={{ x: 10, y: 20 }}
      dimensions={{ width: 256, height: 128 }}
    />,
  );
  expect(screen.queryByLabelText('Map location 10, 20')).toBeNull();
  expect(screen.getByText('INTERMEDIATE IMAGE')).toBeTruthy();
  expect(screen.getByText('512 × 256 preview pixels')).toBeTruthy();
  await act(async () => {
    DeferredImage.pending[1]?.onload?.();
  });
  expect(screen.getByLabelText('Map location 10, 20')).toBeTruthy();
  expect(screen.getByText('PLAYABLE MAP')).toBeTruthy();
  expect(screen.getByText('256 × 128 preview pixels')).toBeTruthy();
});
it('signals readiness after decoding and does not restart a pending image when its callback changes', async () => {
  const first = vi.fn(),
    latest = vi.fn();
  const view = render(<MapViewer artifact={artifact('ready')} onReady={first} />);
  let finish!: () => void;
  DeferredImage.pending[0]!.decode = () =>
    new Promise<void>((resolve) => {
      finish = resolve;
    });
  act(() => DeferredImage.pending[0]?.onload?.());
  expect(first).not.toHaveBeenCalled();
  view.rerender(<MapViewer artifact={artifact('ready')} onReady={latest} />);
  expect(DeferredImage.pending).toHaveLength(1);
  await act(async () => finish());
  expect(first).not.toHaveBeenCalled();
  expect(latest).toHaveBeenCalledExactlyOnceWith('/ready.png');
});
