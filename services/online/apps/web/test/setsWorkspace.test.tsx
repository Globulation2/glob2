// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { act, cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import type { SetDraft, SetPackage } from '@glob2/protocol';
import { MATERIALS, newPackage } from '@glob2/protocol';
import { getLocale, registerCatalog, setLocale } from '../../../packages/i18n/src/index.ts';
import bg from '../../../packages/i18n/locales/bg.json';
import { LocaleProvider } from '../src/i18n.tsx';
import { SetWorkspace } from '../src/sets/Workspace.tsx';
import { SetDetail, SetLibrary } from '../src/sets/Library.tsx';
import { RouterProvider, Link, useRouter } from '../src/router.tsx';

const mock = vi.hoisted(() => ({ request: vi.fn() }));
vi.mock('../src/api.ts', () => ({ request: mock.request }));
vi.mock('../src/state.tsx', async () => {
  const actual = await vi.importActual('../src/state.tsx');
  const account = { id: 'author', displayName: 'Author' };
  return { ...actual, useSession: () => ({ account }) };
});
const ID = '11111111-1111-4111-8111-111111111111';
let draft: SetDraft | null;
let validate: (() => Promise<SetDraft>) | undefined;
function Routes() {
  const { location } = useRouter();
  if (location.path === '/sets/new') return <SetWorkspace />;
  if (location.path.startsWith('/sets/drafts/')) return <SetWorkspace key={ID} id={ID} />;
  return <Link to="/sets/new">Create set</Link>;
}
function open() {
  window.history.replaceState(null, '', '/sets/new');
  return render(
    <LocaleProvider>
      <RouterProvider>
        <Routes />
      </RouterProvider>
    </LocaleProvider>,
  );
}
beforeEach(() => {
  HTMLDialogElement.prototype.showModal = function () {
    this.open = true;
  };
  HTMLDialogElement.prototype.close = function () {
    this.open = false;
  };
  draft = null;
  validate = undefined;
  vi.spyOn(window, 'scrollTo').mockImplementation(() => undefined);
  mock.request.mockReset();
  mock.request.mockImplementation(
    async (method: string, path: string, options?: { body?: unknown }) => {
      if (method === 'POST' && path === '/api/v1/set-drafts') {
        draft = {
          id: ID,
          revision: 1,
          package: structuredClone(options?.body as SetPackage),
          validation: null,
          publishedVersionId: null,
        } as SetDraft;
        return structuredClone(draft);
      }
      if (method === 'PUT') {
        const body = options?.body as { package: SetPackage };
        draft = {
          ...draft!,
          package: structuredClone(body.package),
          revision: draft!.revision + 1,
        };
        return structuredClone(draft);
      }
      if (path.endsWith('/validate') && validate) return validate();
      if (method === 'GET' && path === '/api/v1/set-drafts/' + ID) return structuredClone(draft);
      throw Error('Unexpected request: ' + method + ' ' + path);
    },
  );
});
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

it('adds terrain and resource entries to their canonical package collections', async () => {
  open();
  fireEvent.click(await screen.findByRole('button', { name: 'Add Terrain' }));
  const terrainName = screen.getByLabelText('Name', { exact: true }) as HTMLInputElement;
  expect(terrainName.value).toBe('Terrain 1');
  fireEvent.change(terrainName, { target: { value: 'Authored terrain' } });
  fireEvent.click(screen.getByRole('button', { name: 'Resources' }));
  fireEvent.click(screen.getByRole('button', { name: 'Add Resource' }));
  const resourceName = screen.getByLabelText('Name', { exact: true }) as HTMLInputElement;
  expect(resourceName.value).toBe('Resource 1');
  fireEvent.change(resourceName, { target: { value: 'Authored resource' } });
  fireEvent.click(screen.getByRole('button', { name: 'Save draft' }));
  await waitFor(() => expect(draft?.package.terrains).toHaveLength(1));
  expect(draft?.package.resources).toHaveLength(1);
  expect(draft?.package.terrains[0]?.['base']).toBe('grass');
  expect(draft?.package.terrains[0]?.['name']).toBe('Authored terrain');
  expect(draft?.package.resources[0]?.['presentation']).toMatchObject({
    name: 'Authored resource',
  });
});

