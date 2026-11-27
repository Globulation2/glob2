// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { cleanup, render, screen } from '@testing-library/react';
import type { AdminCrashReport, AdminCrashReportList } from '@glob2/protocol';
import { ProblemReports } from '../src/admin/ProblemReports.tsx';
import { RouterProvider } from '../src/router.tsx';
import { api } from '../src/api.ts';

vi.mock('../src/api.ts', () => ({ api: { adminCrashReports: vi.fn() } }));
afterEach(() => {
  cleanup();
  vi.mocked(api.adminCrashReports).mockReset();
});

const report: AdminCrashReport = {
  id: 'K7M2QX9D',
  kind: 'problem',
  status: 'none',
  receivedAt: '2026-11-26T12:00:00Z',
  version: '0.11.8.1',
  versionMinor: 219,
  simRevision: 169,
  distribution: 'app_store',
  platform: 'ios',
  os: 'iOS',
  deviceClass: 'phone',
  deviceTier: 'high',
  description: 'The game closed after I won.',
  installId: '3d2c1b0a-9f8e-4d7c-8b6a-5f4e3d2c1b0a',
  size: 151049,
  attachments: [
    { name: 'log.txt', size: 815 },
    { name: 'save.game.gz', size: 65504 },
  ],
  bundleAvailable: true,
};

it('lists problem reports with their context and download links', async () => {
  vi.mocked(api.adminCrashReports).mockResolvedValue({
    items: [
      report,
      { ...report, id: 'AY7J8QQY', description: '', attachments: [], bundleAvailable: false },
    ],
  } satisfies AdminCrashReportList);
  render(
    <RouterProvider>
      <ProblemReports />
    </RouterProvider>,
  );
  expect(await screen.findByText('K7M2QX9D')).toBeTruthy();
  expect(screen.getByText('The game closed after I won.')).toBeTruthy();
  expect(screen.getAllByText('ios · app_store · iOS')).toHaveLength(2);
  expect(screen.getByRole('link', { name: 'save.game.gz' }).getAttribute('href')).toBe(
    '/api/v1/admin/crash-reports/K7M2QX9D/attachments/save.game.gz',
  );
  expect(screen.getAllByRole('link', { name: 'Download bundle' })[0]?.getAttribute('href')).toBe(
    '/api/v1/admin/crash-reports/K7M2QX9D/bundle',
  );
  // An expired bundle keeps its summary but offers no download.
  expect(screen.getByText('No description.')).toBeTruthy();
  expect(screen.getByText('Bundle deleted after 90 days.')).toBeTruthy();
  expect(screen.getAllByRole('link', { name: 'Download bundle' })).toHaveLength(1);
});
