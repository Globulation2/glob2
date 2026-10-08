// @vitest-environment jsdom
import { afterEach, describe, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, renderHook, screen } from '@testing-library/react';
import { Operations } from '../src/admin/Operations.tsx';
import { useFilterDraft } from '../src/admin/filters.tsx';
import {
  csvText,
  formatCash,
  reportingPeriods,
  completionStats,
} from '../src/admin/presentation.ts';
import { LineChart } from '../src/components/LineChart.tsx';
import { RouterProvider } from '../src/router.tsx';
import { request } from '../src/api.ts';

vi.mock('../src/api.ts', () => ({ request: vi.fn() }));
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.mocked(request).mockReset();
});

describe('admin report presentation', () => {
  it('compares completion times by weighted samples and leaves absent measurements unavailable', () => {
    expect(
      completionStats(
        [
          { day: '2026-10-01', metric: 'duration.maps', dimension: 'seconds', value: 10 },
          { day: '2026-10-01', metric: 'duration.maps', dimension: 'samples', value: 1 },
          { day: '2026-10-02', metric: 'duration.maps', dimension: 'seconds', value: 180 },
          { day: '2026-10-02', metric: 'duration.maps', dimension: 'samples', value: 9 },
        ],
        'duration.maps',
      ),
    ).toEqual({ mean: 19, samples: 10 });
    expect(completionStats([], 'duration.maps')).toEqual({ mean: null, samples: 0 });
  });

  it('preserves signed numeric credits while escaping text formulas and CSV structure', () => {
    expect(csvText([['=1+1', ' \t=1+1', -42, 'a,"b"\nc']])).toBe(
      '"\'=1+1","\' \t=1+1","-42","a,""b""\nc"',
    );
  });
  it('formats Stripe charge units, including currency exceptions', () => {
    expect(formatCash(12345, 'usd')).toContain('123.45');
    expect(formatCash(500, 'jpy')).toContain('500');
    expect(formatCash(500, 'mga')).toContain('500');
    expect(formatCash(500, 'isk')).toContain('5.00');
    expect(formatCash(500, 'ugx')).toContain('5.00');
    expect(formatCash(1045, 'huf')).toContain('10.45');
    expect(formatCash(80045, 'twd')).toContain('800.45');
  });
  it('uses UTC reporting days across year boundaries', () => {
    expect(reportingPeriods('2026-01-02T23:59:00Z', 7)).toEqual({
      today: '2026-01-02',
      currentStart: '2025-12-27',
      previousStart: '2025-12-20',
      previousEnd: '2025-12-26',
    });
  });
  it('updates filter drafts when browser navigation changes the source', () => {
    const hook = renderHook(({ q }) => useFilterDraft(q), { initialProps: { q: 'old' } });
    hook.rerender({ q: 'back-link' });
    expect(hook.result.current[0]).toBe('back-link');
  });
  it('shows a single observed day as a mark and splits missing-day gaps', () => {
    const result = render(
      <LineChart
        title="Daily accounts"
        xDomain={[0.3, 10.3]}
        maxGap={1}
        dots
        xLabel="UTC day"
        series={[
          {
            name: 'Registered',
            color: 'green',
            points: [
              { x: 2, y: 3 },
              { x: 3, y: 4 },
              { x: 9, y: 2 },
            ],
          },
        ]}
      />,
    );
    expect(result.container.querySelectorAll('circle')).toHaveLength(3);
    expect(result.container.querySelectorAll('polyline')).toHaveLength(2);
    expect(
      [...result.container.querySelectorAll('svg > .axis > text')].map(
        (label) => label.textContent,
      ),
    ).toEqual(expect.arrayContaining(['0.3', '10.3']));
    expect(screen.getByRole('columnheader', { name: 'UTC day' })).toBeTruthy();
    result.rerender(
      <LineChart
        title="Daily accounts"
        xDomain={[0.3, 10.3]}
        dots
        series={[{ name: 'Registered', color: 'green', points: [{ x: 2, y: 3 }] }]}
      />,
    );
    expect(result.container.querySelectorAll('circle')).toHaveLength(1);
  });
});

