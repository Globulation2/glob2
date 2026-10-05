// @vitest-environment jsdom
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, expect, it } from 'vitest';
import { MapPreview } from '../src/components/MapPreview.tsx';

afterEach(cleanup);

function ready(container: HTMLElement, width = 512, height = 256) {
  const loader = container.querySelector('.map-preview-loader')!;
  Object.defineProperties(loader, {
    naturalWidth: { value: width },
    naturalHeight: { value: height },
  });
  fireEvent.load(loader);
}

it('fits rectangular previews and allows keyboard exploration and reset', () => {
  const view = render(<MapPreview src="/map.png" alt="Two rivers" />);
  expect(screen.getByText('Loading map preview…')).toBeTruthy();
  ready(view.container);
  const surface = screen.getByRole('group', { name: 'Two rivers' });
  expect(surface.style.aspectRatio).toBe('512 / 256');
  fireEvent.keyDown(surface, { key: 'ArrowLeft' });
  fireEvent.keyDown(surface, { key: 'ArrowUp' });
  const tile = view.container.querySelectorAll<HTMLImageElement>('.map-preview-tile')[3]!;
  expect(tile.style.left).toBe('95%');
  expect(tile.style.top).toBe('95%');
  fireEvent.keyDown(surface, { key: 'Home' });
  expect(tile.style.left).toBe('0%');
  fireEvent.keyDown(surface, { key: 'ArrowRight' });
  fireEvent.click(screen.getByRole('button', { name: 'Reset view' }));
  expect(tile.style.left).toBe('0%');
});

it('clears the old view when the preview source changes and provides missing/error states', () => {
  const view = render(<MapPreview src="/first.png" alt="Map" />);
  ready(view.container);
  fireEvent.keyDown(screen.getByRole('group'), { key: 'ArrowDown' });
  view.rerender(<MapPreview src="/second.png" alt="Map" />);
  expect(view.container.querySelector('.map-preview-tile')).toBeNull();
  fireEvent.error(view.container.querySelector('.map-preview-loader')!);
  expect(screen.getByText('This preview could not be loaded.')).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Retry preview' }));
  ready(view.container);
  expect(view.container.querySelectorAll<HTMLImageElement>('.map-preview-tile')[3]!.style.top).toBe(
    '0%',
  );
  view.rerender(<MapPreview src={undefined} alt="Map" />);
  expect(screen.getByRole('img', { name: 'Map (no preview)' })).toBeTruthy();
});
