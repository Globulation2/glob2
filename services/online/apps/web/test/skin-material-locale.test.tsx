// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { registerCatalog, setLocale } from '../../../packages/i18n/src/index.ts';
import { LocaleProvider } from '../src/i18n.tsx';
import { MaterialSwatches } from '../src/skins/StudioControls.tsx';

// This checks selection IDs; browser coverage exercises the actual swatch shader.
vi.mock('../src/skins/materialShader.ts', () => ({ SKIN_MATERIAL_GLSL: '' }));
afterEach(async () => {
  cleanup();
  vi.restoreAllMocks();
  await setLocale('en');
});

it('selects the canonical material ID through its translated label', async () => {
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
  registerCatalog('ms', { Metal: 'Logam', 'Riveted metal': 'Logam berpaku' });
  await setLocale('ms');
  const onSelect = vi.fn();
  const { rerender } = render(
    <LocaleProvider>
      <MaterialSwatches color="#ffffff" selected={0} onSelect={onSelect} />
    </LocaleProvider>,
  );
  expect(screen.getByRole('heading', { name: 'Logam' })).toBeTruthy();
  fireEvent.click(screen.getByRole('radio', { name: 'Logam berpaku' }));
  expect(onSelect).toHaveBeenCalledExactlyOnceWith(2);
  rerender(
    <LocaleProvider>
      <MaterialSwatches color="#ffffff" selected={2} onSelect={onSelect} />
    </LocaleProvider>,
  );
  expect(screen.getByRole('radio', { name: 'Logam berpaku' }).getAttribute('aria-checked')).toBe(
    'true',
  );
});
