// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import type {
  AdminCrashGroupList,
  AdminCrashReleaseList,
  AdminCrashReportDetail,
  AdminCrashReportList,
} from '@glob2/protocol';
import { Crashes } from '../src/admin/Crashes.tsx';
import { RouterProvider } from '../src/router.tsx';
import { api } from '../src/api.ts';

vi.mock('../src/api.ts', () => ({
  api: {
    adminCrashGroups: vi.fn(),
    adminCrashReports: vi.fn(),
    adminCrashReport: vi.fn(),
    adminCrashReleases: vi.fn(),
    setCrashGroupState: vi.fn(),
  },
}));
afterEach(() => {
  cleanup();
  vi.mocked(api.adminCrashGroups).mockReset();
  vi.mocked(api.adminCrashReports).mockReset();
  vi.mocked(api.adminCrashReport).mockReset();
  vi.mocked(api.adminCrashReleases).mockReset();
  vi.mocked(api.setCrashGroupState).mockReset();
  window.history.replaceState(null, '', '/');
});

const GROUP = '5f0c1a2b-3c4d-4e5f-8a6b-7c8d9e0f1a2b';
const report = {
  id: 'K7M2QX9D',
  kind: 'crash',
  status: 'done',
  groupId: GROUP,
  reason: 'SIGSEGV /SEGV_MAPERR',
  receivedAt: '2026-11-26T12:00:00Z',
  version: '0.11.8.1',
  versionMinor: 219,
  simRevision: 170,
  distribution: 'steam',
  platform: 'windows',
  os: 'Windows 11',
  deviceClass: 'desktop',
  deviceTier: 'high',
  description: '',
  installId: '3d2c1b0a-9f8e-4d7c-8b6a-5f4e3d2c1b0a',
  size: 20480,
  attachments: [{ name: 'minidump.dmp', size: 18000 }],
  bundleAvailable: true,
} satisfies AdminCrashReportList['items'][number];

it('lists crash groups by installs, filters them and opens a group with its stack', async () => {
  vi.mocked(api.adminCrashGroups).mockResolvedValue({
    items: [
      {
        id: GROUP,
        signature: 'Unit::step(int) | Team::update()',
        firstVersion: '0.11.7',
        lastVersion: '0.11.8.1',
        firstSeenAt: '2026-11-20T12:00:00Z',
        lastSeenAt: '2026-11-26T12:00:00Z',
        reports: 12,
        installs: 9,
        platforms: ['windows'],
        distributions: ['steam'],
        state: 'new',
      },
    ],
  } satisfies AdminCrashGroupList);
  vi.mocked(api.adminCrashReports).mockResolvedValue({
    items: [report],
  } satisfies AdminCrashReportList);
  vi.mocked(api.adminCrashReport).mockResolvedValue({
    report,
    frames: [
      {
        module: 'glob2.exe',
        offset: '0x429e',
        function: 'Unit::step(int)',
        file: 'C:/w/glob2/src/unit/Unit.cpp',
        line: 412,
        inGame: true,
      },
      { module: 'ntdll.dll', offset: '0x11', inGame: false },
    ],
  } satisfies AdminCrashReportDetail);
  render(
    <RouterProvider>
      <Crashes />
    </RouterProvider>,
  );
  expect(await screen.findByText('Unit::step(int) | Team::update()')).toBeTruthy();
  expect(screen.getByText('0.11.7 – 0.11.8.1')).toBeTruthy();
  expect(screen.getByText('windows · steam')).toBeTruthy();

  fireEvent.change(screen.getByLabelText('Channel'), { target: { value: 'steam' } });
  fireEvent.click(screen.getByRole('button', { name: 'Filter' }));
  await vi.waitFor(() =>
    expect(api.adminCrashGroups).toHaveBeenLastCalledWith(
      { distribution: 'steam' },
      expect.anything(),
    ),
  );

  fireEvent.click(await screen.findByRole('button', { name: 'Show reports' }));
  expect(await screen.findByText('SIGSEGV /SEGV_MAPERR')).toBeTruthy();
  expect(api.adminCrashReports).toHaveBeenCalledWith({ group: GROUP }, expect.anything());
  // The newest report's stack is open; library frames show their module and offset.
  expect(await screen.findByText('Unit::step(int)')).toBeTruthy();
  expect(screen.getByText('unit/Unit.cpp:412')).toBeTruthy();
  expect(screen.getByText('ntdll.dll+0x11')).toBeTruthy();
});

