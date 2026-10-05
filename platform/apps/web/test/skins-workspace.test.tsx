// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Skins } from '../src/pages/Skins.tsx';
vi.mock('../src/state.tsx', () => ({
  useSession: () => ({ account: null }),
  useLoad: () => ({ status: 'ready', data: { items: [] }, reload: vi.fn() }),
}));
vi.mock('../src/skins/Store.tsx', () => ({ SkinStore: () => <p>Skin store content</p> }));
vi.mock('../src/skins/MeshPreview.tsx', () => ({
  MeshPreview: ({ camera }: { camera: { zoom: number } }) => (
    <canvas aria-label="Live preview" data-zoom={camera.zoom} />
  ),
}));
beforeEach(() => {
  vi.stubGlobal(
    'ResizeObserver',
    class {
      observe() {}
      disconnect() {}
    },
  );
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue({
    fillRect: vi.fn(),
    drawImage: vi.fn(),
    putImageData: vi.fn(),
  } as unknown as CanvasRenderingContext2D);
  vi.spyOn(HTMLCanvasElement.prototype, 'toDataURL').mockReturnValue('data:image/png;base64,AAAA');
  vi.stubGlobal(
    'ImageData',
    class {
      data: Uint8ClampedArray;
      constructor(data: Uint8ClampedArray) {
        this.data = data;
      }
    },
  );
  HTMLDialogElement.prototype.showModal = function () {
    this.setAttribute('open', '');
  };
  HTMLDialogElement.prototype.close = function () {
    this.removeAttribute('open');
  };
});
afterEach(() => {
  cleanup();
  localStorage.clear();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  window.history.replaceState(null, '', '/skins');
});
it('preserves the document, model and tool when opening and closing shop', async () => {
  const { container } = render(<Skins />);
  await waitFor(() =>
    expect((screen.getByLabelText('Skin name') as HTMLInputElement).disabled).toBe(false),
  );
  const canvas = container.querySelector('canvas'),
    name = screen.getByLabelText('Skin name');
  fireEvent.change(name, { target: { value: 'My painted colony' } });
  fireEvent.click(screen.getByRole('button', { name: 'Warrior' }));
  fireEvent.click(screen.getByRole('button', { name: 'Eraser' }));
  fireEvent.click(screen.getByRole('button', { name: 'Shop' }));
  expect(screen.getByText('Skin store content')).toBeTruthy();
  expect(screen.getByRole('dialog')).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Close Skin shop' }));
  expect(screen.queryByRole('dialog')).toBeNull();
  expect(screen.getByLabelText('Skin name')).toBe(name);
  expect((name as HTMLInputElement).value).toBe('My painted colony');
  expect(container.querySelector('canvas')).toBe(canvas);
  expect(screen.getByRole('button', { name: 'Warrior' }).getAttribute('aria-pressed')).toBe('true');
  expect(screen.getByRole('button', { name: 'Eraser' }).getAttribute('aria-pressed')).toBe('true');
  expect(screen.queryByLabelText('Paint texture')).toBeNull();
});
it('opens checkout recovery as a shop dialog', async () => {
  window.history.replaceState(null, '', '/skins?purchase=paid');
  render(<Skins />);
  expect(screen.getByRole('dialog')).toBeTruthy();
  expect(screen.getByText('Skin store content')).toBeTruthy();
  await waitFor(() =>
    expect((screen.getByLabelText('Skin name') as HTMLInputElement).disabled).toBe(false),
  );
});
it('final angle is an explicit undoable document change', async () => {
  render(<Skins />);
  await waitFor(() =>
    expect((screen.getByLabelText('Skin name') as HTMLInputElement).disabled).toBe(false),
  );
  fireEvent.click(screen.getByRole('button', { name: 'Swarm' }));
  fireEvent.click(screen.getByRole('button', { name: 'Choose final view' }));
  fireEvent.change(screen.getByRole('slider', { name: /Camera angle/ }), {
    target: { value: '125' },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
  expect(screen.getByText('Final game view · 0°')).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Choose final view' }));
  fireEvent.change(screen.getByRole('slider', { name: /Camera angle/ }), {
    target: { value: '125' },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Use this view' }));
  expect(screen.getByText('Final game view · 125°')).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Undo' }));
  expect(screen.getByText('Final game view · 0°')).toBeTruthy();
});

it('keeps shortcuts out of text fields and modified browser commands', async () => {
  render(<Skins />);
  const name = screen.getByLabelText('Skin name');
  await waitFor(() => expect((name as HTMLInputElement).disabled).toBe(false));
  const brush = () => screen.getByRole('button', { name: /^Brush$/ });
  fireEvent.click(screen.getByRole('button', { name: 'Eraser' }));
  fireEvent.keyDown(window, { key: 'b', ctrlKey: true });
  expect(brush().getAttribute('aria-pressed')).toBe('false');
  fireEvent.keyDown(name, { key: 'b' });
  expect(brush().getAttribute('aria-pressed')).toBe('false');
  fireEvent.keyDown(window, { key: 'b' });
  expect(brush().getAttribute('aria-pressed')).toBe('true');
  const preview = () => screen.getByLabelText('Live preview');
  fireEvent.keyDown(name, { key: '+' });
  expect(preview().getAttribute('data-zoom')).toBe('1');
  fireEvent.keyDown(window, { key: '+' });
  expect(preview().getAttribute('data-zoom')).toBe('1.25');
  fireEvent.change(screen.getByLabelText('Camera view'), { target: { value: 'zoom-out' } });
  expect(preview().getAttribute('data-zoom')).toBe('1');
  fireEvent.click(screen.getByRole('button', { name: 'Swarm' }));
  fireEvent.click(screen.getByRole('button', { name: 'Choose final view' }));
  fireEvent.keyDown(window, { key: '+' });
  fireEvent.click(screen.getByRole('button', { name: 'Cancel' }));
  expect(preview().getAttribute('data-zoom')).toBe('1');
});
