// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { Skins } from '../src/pages/Skins.tsx';
vi.mock('../src/state.tsx', () => ({
  useSession: () => ({ account: null }),
  useLoad: () => ({ status: 'ready', data: { items: [] }, reload: vi.fn() }),
}));
vi.mock('../src/skins/Store.tsx', () => ({ SkinStore: () => <p>Skin store content</p> }));
vi.mock('../src/skins/MeshPreview.tsx', () => ({ MeshPreview: () => <p>Live preview</p> }));
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  window.history.replaceState(null, '', '/skins');
});
it('keeps the paint canvas and draft fields mounted when browsing library and store', () => {
  const fillRect = vi.fn();
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue({
    fillRect,
  } as unknown as CanvasRenderingContext2D);
  const { container } = render(<Skins />);
  const canvas = container.querySelector('canvas');
  const name = screen.getByRole('textbox', { name: /Skin name/ });
  fireEvent.change(name, { target: { value: 'My painted colony' } });
  fireEvent.click(screen.getByRole('button', { name: 'Store' }));
  expect(screen.queryByRole('textbox', { name: /Skin name/ })).toBeNull();
  expect(screen.getByText('Skin store content')).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'My skins' }));
  fireEvent.click(screen.getByRole('button', { name: 'Designer' }));
  expect(screen.getByRole('textbox', { name: /Skin name/ })).toBe(name);
  expect((name as HTMLInputElement).value).toBe('My painted colony');
  expect(container.querySelector('canvas')).toBe(canvas);
  expect(fillRect).toHaveBeenCalledTimes(1);
});
it('opens the store after returning from checkout', () => {
  window.history.replaceState(null, '', '/skins?purchase=paid');
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue({
    fillRect: vi.fn(),
  } as unknown as CanvasRenderingContext2D);
  render(<Skins />);
  expect(screen.getByRole('region', { name: 'Skin store' })).toBeTruthy();
  expect(screen.queryByRole('textbox', { name: /Skin name/ })).toBeNull();
});