it('resolves a group with the release that fixed it', async () => {
  vi.mocked(api.adminCrashGroups).mockResolvedValue({
    items: [
      {
        id: GROUP,
        signature: 'Unit::step(int)',
        firstVersion: '0.11.8.1',
        lastVersion: '0.11.8.1',
        firstSeenAt: '2026-11-20T12:00:00Z',
        lastSeenAt: '2026-11-26T12:00:00Z',
        reports: 3,
        installs: 2,
        platforms: ['android'],
        distributions: ['google_play'],
        state: 'new',
        fixedIn: '0.11.8.0',
        githubIssue: { number: 42, url: 'https://github.com/Globulation2/glob2/issues/42' },
      },
    ],
  } satisfies AdminCrashGroupList);
  vi.mocked(api.setCrashGroupState).mockResolvedValue({
    id: GROUP,
    state: 'resolved',
    fixedIn: '0.11.8.2',
  });
  render(
    <RouterProvider>
      <Crashes />
    </RouterProvider>,
  );
  expect(await screen.findByText(/Reopened after the fix in 0\.11\.8\.0/)).toBeTruthy();
  expect(screen.getByRole('link', { name: '#42' }).getAttribute('href')).toBe(
    'https://github.com/Globulation2/glob2/issues/42',
  );
  const card = screen.getByRole('article');
  const select = card.querySelector('select') as HTMLSelectElement;
  fireEvent.change(select, { target: { value: 'resolved' } });
  const save = screen.getByRole('button', { name: 'Save' }) as HTMLButtonElement;
  fireEvent.change(screen.getByLabelText('Fixed in'), { target: { value: '' } });
  expect(save.disabled).toBe(true);
  fireEvent.change(screen.getByLabelText('Fixed in'), { target: { value: ' 0.11.8.2 ' } });
  fireEvent.click(save);
  await vi.waitFor(() =>
    expect(api.setCrashGroupState).toHaveBeenCalledWith(GROUP, {
      state: 'resolved',
      fixedIn: '0.11.8.2',
    }),
  );
  expect(await screen.findByText('0.11.8.2')).toBeTruthy();
});

it('shows crash-free rates per release and the groups new in it', async () => {
  window.history.replaceState(null, '', '/?view=releases');
  vi.mocked(api.adminCrashReleases).mockResolvedValue({
    items: [
      {
        version: '0.11.8.4',
        registeredAt: '2026-11-25T12:00:00Z',
        overall: {
          sessions: 2000,
          installs: 400,
          crashedSessions: 10,
          crashedInstalls: 8,
          crashFreeSessions: 0.995,
          crashFreeInstalls: 0.98,
        },
        breakdown: [
          {
            platform: 'android',
            distribution: 'google_play',
            stats: {
              sessions: 2000,
              installs: 400,
              crashedSessions: 10,
              crashedInstalls: 8,
              crashFreeSessions: 0.995,
              crashFreeInstalls: 0.98,
            },
          },
        ],
        reports: 14,
        affectedInstalls: 11,
        newGroups: [{ id: GROUP, signature: 'Map::load()', state: 'new', reports: 4, installs: 3 }],
      },
    ],
  } satisfies AdminCrashReleaseList);
  render(
    <RouterProvider>
      <Crashes />
    </RouterProvider>,
  );
  expect(await screen.findByText('0.11.8.4')).toBeTruthy();
  expect(screen.getAllByText('99.50%').length).toBe(2);
  expect(screen.getAllByText('98.00%').length).toBe(2);
  expect(screen.getByText('Map::load()')).toBeTruthy();
  expect(api.adminCrashGroups).not.toHaveBeenCalled();
});
