// @vitest-environment jsdom
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, expect, it } from 'vitest';
import { ICON_NAMES, Icon } from '../src/icons.tsx';

afterEach(cleanup);

const SOURCE = join(import.meta.dirname, '../../../../datasrc/icons/tabler');

it('uses only icons pinned by the shared Tabler manifest', () => {
  const manifest = JSON.parse(readFileSync(join(SOURCE, 'manifest.json'), 'utf8')) as {
    icons: { name: string; sha256: string }[];
  };
  const pinned = new Map(manifest.icons.map((icon) => [icon.name, icon.sha256]));
  for (const name of ICON_NAMES) {
    const bytes = readFileSync(join(SOURCE, `${name}.svg`));
    expect(createHash('sha256').update(bytes).digest('hex'), name).toBe(pinned.get(name));
  }
});

it('draws decorative icons in the text colour', () => {
  const { container } = render(<Icon name="trophy" size={20} />);
  const svg = container.querySelector('svg')!;
  expect(svg.getAttribute('aria-hidden')).toBe('true');
  expect(svg.getAttribute('stroke')).toBe('currentColor');
  expect(svg.getAttribute('width')).toBe('20');
  expect(svg.querySelectorAll('path').length).toBeGreaterThan(0);
});

it('names labelled icons for assistive technology', () => {
  render(<Icon name="robot" label="AI player" />);
  expect(screen.getByRole('img', { name: 'AI player' })).toBeTruthy();
});