it('does not reuse a remaining terrain key after deletion and addition', async () => {
  open();
  const add = await screen.findByRole('button', { name: 'Add Terrain' });
  fireEvent.click(add);
  fireEvent.click(add);
  fireEvent.click(screen.getAllByRole('button', { name: 'Terrain 1' })[0]!);
  fireEvent.click(screen.getByRole('button', { name: 'Remove entry' }));
  fireEvent.click(add);
  fireEvent.click(screen.getByRole('button', { name: 'Save draft' }));
  await waitFor(() => expect(draft?.package.terrains).toHaveLength(2));
  expect(new Set(draft!.package.terrains.map((entry) => entry['key'])).size).toBe(2);
  expect(new Set(draft!.package.terrains.map((entry) => entry['name'])).size).toBe(2);
});
it('keeps a typed comma while entering multiple tags', async () => {
  open();
  const tags = await screen.findByLabelText('Tags');
  fireEvent.change(tags, { target: { value: 'forest,' } });
  expect((tags as HTMLInputElement).value).toBe('forest,');
  fireEvent.change(tags, { target: { value: 'forest, moss' } });
  fireEvent.click(screen.getByRole('button', { name: 'Save draft' }));
  await waitFor(() => expect(draft?.package.tags).toEqual(['forest', 'moss']));
});
it('retains invalid JSON across entry switches and prevents saving hidden invalid text', async () => {
  open();
  const add = await screen.findByRole('button', { name: 'Add Terrain' });
  fireEvent.click(add);
  fireEvent.change(screen.getByLabelText('Name', { exact: true }), {
    target: { value: 'First terrain' },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Save draft' }));
  await waitFor(() => expect(window.location.pathname).toBe('/sets/drafts/' + ID));
  await screen.findByDisplayValue('First terrain');
  const details = screen.getByText('Resource placement permissions').closest('details')!;
  details.open = true;
  const json = within(details).getByRole('textbox');
  fireEvent.change(json, { target: { value: '[unfinished' } });
  expect(screen.getByText(/Unsaved changes/)).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Add Terrain' }));
  fireEvent.click(screen.getByRole('button', { name: 'Save draft' }));
  await screen.findByText('Correct the invalid JSON before saving.');
  fireEvent.click(screen.getByRole('button', { name: 'First terrain' }));
  expect(
    (
      within(screen.getByText('Resource placement permissions').closest('details')!).getByRole(
        'textbox',
        { hidden: true },
      ) as HTMLTextAreaElement
    ).value,
  ).toBe('[unfinished');
});
it('waits for new-draft validation before replacing the workspace route', async () => {
  let finish: (() => void) | undefined;
  validate = async () => {
    await new Promise<void>((resolve) => {
      finish = resolve;
    });
    draft = {
      ...draft!,
      validation: {
        status: 'valid',
        hash: 'a'.repeat(64),
        simVersion: 'test',
        suite: 1,
        report: null,
        error: null,
      },
    };
    return structuredClone(draft);
  };
  open();
  await screen.findByLabelText('Title', { exact: true });
  fireEvent.click(screen.getByRole('button', { name: 'Run checks & preview' }));
  await waitFor(() => expect(finish).toBeTypeOf('function'));
  expect(window.location.pathname).toBe('/sets/new');
  finish!();
  await screen.findByText('Checks passed');
  await waitFor(() => expect(window.location.pathname).toBe('/sets/drafts/' + ID));
  fireEvent.click(screen.getByRole('button', { name: 'Review & publish' }));
  await waitFor(() =>
    expect(
      (screen.getByRole('button', { name: 'Publish this release' }) as HTMLButtonElement).disabled,
    ).toBe(false),
  );
});
it('appends draft pages and keeps older unpublished drafts discoverable', async () => {
  mock.request.mockImplementation(
    async (_method: string, path: string, options?: { query?: { cursor?: string } }) => {
      if (path === '/api/v1/sets') return { items: [] };
      if (path === '/api/v1/set-drafts')
        return options?.query?.cursor
          ? { items: [{ id: 'older', title: 'Older draft', publishedVersionId: null }] }
          : {
              items: [{ id: 'newer', title: 'Recent draft', publishedVersionId: null }],
              nextCursor: 'page-two',
            };
      throw Error('Unexpected path: ' + path);
    },
  );
  render(
    <RouterProvider>
      <SetLibrary mine />
    </RouterProvider>,
  );
  await screen.findByRole('link', { name: 'Recent draft · Continue editing' });
  fireEvent.click(screen.getByRole('button', { name: 'Load more drafts' }));
  await screen.findByRole('link', { name: 'Older draft · Continue editing' });
  expect(screen.getByRole('link', { name: 'Recent draft · Continue editing' })).toBeTruthy();
  expect(screen.queryByRole('button', { name: 'Load more drafts' })).toBeNull();
});

it('shows read-only entry properties and custom terrain frame mappings', async () => {
  const pack = {
    terrains: [
      {
        key: 'test:moss',
        name: 'Moss',
        base: 'grass',
        properties: {
          walkable: false,
          groundSpeedQ8: 256,
          unknownVelocityQ8: 384,
          constructor: 'authored constructor',
        },
      },
    ],
    resources: [],
    assets: { terrains: { 'test:moss': { variants: [{ frame: 3, weight: 1 }] } } },
  };
  mock.request.mockImplementation(async (_method: string, path: string) => {
    if (path.endsWith('/file')) return pack;
    return {
      id: ID,
      title: 'Moss set',
      description: '',
      owner: { id: 'other', displayName: 'Artist' },
      versions: [{ id: ID, label: '1.0', credits: [], license: 'CC0-1.0' }],
      likes: 0,
    };
  });
  render(
    <RouterProvider>
      <SetDetail id={ID} />
    </RouterProvider>,
  );
  fireEvent.click(await screen.findByRole('button', { name: 'Inspect latest release' }));
  await screen.findByText('Terrain · Moss');
  for (const key of ['walkable', 'groundSpeedQ8', 'unknownVelocityQ8', 'constructor'])
    expect(screen.getByText(key).tagName).toBe('CODE');
  expect(screen.getByText('256')).toBeTruthy();
  expect(screen.getByText('384')).toBeTruthy();
  expect(screen.getByText('authored constructor')).toBeTruthy();
  expect(screen.queryByText('ground Speed Q8')).toBeNull();
  expect(screen.getByText('false')).toBeTruthy();
  expect(screen.getByText(/"frame": 3/)).toBeTruthy();
});

it('uses audited Bulgarian resource captions while retaining material and schema identifiers', async () => {
  const previous = getLocale();
  registerCatalog('bg', bg);
  const materials = [...MATERIALS];
  try {
    await setLocale('bg');
    open();
    fireEvent.click(await screen.findByRole('button', { name: bg.Resources }));
    fireEvent.click(
      screen.getByRole('button', {
        name: bg['Add {slot0}'].replace('{slot0}', bg.Resource),
      }),
    );
    const material = screen.getByRole('textbox', { name: bg['Main material'] });
    expect((material as HTMLInputElement).value).toBe('wood');
    const growth = screen.getAllByRole('spinbutton', { name: new RegExp('^' + bg['Growth rate']) });
    expect(growth).toHaveLength(2); // resource properties and enabled wood yield
    expect(
      screen.getByRole('spinbutton', { name: new RegExp('^' + bg['Spread rate']) }),
    ).toBeTruthy();
    expect(screen.getByRole('checkbox', { name: bg['Can be farmed'] })).toBeTruthy();
    for (const key of [
      'ecology',
      'stockBranchDivisor',
      'capacity',
      'consumption',
      'animationFrames',
      'animationTicks',
      'animationStride',
      'minimap',
    ]) {
      expect(screen.getByText(key, { exact: true }).tagName).toBe('CODE');
    }
    expect(
      screen.getByRole('checkbox', { name: 'wood' }).parentElement?.querySelector('code')
        ?.textContent,
    ).toBe('wood');
    expect(screen.queryByText('Primary Material')).toBeNull();
    expect(screen.queryByText('Growth Rate')).toBeNull();
    fireEvent.change(material, { target: { value: 'stone' } });
    fireEvent.change(growth[0]!, { target: { value: '98304' } });
    await act(async () => setLocale('en'));
    expect(screen.getByRole('textbox', { name: 'Main material' })).toBeTruthy();
    await act(async () => setLocale('bg'));
    fireEvent.click(screen.getByRole('button', { name: bg['Save draft'] }));
    await waitFor(() => expect(draft?.package.resources).toHaveLength(1));
    expect(draft!.package.resources[0]?.['properties']).toMatchObject({
      primaryMaterial: 'stone',
      growthRate: 98304,
    });
    expect(draft!.package.resources[0]?.['yields']).toMatchObject({
      wood: { growthRate: 196608, consumption: 'all' },
    });
    expect(MATERIALS).toEqual(materials);
  } finally {
    cleanup();
    await setLocale(previous);
  }
});

it('translates safe terrain captions and override options while custom fields remain exact code', async () => {
  const previous = getLocale();
  registerCatalog('bg', bg);
  const key = 'fixture:terrain';
  const basePack = newPackage('Author');
  draft = {
    id: ID,
    revision: 1,
    validation: null,
    publishedVersionId: null,
    package: {
      ...basePack,
      assets: { ...basePack.assets, terrains: { [key]: { sprite: 'data/gfx/terrain-grass' } } },
      terrains: [
        {
          key,
          name: 'Authored terrain',
          base: 'grass',
          properties: {
            resourcesGrow: true,
            buildable: true,
            groundSpeedQ8: 256,
            walkable: true,
            customGrowthRate: 33,
            toString: 'custom identifier value',
            constructor: 'custom constructor value',
            ['__proto__']: 'custom prototype value',
          },
        },
      ],
    },
  } as SetDraft;
  try {
    await setLocale('bg');
    window.history.replaceState(null, '', '/sets/drafts/' + ID);
    render(
      <LocaleProvider>
        <RouterProvider>
          <Routes />
        </RouterProvider>
      </LocaleProvider>,
    );
    await screen.findByRole('checkbox', { name: bg['Resource growth enabled'] });
    expect(screen.getByRole('checkbox', { name: bg['Buildings allowed'] })).toBeTruthy();
    expect(
      screen.getByRole('spinbutton', { name: new RegExp('^' + bg['Ground movement']) }),
    ).toBeTruthy();
    for (const field of [
      'walkable',
      'customGrowthRate',
      'toString',
      'constructor',
      '__proto__',
      'animation_frames',
      'animation_ticks',
      'animation_stride',
    ]) {
      expect(screen.getByText(field, { exact: true }).tagName).toBe('CODE');
    }
    const overrides = screen.getByRole('combobox', {
      name: bg['Add a property override'],
    });
    for (const [wire, caption] of [
      ['groundHealthQ8', 'Ground health change'],
      ['airHealthQ8', 'Air health change'],
      ['growthQ8', 'Resource growth'],
    ] as const) {
      expect(
        within(overrides).getByRole('option', { name: bg[caption] }).getAttribute('value'),
      ).toBe(wire);
    }
    expect(within(overrides).getByRole('option', { name: 'swimmable' }).getAttribute('value')).toBe(
      'swimmable',
    );
    fireEvent.change(overrides, { target: { value: 'groundHealthQ8' } });
    const health = screen.getByRole('spinbutton', {
      name: new RegExp('^' + bg['Ground health change']),
    });
    fireEvent.change(health, { target: { value: '-512' } });
    fireEvent.click(screen.getByRole('button', { name: bg['Save draft'] }));
    await waitFor(() => expect(draft?.revision).toBe(2));
    expect(draft!.package.terrains[0]?.['properties']).toMatchObject({
      groundHealthQ8: -512,
      walkable: true,
      customGrowthRate: 33,
      toString: 'custom identifier value',
      constructor: 'custom constructor value',
      ['__proto__']: 'custom prototype value',
    });
    expect(draft!.package.terrains[0]?.['name']).toBe('Authored terrain');
  } finally {
    cleanup();
    await setLocale(previous);
  }
});
