// @vitest-environment jsdom
import { act, cleanup, render, screen } from '@testing-library/react';
import { afterEach, expect, it } from 'vitest';
import { LibraryResults } from '../src/components/library.tsx';
import type { Load } from '../src/state.tsx';

afterEach(cleanup);

function Gallery({ load }: { load: Load<string[]> }) {
  return (
    <section className="library-page">
      <input type="search" aria-label="Search" />
      <LibraryResults load={load} retry={() => undefined} count={(items) => items.length}>
        {(items) => (
          <>
            {items.map((item) => (
              <a key={item} className="library-card" href={'/' + item}>
                {item}
              </a>
            ))}
            <button>Show more</button>
            {!items.length && <button>Clear filters</button>}
          </>
        )}
      </LibraryResults>
    </section>
  );
}

it('announces completion outside the busy gallery and restores pagination to the first new card', () => {
  const { rerender } = render(<Gallery load={{ status: 'ready', data: ['old'] }} />);
  act(() => screen.getByRole('button', { name: 'Show more' }).focus());
  rerender(<Gallery load={{ status: 'loading' }} />);
  expect(screen.getByRole('status').closest('[aria-busy]')).toBeNull();
  rerender(<Gallery load={{ status: 'ready', data: ['old', 'new'] }} />);
  expect(document.activeElement).toBe(screen.getByRole('link', { name: 'new' }));
  expect(screen.getByRole('status').textContent).toBe('Results: 2');
});

it('returns clearing to search and preserves focus when automatic results update', () => {
  const { rerender } = render(<Gallery load={{ status: 'ready', data: [] }} />);
  act(() => screen.getByRole('button', { name: 'Clear filters' }).focus());
  rerender(<Gallery load={{ status: 'loading' }} />);
  expect(document.activeElement).toBe(screen.getByRole('searchbox'));
  rerender(<Gallery load={{ status: 'ready', data: ['new'] }} />);
  expect(document.activeElement).toBe(screen.getByRole('searchbox'));
});

it('does not restore gallery focus after a deliberate move away during loading', () => {
  const { rerender } = render(<Gallery load={{ status: 'ready', data: ['old'] }} />);
  act(() => screen.getByRole('button', { name: 'Show more' }).focus());
  rerender(<Gallery load={{ status: 'loading' }} />);
  act(() => screen.getByRole('searchbox').focus());
  rerender(<Gallery load={{ status: 'ready', data: ['new'] }} />);
  expect(document.activeElement).toBe(screen.getByRole('searchbox'));
});

it('restores a useful target when retry replaces an error without an intermediate loading state', () => {
  const { rerender } = render(
    <Gallery load={{ status: 'error', error: new Error('Unavailable') }} />,
  );
  act(() => screen.getByRole('button', { name: 'Try again' }).focus());
  rerender(<Gallery load={{ status: 'ready', data: ['new'] }} />);
  expect(document.activeElement).toBe(screen.getByRole('link', { name: 'new' }));
});

it('focuses the result region when a new page is empty', () => {
  const { rerender, container } = render(<Gallery load={{ status: 'ready', data: ['old'] }} />);
  act(() => screen.getByRole('button', { name: 'Show more' }).focus());
  rerender(<Gallery load={{ status: 'loading' }} />);
  rerender(<Gallery load={{ status: 'ready', data: [] }} />);
  expect(document.activeElement).toBe(container.querySelector('.library-results'));
  expect(screen.getByRole('status').textContent).toBe('Results: 0');
});
