// @vitest-environment jsdom
import { beforeEach, afterEach, it, expect, vi } from 'vitest';
import type { ReactNode } from 'react';
import { render, screen, fireEvent, waitFor, cleanup } from '@testing-library/react';
import { STAGES } from '../src/pages/studio/types.ts';
import { MapStudio } from '../src/pages/MapStudio.tsx';
vi.mock('../src/state.tsx', () => {
  const account = { id: 'owner', kind: 'registered' };
  return { useSession: () => ({ account }) };
});
const navigate = vi.hoisted(() => vi.fn());
vi.mock('../src/router.tsx', () => ({
  useRouter: () => ({ navigate }),
  Link: ({ children, to }: { children: ReactNode; to: string }) => <a href={to}>{children}</a>,
}));
const id = 'thread',
  version = {
    id: 'version',
    thread_id: id,
    kind: 'generate',
    status: 'ready',
    input: {
      brief: 'River',
      messages: [],
      settings: { width: 256, height: 128, players: 4 },
      pipelineVersion: 'v1',
    },
    map_id: 'map',
    map_hash: 'a'.repeat(64),
    charged: true,
  };
let available = 3;
let failures = 0;
let rejectedStatus = 0;
let failedSnapshotReads = 0;
let activeRequest: { id: string; threadId: string; status: string } | undefined;
let generationStatus = 'ready';
let writes: { path: string; body: unknown }[];
beforeEach(() => {
  writes = [];
  available = 3;
  failures = 0;
  rejectedStatus = 0;
  failedSnapshotReads = 0;
  activeRequest = undefined;
  generationStatus = 'ready';
  navigate.mockClear();
  sessionStorage.clear();
  window.history.replaceState({}, '', '/');
  vi.stubGlobal(
    'fetch',
    vi.fn(async (path: string, init: RequestInit) => {
      if (init.method === 'POST') {
        writes.push({ path, body: JSON.parse(String(init.body)) as unknown });
        if (failures-- > 0) throw new Error('Network response lost');
        if (rejectedStatus)
          return new Response(
            JSON.stringify({ code: 'bad_request', message: 'Invalid request body.' }),
            { status: rejectedStatus },
          );
        return new Response(JSON.stringify({ id: 'request' }), { status: 200 });
      }
      if (path.endsWith(`/threads/${id}`) && failedSnapshotReads-- > 0)
        return new Response(
          JSON.stringify({ code: 'unavailable', message: 'The request could not be completed.' }),
          { status: 503 },
        );
      if (path.endsWith('/updates'))
        return new Promise<Response>((_resolve, reject) =>
          init.signal?.addEventListener('abort', () => reject(new Error('aborted'))),
        );
      if (path.endsWith('/progress'))
        return new Response(
          JSON.stringify({
            requestId: 'version',
            historical: false,
            stages: STAGES.map((s) => ({
              ...s,
              status:
                generationStatus === 'processing' && s.id === 'checks'
                  ? 'running'
                  : generationStatus === 'processing' && s.id === 'ready'
                    ? 'pending'
                    : 'complete',
            })),
            artifacts: [
              {
                id: 'reference',
                requestId: 'version',
                stage: 'prepare',
                kind: 'reference',
                label: 'Source reference',
                url: '/reference.png',
              },
              {
                id: 'native',
                requestId: 'version',
                stage: 'build',
                kind: 'preview',
                label: 'Native terrain',
                url: '/native.png',
              },
            ],
            checks: [
              {
                id: 'routes',
                label: 'Connected routes',
                status: generationStatus === 'processing' ? 'running' : 'passed',
                detail: 'Walking routes connect the colonies.',
                colony: 0,
                location: { x: 10, y: 20 },
              },
            ],
          }),
          { status: 200 },
        );
      const data = path.endsWith('/account')
        ? {
            enabled: true,
            available,
            reserved: activeRequest ? 1 : 0,
            packs: [],
            usage: [],
            activeRequest,
          }
        : path.endsWith('/threads')
          ? { items: [{ id, title: 'River' }] }
          : {
              id,
              title: 'River',
              messages: [{ id: 'm', role: 'user', text: 'Wider river' }],
              requests: [{ ...version, status: generationStatus }],
            };
      return new Response(JSON.stringify(data), { status: 200 });
    }),
  );
});

