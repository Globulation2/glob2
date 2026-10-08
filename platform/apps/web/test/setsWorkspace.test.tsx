// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import type { SetDraft, SetPackage } from '@glob2/protocol';
import { SetWorkspace } from '../src/sets/Workspace.tsx';
import { SetDetail, SetLibrary } from '../src/sets/Library.tsx';
import { RouterProvider, Link, useRouter } from '../src/router.tsx';

const mock = vi.hoisted(() => ({ request: vi.fn() }));
vi.mock('../src/api.ts', () => ({ request: mock.request }));
vi.mock('../src/state.tsx', async () => {
  const actual = await vi.importActual('../src/state.tsx');
  return { ...actual, useSession: () => ({ account: { id: 'author', displayName: 'Author' } }) };
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
    <RouterProvider>
      <Routes />
    </RouterProvider>,
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

it('does not reuse a remaining terrain key after deletion and addition', async () => {
  open();
  const add = await screen.findByRole('button', { name: 'Add terrain' });
  fireEvent.click(add);
  fireEvent.click(add);
  fireEvent.click(screen.getAllByRole('button', { name: 'Custom Grass' })[0]!);
  fireEvent.click(screen.getByRole('button', { name: 'Remove entry' }));
  fireEvent.click(add);
  fireEvent.click(screen.getByRole('button', { name: 'Save draft' }));
  await waitFor(() => expect(draft?.package.terrains).toHaveLength(2));
  expect(new Set(draft!.package.terrains.map((entry) => entry['key'])).size).toBe(2);
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
  const add = await screen.findByRole('button', { name: 'Add terrain' });
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
  fireEvent.click(screen.getByRole('button', { name: 'Add terrain' }));
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
    terrains: [{ key: 'test:moss', name: 'Moss', base: 'grass', properties: { walkable: false } }],
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
  expect(screen.getByText('walkable')).toBeTruthy();
  expect(screen.getByText('false')).toBeTruthy();
  expect(screen.getByText(/"frame": 3/)).toBeTruthy();
});
