// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { setLocale, t } from '../../../packages/i18n/src/index.ts';
import { LocaleProvider, RichMessage } from '../src/i18n.tsx';
import { measureName } from '../src/admin/presentation.ts';
import { MapUpload } from '../src/pages/Maps.tsx';
import { RouterProvider } from '../src/router.tsx';
import { MaterialSwatches } from '../src/skins/StudioControls.tsx';
import { ACTIONS, ACTION_LABELS } from '../src/skins/geometry.ts';
import { MusicValidation } from '../src/music/Validation.tsx';

vi.mock('../src/state.tsx', () => ({
  useSession: () => ({ account: { id: 'owner' } }),
}));
vi.mock('../src/skins/materialShader.ts', () => ({ SKIN_MATERIAL_GLSL: '' }));

// Load the shipped Telugu catalog, rather than replacing it with a test fixture.
beforeEach(async () => {
  await setLocale('te');
});
afterEach(async () => {
  cleanup();
  vi.restoreAllMocks();
  await setLocale('en');
});

it('composes report metrics with category and product names, not numeric placeholders', () => {
  expect(measureName('library.published', 'maps')).toBe('పటాలు: ప్రచురణలు');
  expect(measureName('library.downloads', 'maps')).toBe('పటాలు: నమోదైన డౌన్‌లోడ్‌లు');
  expect(measureName('duration.maps', 'seconds')).toBe(
    'మ్యాప్ స్టూడియో: పూర్తి చేయడానికి పట్టిన సమయం (సెకన్లు)',
  );
  expect(measureName('duration.maps', 'samples')).toBe('మ్యాప్ స్టూడియో: పూర్తయిన పనుల సమయ కొలతలు');
  expect(measureName('future.metric', 'future.dimension')).toBe('future.metric future.dimension');
});

it('keeps unit animation identifiers canonical while translating their actions', () => {
  expect(ACTIONS.explorer).toEqual(['fly']);
  expect(ACTIONS.worker).toEqual(['walk', 'swim', 'harvest']);
  expect(t(ACTION_LABELS.fly ?? '')).toBe('ఎగరడం');
  expect(t(ACTION_LABELS.harvest ?? '')).toBe('సేకరణ');
});

it('selects the canonical scales material through its actual Telugu display name', () => {
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
  const onSelect = vi.fn();
  render(
    <LocaleProvider>
      <MaterialSwatches color="#ffffff" selected={0} onSelect={onSelect} />
    </LocaleProvider>,
  );
  fireEvent.click(screen.getByRole('radio', { name: 'పొలుసులు' }));
  expect(onSelect).toHaveBeenCalledExactlyOnceWith(9);
});

it('uses the soundtrack mood guidance from the actual catalog', () => {
  render(
    <LocaleProvider>
      <MusicValidation
        checks={[
          {
            id: 'contrast',
            label: 'contrast',
            status: 'warn',
            attempt: 1,
            measures: [],
          },
        ]}
      />
    </LocaleProvider>,
  );
  expect(
    screen.getAllByText(
      'నిర్మాణం, పోరాటం సంగీతాలు ప్రశాంతం సంగీతానికి భిన్నంగా అనిపిస్తున్నాయో తనిఖీ చేయండి.',
    ).length,
  ).toBeGreaterThan(0);
});

it('changes map visibility through translated options without translating policy IDs', () => {
  render(
    <LocaleProvider>
      <RouterProvider>
        <MapUpload />
      </RouterProvider>
    </LocaleProvider>,
  );
  const visibility = screen.getByLabelText('దృశ్యమానత') as HTMLSelectElement;
  expect([...visibility.options].map((option) => [option.value, option.textContent])).toEqual([
    ['public', 'బహిరంగం: కేటలాగ్‌లో కనిపిస్తుంది'],
    ['unlisted', 'జాబితాలో లేదు: లింక్ ఉన్నవారికి అందుబాటు'],
    ['private', 'ప్రైవేట్: మీకు మాత్రమే'],
  ]);
  expect(visibility.value).toBe('unlisted');
  fireEvent.change(visibility, { target: { value: 'private' } });
  expect(visibility.value).toBe('private');
});

it('composes the application button with a translated model without a fixed dative suffix', () => {
  render(
    <LocaleProvider>
      <RichMessage source="Apply to {slot0}" slots={{ slot0: t('Worker') }} />
    </LocaleProvider>,
  );
  expect(screen.getByText('వర్తింపజేయండి: కార్మికుడు')).toBeTruthy();
});