it('releases a definitively rejected submission so the draft can be corrected', async () => {
  render(<MapStudio id={id} />);
  const composer = await screen.findByRole('textbox', {
    name: 'Describe your map or discuss changes',
  });
  fireEvent.change(composer, { target: { value: 'Original request' } });
  rejectedStatus = 400;
  fireEvent.click(screen.getByRole('button', { name: 'Send' }));
  await screen.findByText('Invalid request body.');
  expect(screen.queryByRole('button', { name: 'Retry the same request' })).toBeNull();
  expect(sessionStorage.getItem('studio-pending:owner:thread')).toBeNull();
  expect(sessionStorage.getItem('studio-draft:owner:thread')).toBe('Original request');
  rejectedStatus = 0;
  fireEvent.change(composer, { target: { value: 'Corrected request' } });
  fireEvent.click(screen.getByRole('button', { name: 'Send' }));
  await waitFor(() => expect(writes).toHaveLength(2));
  expect(writes[1]?.body).toMatchObject({ text: 'Corrected request' });
  expect((writes[1]?.body as { id: string }).id).not.toBe((writes[0]?.body as { id: string }).id);
});

it('does not offer to resubmit an accepted message when refreshing history fails', async () => {
  render(<MapStudio id={id} />);
  const composer = await screen.findByRole('textbox', {
    name: 'Describe your map or discuss changes',
  });
  await screen.findByRole('button', { name: 'Edit this version' });
  fireEvent.change(composer, { target: { value: 'Accepted message' } });
  failedSnapshotReads = 1;
  fireEvent.click(screen.getByRole('button', { name: 'Send' }));
  await screen.findByText('The request could not be completed.');
  expect(writes).toHaveLength(1);
  expect(screen.queryByRole('button', { name: 'Retry the same request' })).toBeNull();
  expect(sessionStorage.getItem('studio-pending:owner:thread')).toBeNull();
  expect((composer as HTMLTextAreaElement).value).toBe('');
});