it('requires inspected evidence and explicit verified usage before reconciliation', async () => {
  const now = '2026-10-08T12:00:00Z';
  const detail = {
    id: 'call-1',
    product: 'hive',
    status: 'uncertain',
    createdAt: now,
    completedAt: null,
    reserved: 12,
    charged: null,
    usage: null,
    creditConsequence: 'The remainder is released.',
    attempts: [],
  };
  vi.mocked(request).mockImplementation(async (method, path) => {
    if (method === 'POST') return { charged: 0 } as never;
    return (
      path === '/api/v1/admin/operations'
        ? {
            items: [
              {
                id: 'call-1',
                product: 'hive',
                accountId: null,
                status: 'uncertain',
                createdAt: now,
                reserved: 12,
                kind: 'usage',
                error: null,
              },
            ],
            agents: [],
            workers: [],
            reservedCredits: [],
            queueAgeSeconds: null,
          }
        : detail
    ) as never;
  });
  vi.spyOn(window, 'confirm').mockReturnValue(true);
  render(
    <RouterProvider>
      <Operations />
    </RouterProvider>,
  );
  const submit = await screen.findByRole('button', { name: 'Reconcile verified usage' });
  fireEvent.change(screen.getByLabelText('Reason and evidence'), {
    target: { value: 'Provider confirmed usage.' },
  });
  expect((submit as HTMLButtonElement).disabled).toBe(true);
  fireEvent.click(screen.getByRole('button', { name: 'Inspect recovery details' }));
  await screen.findByText('No attempt usage evidence is available.');
  for (const label of ['Input tokens (including cached)', 'Cached input tokens', 'Output tokens'])
    fireEvent.change(screen.getByLabelText(label), { target: { value: '0' } });
  expect((submit as HTMLButtonElement).disabled).toBe(true);
  fireEvent.click(screen.getByRole('checkbox'));
  expect((submit as HTMLButtonElement).disabled).toBe(false);
  fireEvent.click(submit);
  expect(window.confirm).toHaveBeenCalledWith(
    expect.stringContaining('0 input (0 cached; 0 cache write) and 0 output'),
  );
  expect(request).toHaveBeenCalledWith('POST', '/api/v1/admin/hive/calls/call-1/reconcile', {
    body: { usage: { input: 0, cachedInput: 0, output: 0 }, evidence: 'Provider confirmed usage.' },
  });
});

it('copies verified cache-write usage and prevents cache counts exceeding total input', async () => {
  const now = '2026-10-08T12:00:00Z';
  const detail = {
    id: 'call-2',
    product: 'hive',
    status: 'uncertain',
    createdAt: now,
    completedAt: null,
    reserved: 12,
    charged: null,
    usage: { input: 5, cached: 2, cacheWrite: 1, output: 1 },
    creditConsequence: 'The remainder is released.',
    attempts: [],
  };
  vi.mocked(request).mockImplementation(async (method, path) => {
    if (method === 'POST') return { charged: 1 } as never;
    return (
      path === '/api/v1/admin/operations'
        ? {
            items: [
              {
                id: 'call-2',
                product: 'hive',
                accountId: null,
                status: 'uncertain',
                createdAt: now,
                reserved: 12,
                kind: 'usage',
                error: null,
              },
            ],
            agents: [],
            workers: [],
            reservedCredits: [],
            queueAgeSeconds: null,
          }
        : detail
    ) as never;
  });
  vi.spyOn(window, 'confirm').mockReturnValue(true);
  render(
    <RouterProvider>
      <Operations />
    </RouterProvider>,
  );
  const submit = await screen.findByRole('button', { name: 'Reconcile verified usage' });
  fireEvent.click(screen.getByRole('button', { name: 'Inspect recovery details' }));
  fireEvent.click(await screen.findByRole('button', { name: 'Copy recorded usage' }));
  fireEvent.change(screen.getByLabelText('Reason and evidence'), {
    target: { value: 'Checked all provider cache usage.' },
  });
  expect((submit as HTMLButtonElement).disabled).toBe(true);
  fireEvent.click(screen.getByRole('checkbox'));
  expect((submit as HTMLButtonElement).disabled).toBe(false);
  fireEvent.change(screen.getByLabelText('Cached input tokens'), { target: { value: '5' } });
  expect((screen.getByRole('checkbox') as HTMLInputElement).checked).toBe(false);
  fireEvent.click(screen.getByRole('checkbox'));
  expect((submit as HTMLButtonElement).disabled).toBe(true);
  fireEvent.change(screen.getByLabelText('Cached input tokens'), { target: { value: '2' } });
  fireEvent.click(screen.getByRole('checkbox'));
  fireEvent.click(submit);
  expect(request).toHaveBeenCalledWith('POST', '/api/v1/admin/hive/calls/call-2/reconcile', {
    body: {
      usage: { input: 5, cachedInput: 2, cacheWrite: 1, output: 1 },
      evidence: 'Checked all provider cache usage.',
    },
  });
});
