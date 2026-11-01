// @vitest-environment jsdom
import { afterEach, expect, it, vi } from 'vitest';
import { act, cleanup, render, screen } from '@testing-library/react';
import type { ReactNode } from 'react';
import { getLocale, registerCatalog, setLocale } from '../../../packages/i18n/src/index.ts';
import bg from '../../../packages/i18n/locales/bg.json';
import { LocaleProvider } from '../src/i18n.tsx';
import { TerrainStudio } from '../src/pages/TerrainStudio.tsx';
import { AiBuildingStudio } from '../src/pages/AiBuildingStudio.tsx';
const fixture = vi.hoisted(() => ({ revisions: [] as object[] }));
vi.mock('../src/state.tsx', () => {
  const account = { id: 'owner' };
  return { useSession: () => ({ account }) };
});
vi.mock('../src/router.tsx', () => ({
  useRouter: () => ({ navigate: vi.fn(), location: { search: new URLSearchParams() } }),
  Link: ({ children, to }: { children: ReactNode; to: string }) => <a href={to}>{children}</a>,
}));
vi.mock('../src/pages/BuildingStudio.tsx', () => ({ BuildingEditor: () => null }));
vi.mock('../src/pages/building-ai/Preview.tsx', () => ({ BuildingPreview: () => null }));
vi.mock('../src/sets/Workspace.tsx', () => ({ SetEditor: () => null }));
vi.mock('../src/sets/Preview.tsx', () => ({ SetPreview: () => null }));
const pack: {
  title: string;
  terrains: Record<string, unknown>[];
  resources: Record<string, unknown>[];
} = { title: 'Authored package', terrains: [], resources: [] };
vi.mock('../src/api.ts', async (importOriginal) => ({
  ...(await importOriginal<object>()),
  request: vi.fn(async (_method: string, path: string) => {
    if (path.endsWith('/account')) return { enabled: true, available: 0, reserved: 0, packs: [] };
    if (path.endsWith('/progress')) return { stages: [], checks: [], notes: [], artifacts: [] };
    if (path.endsWith('/file')) return pack;
    if (path.includes('/set-drafts/')) return { id: 'draft', revision: 1, package: pack };
    return {
      title: 'Authored project',
      draftId: 'draft',
      messages: [],
      requests: [],
      revisions: fixture.revisions,
      references: [],
    };
  }),
}));
vi.mock('../src/pages/building-ai/useStudioSnapshot.ts', () => ({
  BUILDING_STUDIO_ROOT: '/api/v1/ai-building-studio',
  useStudioSnapshot: () => ({
    wallet: { enabled: true, available: 0, reserved: 0, packs: [] },
    thread: {
      title: 'Authored project',
      messages: [],
      requests: [],
      revisions: fixture.revisions,
      references: [],
    },
    draft: { id: 'draft', revision: 'revision', package: {} },
    projects: [],
    refresh: vi.fn(),
  }),
}));
afterEach(() => {
  cleanup();
  pack.terrains = [];
  pack.resources = [];
  sessionStorage.clear();
  vi.restoreAllMocks();
});
it.each([
  ['terrain', TerrainStudio],
  ['building', AiBuildingStudio],
] as const)(
  'localizes %s generated draft labels while preserving authored revision names',
  async (kind, Workspace) => {
    const previous = getLocale();
    registerCatalog('bg', bg);
    fixture.revisions = [];
    try {
      await setLocale('bg');
      const view = render(
        <LocaleProvider>
          <Workspace id="project" />
        </LocaleProvider>,
      );
      const viewing = bg['Viewing {slot0}'].replace('{slot0}', bg['Current saved draft']);
      const editing = bg['Editing {slot0}'].replace('{slot0}', bg['Current saved draft']);
      await screen.findByText(viewing);
      expect(screen.getByText(editing)).toBeTruthy();
      await act(async () => setLocale('en'));
      expect(screen.getByText('Viewing Current saved draft')).toBeTruthy();
      expect(screen.getByText('Editing Current saved draft')).toBeTruthy();
      view.unmount();
      fixture.revisions = [
        {
          requestId: 'authored',
          title: 'My authored revision',
          report: { valid: true },
          baseRevision: 1,
        },
      ];
      sessionStorage.setItem(
        `${kind === 'terrain' ? 'terrain' : 'building'}-view:owner:project`,
        JSON.stringify('authored'),
      );
      await setLocale('bg');
      render(
        <LocaleProvider>
          <Workspace id="project" />
        </LocaleProvider>,
      );
      await screen.findByText(bg['Viewing {slot0}'].replace('{slot0}', 'My authored revision'));
      expect(screen.getByText(editing)).toBeTruthy();
    } finally {
      cleanup();
      await setLocale(previous);
    }
  },
);

it('keeps read-only terrain diagnostics and material IDs literal while known captions localize', async () => {
  const previous = getLocale();
  registerCatalog('bg', bg);
  fixture.revisions = [];
  pack.terrains = [
    {
      key: 'fixture:terrain',
      name: 'Authored terrain',
      properties: {
        groundSpeedQ8: 256,
        unknownHealthQ8: -512,
        constructor: 'authored constructor',
        toString: 'authored method-name field',
      },
    },
  ];
  pack.resources = [
    {
      key: 'fixture:resource',
      presentation: { name: 'Authored resource' },
      properties: { growthRate: 196608 },
      yields: { wood: { initial: 1, capacity: 5, consumption: 'infinite' } },
    },
  ];
  const original = structuredClone(pack);
  try {
    await setLocale('bg');
    render(
      <LocaleProvider>
        <TerrainStudio id="project" />
      </LocaleProvider>,
    );
    await screen.findByText(bg['Ground movement']);
    expect(screen.getByText(bg['Growth rate'])).toBeTruthy();
    for (const key of ['unknownHealthQ8', 'constructor', 'toString', 'wood'])
      expect(screen.getByText(key, { exact: true }).tagName).toBe('CODE');
    expect(screen.queryByText('Unknown Health')).toBeNull();
    expect(screen.queryByText('Wood', { exact: true })).toBeNull();
    await act(async () => setLocale('en'));
    expect(screen.getByText('Ground movement')).toBeTruthy();
    expect(screen.getByText('unknownHealthQ8').tagName).toBe('CODE');
    expect(pack).toEqual(original);
  } finally {
    cleanup();
    await setLocale(previous);
  }
});