it('lets a rejected first project use corrected prompt and settings', async () => {
  render(<MapStudio />);
  const composer = await screen.findByRole('textbox', {
    name: 'Describe your map or discuss changes',
  });
  await screen.findByRole('button', { name: /3 Map credits/ });
  fireEvent.change(composer, { target: { value: 'Original landscape' } });
  rejectedStatus = 400;
  fireEvent.click(screen.getByRole('button', { name: 'Send' }));
  await screen.findByText('Invalid request body.');
  expect(sessionStorage.getItem('studio-created:owner')).toBeNull();
  rejectedStatus = 0;
  fireEvent.change(composer, { target: { value: 'Corrected landscape' } });
  fireEvent.click(screen.getByRole('button', { name: 'Send' }));
  await waitFor(() => expect(navigate).toHaveBeenCalled());
  expect(writes[1]?.body).toMatchObject({ title: 'Corrected landscape' });
});
afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});
it('shows private versions and submits edits through a single server-directed turn', async () => {
  render(<MapStudio id={id} />);
  await screen.findByRole('button', { name: 'Play' });
  fireEvent.click(screen.getByText('More', { selector: 'summary' }));
  expect(screen.getByRole('button', { name: 'Publish this version' })).toBeTruthy();
  expect(screen.getByRole('button', { name: 'Play' })).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Edit this version' }));
  fireEvent.change(screen.getByRole('textbox', { name: 'Describe your map or discuss changes' }), {
    target: { value: 'Build the map with more bridges' },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Send' }));
  await waitFor(() => expect(writes).toHaveLength(1));
  expect(writes[0]?.body).toMatchObject({
    settings: { width: 256, height: 128, players: 4 },
    parent: 'version',
  });
});
it('submits feedback once with its snapshotted build context and stores a recoverable draft', async () => {
  render(<MapStudio id={id} />);
  await screen.findByRole('button', { name: 'Play' });
  fireEvent.change(screen.getByRole('textbox', { name: 'Describe your map or discuss changes' }), {
    target: { value: 'More bridges' },
  });
  expect(
    screen.getByRole('button', { name: 'Send' }).getAttribute('aria-disabled') === 'true',
  ).toBe(false);
  expect(sessionStorage.getItem('studio-draft:owner:thread')).toBe('More bridges');
  fireEvent.click(screen.getByRole('button', { name: 'Send' }));
  await waitFor(() => expect(writes).toHaveLength(1));
  expect(writes[0]?.path).toContain('/turns');
  expect(writes[0]?.body).toMatchObject({ text: 'More bridges' });
});
it('preserves a draft and revision target while switching workspace views', async () => {
  render(<MapStudio id={id} />);
  await screen.findByRole('button', { name: 'Edit this version' });
  fireEvent.click(screen.getByRole('button', { name: 'Edit this version' }));
  const composer = screen.getByRole('textbox', { name: 'Describe your map or discuss changes' });
  fireEvent.change(composer, { target: { value: 'Keep this unsent revision' } });
  // Inspection and resizing preserve the draft and explicit edit target.
  fireEvent.keyDown(screen.getByRole('separator'), { key: 'ArrowRight' });
  expect(screen.getByRole('textbox', { name: 'Describe your map or discuss changes' })).toBe(
    composer,
  );
  expect((composer as HTMLTextAreaElement).value).toBe('Keep this unsent revision');
  expect(screen.getAllByText(/Editing version 1/)).toBeTruthy();
  expect(sessionStorage.getItem('studio-draft:owner:thread')).toBe('Keep this unsent revision');
});

it('pins stage inspection without losing the selected generation', async () => {
  render(<MapStudio id={id} />);
  fireEvent.click(await screen.findByText('Build details', { selector: 'summary' }));
  const stage = await screen.findByRole('button', { name: 'Prepare the design' });
  await waitFor(() => expect((stage as HTMLButtonElement).disabled).toBe(false));
  fireEvent.click(stage);
  expect(screen.getByText('Source reference', { selector: 'figcaption span' })).toBeTruthy();
  expect(screen.getByRole('button', { name: /Follow latest/ })).toBeTruthy();
  expect((screen.getByLabelText('Inspect version') as HTMLSelectElement).value).toBe('version');
  fireEvent.click(screen.getByRole('button', { name: /Follow latest/ }));
  expect(screen.getByRole('button', { name: 'Edit this version' })).toBeTruthy();
});
it('keeps the native image visible during checks and when inspecting a check', async () => {
  generationStatus = 'processing';
  available = 0;
  activeRequest = { id: 'version', threadId: id, status: 'processing' };
  render(<MapStudio id={id} />);
  await screen.findByText('Native terrain', { selector: 'figcaption span' });
  fireEvent.click(screen.getByText('Build details', { selector: 'summary' }));
  fireEvent.click(screen.getByRole('button', { name: /Connected routes/ }));
  expect(screen.getByText('Native terrain', { selector: 'figcaption span' })).toBeTruthy();
  expect(screen.getByText(/Colony 1/)).toBeTruthy();
  expect(screen.queryByText(/An available credit is needed/)).toBeNull();
});
it('persists revision settings through a reload and resets the parent when size changes', async () => {
  const view = render(<MapStudio id={id} />);
  fireEvent.click(await screen.findByRole('button', { name: 'Edit this version' }));
  view.unmount();
  render(<MapStudio id={id} />);
  await screen.findAllByText(/Editing version 1/);
  fireEvent.click(screen.getByText(/256 × 128 · 4 players/, { selector: 'summary' }));
  expect((screen.getByLabelText('Height') as HTMLSelectElement).value).toBe('128');
  fireEvent.change(screen.getByLabelText('Players'), { target: { value: '6' } });
  expect(screen.queryByText(/Editing version 1/)).toBeNull();
});
it('keeps saved maps accessible without credits and opens credits without losing the prompt', async () => {
  available = 0;
  const view = render(<MapStudio />);
  await screen.findByRole('textbox', { name: 'Describe your map or discuss changes' });
  fireEvent.change(screen.getByRole('textbox', { name: 'Describe your map or discuss changes' }), {
    target: { value: 'Saved before purchase' },
  });
  expect(sessionStorage.getItem('studio-draft:owner:new')).toBe('Saved before purchase');
  view.unmount();
  render(<MapStudio id={id} />);
  await screen.findByRole('button', { name: 'Play' });
  expect(
    screen.getByRole('button', { name: 'Send' }).getAttribute('aria-disabled') === 'true',
  ).toBe(true);
  HTMLDialogElement.prototype.showModal = function () {
    this.open = true;
  };
  const prompt = screen.getByRole('textbox', { name: 'Describe your map or discuss changes' });
  fireEvent.change(prompt, { target: { value: 'Make the map greener.' } });
  fireEvent.keyDown(prompt, { key: 'Enter' });
  expect(screen.getByRole('dialog', { name: 'Map credits' })).toBeTruthy();
  expect((prompt as HTMLTextAreaElement).value).toBe('Make the map greener.');
  expect(writes).toEqual([]);
});
it('recovers an active last-credit project on the entry page', async () => {
  available = 0;
  activeRequest = { id: 'version', threadId: id, status: 'processing' };
  render(<MapStudio />);
  await waitFor(() =>
    expect(navigate).toHaveBeenCalledWith('/map-studio/thread', { replace: true }),
  );
  expect(screen.queryByText('battlefield.')).toBeNull();
});
it('reuses the persisted creation and message IDs after a lost creation response', async () => {
  failures = 1;
  render(<MapStudio />);
  await waitFor(() => expect(screen.getByRole('button', { name: /3 Map credits/ })).toBeTruthy());
  const input = screen.getByRole('textbox', { name: 'Describe your map or discuss changes' });
  fireEvent.change(input, { target: { value: 'Original island idea' } });
  fireEvent.click(screen.getByRole('button', { name: 'Send' }));
  await screen.findByRole('alert');
  const stored = JSON.parse(sessionStorage.getItem('studio-created:owner') ?? '{}') as {
    id: string;
    messageId: string;
  };
  fireEvent.change(input, { target: { value: 'An edited draft' } });
  fireEvent.click(screen.getByRole('button', { name: 'Send' }));
  await waitFor(() => expect(writes).toHaveLength(2));
  expect(writes[0]?.body).toEqual(writes[1]?.body);
  expect(writes[1]?.body).toMatchObject({ id: stored.id, title: 'Original island idea' });
  const pending = JSON.parse(
    sessionStorage.getItem(`studio-pending:owner:${stored.id}`) ?? '{}',
  ) as { body: { id: string; text: string } };
  expect(pending.body).toMatchObject({ id: stored.messageId, text: 'Original island idea' });
  expect(sessionStorage.getItem('studio-draft:owner:new')).toBe('An edited draft');
});
it('blocks replacement generation after an unknown submission outcome and retries the same ID', async () => {
  failures = 1;
  render(<MapStudio id={id} />);
  await screen.findByRole('button', { name: 'Edit this version' });
  fireEvent.change(screen.getByRole('textbox', { name: 'Describe your map or discuss changes' }), {
    target: { value: 'Build the map with more bridges' },
  });
  fireEvent.click(screen.getByRole('button', { name: 'Send' }));
  const retry = await screen.findByRole('button', { name: 'Retry the same request' });
  expect(
    screen.getByRole('button', { name: 'Send' }).getAttribute('aria-disabled') === 'true',
  ).toBe(true);
  fireEvent.click(retry);
  await waitFor(() => expect(writes).toHaveLength(2));
  expect(writes[0]?.body).toEqual(writes[1]?.body);
});

it.each(['returned', 'cancelled'])(
  'keeps the wallet authoritative and draft intact after checkout %s',
  async (payment) => {
    available = 0;
    sessionStorage.setItem('studio-draft:owner:thread', 'Keep these islands');
    window.history.replaceState({}, '', `/map-studio/thread?payment=${payment}`);
    render(<MapStudio id={id} />);
    await screen.findByRole('button', { name: /0 Map credits/ });
    expect(
      (
        screen.getByRole('textbox', {
          name: 'Describe your map or discuss changes',
        }) as HTMLTextAreaElement
      ).value,
    ).toBe('Keep these islands');
    expect(
      screen.getByRole('button', { name: 'Send' }).getAttribute('aria-disabled') === 'true',
    ).toBe(true);
    if (payment === 'returned') expect(screen.getByText(/Confirming your payment/)).toBeTruthy();
    else expect(screen.getByText(/Checkout was cancelled/)).toBeTruthy();
    expect(screen.queryByText(/Your credits are ready/)).toBeNull();
  },
);
