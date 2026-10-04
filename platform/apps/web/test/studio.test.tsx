// @vitest-environment jsdom
import { beforeEach, afterEach, it, expect, vi } from 'vitest';
import type { ReactNode } from 'react';
import { render, screen, fireEvent, waitFor, cleanup } from '@testing-library/react';
import { MapStudio } from '../src/pages/MapStudio.tsx';
vi.mock('../src/state.tsx', () => {
  const account = { id: 'owner', kind: 'registered' };
  return { useSession: () => ({ account }) };
});
vi.mock('../src/router.tsx', () => ({
  useRouter: () => ({ navigate: vi.fn() }),
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
let writes: { path: string; body: unknown }[];
beforeEach(() => {
  writes = [];
  sessionStorage.clear();
  vi.stubGlobal(
    'fetch',
    vi.fn(async (path: string, init: RequestInit) => {
      if (init.method === 'POST') {
        writes.push({ path, body: JSON.parse(String(init.body)) as unknown });
        return new Response(JSON.stringify({ id: 'request' }), { status: 200 });
      }
      if (path.endsWith('/updates'))
        return new Promise<Response>((_resolve, reject) =>
          init.signal?.addEventListener('abort', () => reject(new Error('aborted'))),
        );
      const data = path.endsWith('/account')
        ? { enabled: true, available: 3, reserved: 0, packs: [], usage: [] }
        : path.endsWith('/threads')
          ? { items: [{ id, title: 'River' }] }
          : {
              id,
              title: 'River',
              messages: [{ id: 'm', role: 'user', text: 'Wider river' }],
              requests: [version],
            };
      return new Response(JSON.stringify(data), { status: 200 });
    }),
  );
});
afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});
it('shows private versions and explicit paid generation separately from included discussion', async () => {
  render(<MapStudio id={id} />);
  await screen.findByRole('button', { name: 'Generate — 1 credit' });
  expect(screen.getByRole('button', { name: 'Publish this version' })).toBeTruthy();
  expect(screen.getByRole('button', { name: 'Host room' })).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Revise this version' }));
  fireEvent.click(screen.getByRole('button', { name: 'Generate — 1 credit' }));
  await waitFor(() => expect(writes).toHaveLength(1));
  expect(writes[0]!.body).toMatchObject({
    settings: { width: 256, height: 128, players: 4 },
    parent: 'version',
  });
});
it('keeps unsent feedback out of generation and stores a recoverable draft', async () => {
  render(<MapStudio id={id} />);
  await screen.findByRole('button', { name: 'Generate — 1 credit' });
  fireEvent.change(screen.getByRole('textbox', { name: 'Describe your map or discuss changes' }), {
    target: { value: 'More bridges' },
  });
  expect(
    (screen.getByRole('button', { name: 'Generate — 1 credit' }) as HTMLButtonElement).disabled,
  ).toBe(true);
  expect(sessionStorage.getItem('studio-draft:owner:thread')).toBe('More bridges');
  fireEvent.click(screen.getByRole('button', { name: 'Send message' }));
  await waitFor(() => expect(writes).toHaveLength(1));
  expect(writes[0]!.path).toContain('/messages');
  expect(writes[0]!.body).toMatchObject({ text: 'More bridges' });
});
it('preserves a draft and revision target while switching workspace views', async () => {
  render(<MapStudio id={id} />);
  await screen.findByRole('button', { name: 'Revise this version' });
  fireEvent.click(screen.getByRole('button', { name: 'Revise this version' }));
  const composer = screen.getByRole('textbox', { name: 'Describe your map or discuss changes' });
  fireEvent.change(composer, { target: { value: 'Keep this unsent revision' } });
  fireEvent.click(screen.getByRole('button', { name: 'Versions (1)' }));
  fireEvent.click(screen.getByRole('button', { name: 'Conversation' }));
  expect(screen.getByRole('textbox', { name: 'Describe your map or discuss changes' })).toBe(
    composer,
  );
  expect((composer as HTMLTextAreaElement).value).toBe('Keep this unsent revision');
  expect(screen.getByText(/Revising version 1/)).toBeTruthy();
  expect(sessionStorage.getItem('studio-draft:owner:thread')).toBe('Keep this unsent revision');
});
