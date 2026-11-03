// @vitest-environment jsdom
import { afterEach, expect, it } from 'vitest';
import { cleanup, render, screen } from '@testing-library/react';
import { MusicValidation, latestMusicChecks } from '../src/music/Validation.tsx';
import type { MusicStudioCheck } from '@glob2/protocol';
const check = (
  label: string,
  status: MusicStudioCheck['status'],
  attempt = 1,
): MusicStudioCheck => ({ id: `${attempt}:${label}`, label, status, attempt, measures: [] });
afterEach(cleanup);
it('summarizes only the latest candidate and preserves non-pass statuses', () => {
  const checks = [
    check('seam', 'fail'),
    check('seam', 'pass', 2),
    check('format', 'waived'),
    check('balance', 'skip'),
    check('noise', 'pending'),
  ];
  expect(latestMusicChecks(checks).find((c) => c.label === 'seam')?.status).toBe('pass');
  const view = render(<MusicValidation checks={checks} latestAttempts />);
  expect(screen.getByText('1 passed · 0 findings · 1 Waived · 1 Skipped · 1 Pending')).toBeTruthy();
  expect(view.container.querySelectorAll('.music-check')).toHaveLength(4);
  expect(view.container.querySelectorAll('.music-check[data-status="fail"]')).toHaveLength(0);
  expect(view.container.querySelector('.music-technical')?.hasAttribute('open')).toBe(false);
});
it('shows human-readable warnings, deduplicates notes, and retains unknown categories', () => {
  render(
    <MusicValidation
      checks={[
        { ...check('seam', 'warn'), detail: 'Listen to the wrap' },
        check('new category', 'fail'),
      ]}
      warnings={['Listen to the wrap', 'Other note', 'Other note']}
    />,
  );
  expect(screen.getAllByText('Listen to the wrap')).toHaveLength(2);
  expect(screen.getAllByText('Other note')).toHaveLength(1);
  expect(screen.getAllByText('Loop continuity')).toHaveLength(2);
  expect(screen.getAllByText('new category')).toHaveLength(2);
});
it('does not claim that invalid or absent checks passed', () => {
  render(<MusicValidation checks={[{ label: 'format', status: 'pass' }]} />);
  expect(screen.getByText('No measured results are available for this version.')).toBeTruthy();
  expect(screen.getByText('Some results could not be displayed.')).toBeTruthy();
  expect(screen.queryByText('Playback format')).toBeNull();
});
