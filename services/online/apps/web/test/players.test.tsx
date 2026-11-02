// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Players } from '../src/pages/Players.tsx';
import { RouterProvider } from '../src/router.tsx';
import { api } from '../src/api.ts';
import { checkPhotoBytes } from '../src/components/ProfilePhoto.tsx';
import type { PlayerDirectory } from '@glob2/protocol';
const results: PlayerDirectory = { items: [{ kind: 'ai', ai: 'nicowar', displayName: 'Nicowar' }] };
beforeEach(() => {
  Element.prototype.scrollIntoView = vi.fn();
});
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});
it('debounces search, discards stale results, and navigates by keyboard', async () => {
  vi.spyOn(window, 'scrollTo').mockImplementation(() => {});
  let stale: ((value: PlayerDirectory) => void) | undefined;
  const search = vi.spyOn(api, 'players').mockResolvedValue(results);
  render(
    <RouterProvider>
      <Players />
    </RouterProvider>,
  );
  await screen.findByRole('option');
  search.mockImplementationOnce(
    () =>
      new Promise((resolve) => {
        stale = resolve;
      }),
  );
  const input = screen.getByRole('combobox');
  fireEvent.change(input, { target: { value: 'old' } });
  await waitFor(() => expect(stale).toBeDefined());
  fireEvent.change(input, { target: { value: 'nico' } });
  await screen.findByRole('option');
  await act(async () => stale?.({ items: [{ kind: 'ai', ai: 'castor', displayName: 'Castor' }] }));
  expect(screen.queryByText('Castor')).toBeNull();
  fireEvent.keyDown(input, { key: 'Escape' });
  expect(input.getAttribute('aria-expanded')).toBe('false');
  fireEvent.keyDown(input, { key: 'ArrowDown' });
  expect(input.getAttribute('aria-expanded')).toBe('true');
  fireEvent.keyDown(input, { key: 'Enter' });
  expect(window.location.pathname).toBe('/players/ai/nicowar');
});
it('shows empty and error states and resets pagination when filters change', async () => {
  const search = vi.spyOn(api, 'players').mockResolvedValue({ ...results, nextCursor: 'next' });
  render(
    <RouterProvider>
      <Players />
    </RouterProvider>,
  );
  fireEvent.click(await screen.findByText('Next'));
  await waitFor(() =>
    expect(search).toHaveBeenLastCalledWith(
      expect.objectContaining({ cursor: 'next' }),
      expect.any(AbortSignal),
    ),
  );
  search.mockResolvedValue({ items: [] });
  fireEvent.click(screen.getByRole('button', { name: 'Humans' }));
  await screen.findByText('No players found. Try another name.');
  expect(search).toHaveBeenLastCalledWith(
    expect.objectContaining({ cursor: undefined, participants: 'humans' }),
    expect.any(AbortSignal),
  );
  search.mockRejectedValue(new Error('Search failed'));
  fireEvent.change(screen.getByRole('combobox'), { target: { value: 'someone' } });
  expect(await screen.findByRole('alert')).toHaveProperty('textContent', 'Search failed');
});
it('rejects animated and unsupported originals before cropping flattens them', () => {
  expect(() => checkPhotoBytes(new TextEncoder().encode('<svg/>').buffer)).toThrow('JPEG');
  const png = new Uint8Array(24);
  png.set([137, 80, 78, 71, 13, 10, 26, 10]);
  png.set(new TextEncoder().encode('acTL'), 12);
  expect(() => checkPhotoBytes(png.buffer)).toThrow('animation');
  const webp = new Uint8Array(24);
  webp.set(new TextEncoder().encode('RIFF'));
  webp.set(new TextEncoder().encode('WEBPANIM'), 8);
  expect(() => checkPhotoBytes(webp.buffer)).toThrow('animation');
});

it('selects the last result with ArrowUp and keeps keyboard selection visible', async () => {
  vi.spyOn(api, 'players').mockResolvedValue({
    items: [
      { kind: 'ai', ai: 'castor', displayName: 'Castor' },
      { kind: 'ai', ai: 'nicowar', displayName: 'Nicowar' },
      { kind: 'ai', ai: 'warrush', displayName: 'Warrush' },
    ],
  });
  render(
    <RouterProvider>
      <Players />
    </RouterProvider>,
  );
  await screen.findAllByRole('option');
  const input = screen.getByRole('combobox');
  fireEvent.keyDown(input, { key: 'ArrowUp' });
  const options = screen.getAllByRole('option');
  expect(options[2]?.getAttribute('aria-selected')).toBe('true');
  expect(Element.prototype.scrollIntoView).toHaveBeenCalledWith({ block: 'nearest' });
  fireEvent.keyDown(input, { key: 'ArrowDown' });
  expect(options[0]?.getAttribute('aria-selected')).toBe('true');
});
it('retries a failed directory request without changing the search', async () => {
  const search = vi
    .spyOn(api, 'players')
    .mockRejectedValueOnce(new Error('Offline'))
    .mockResolvedValue(results);
  render(
    <RouterProvider>
      <Players />
    </RouterProvider>,
  );
  fireEvent.click(await screen.findByRole('button', { name: 'Try again' }));
  await screen.findByRole('option');
  expect(search).toHaveBeenCalledTimes(2);
});
