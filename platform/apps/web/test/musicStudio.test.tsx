// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { render, screen, fireEvent, cleanup, waitFor } from '@testing-library/react';
import { MusicWorkspace } from '../src/pages/music-studio/Workspace.tsx';
import type { MusicStudioThread } from '@glob2/protocol';
vi.mock('../src/music/Player.tsx', () => ({
  MusicPlayer: ({
    release,
    initialPosition,
    onPosition,
  }: {
    release: { id: string };
    initialPosition: number;
    onPosition: (n: number) => void;
  }) => (
    <div data-testid="music-player" data-position={initialPosition}>
      {release.id}
      <button onClick={() => onPosition(13)}>Test seek</button>
    </div>
  ),
}));
const version = {
  id: 'v1',
  thread_id: 'thread',
  kind: 'generate' as const,
  status: 'ready' as const,
  input: {
    settings: { pipeline: 'acoustic-v1' as const, seed: 4 },
    brief: '',
    messages: [],
    pipelineVersion: 'music-v1',
  },
  release_id: 'music1',
  error: null,
  charged: true,
  created_at: '2026-10-05T00:00:00Z',
};
const thread: MusicStudioThread = {
  id: 'thread',
  title: 'Moss theme',
  messages: [
    { id: 'm1', role: 'user', text: 'Flute and harp', created_at: '2026-10-05T00:00:00Z' },
  ],
  requests: [version],
};
const publish = vi.fn(),
  revise = vi.fn();
const props = {
  id: 'thread',
  thread,
  wallet: { enabled: true, available: 2, reserved: 0, packs: [], usage: [] },
  busy: false,
  draft: '',
  setDraft: vi.fn(),
  send: vi.fn(),
  generate: vi.fn(),
  settings: version.input.settings,
  revise,
  revision: 0,
  loadEarlier: vi.fn(),
  changeSettings: vi.fn(),
  versionAction: publish,
};
beforeEach(() => {
  publish.mockClear();
  revise.mockClear();
  vi.stubGlobal(
    'fetch',
    vi.fn(
      async (path: string) =>
        new Response(
          JSON.stringify(
            path.includes('/progress')
              ? {
                  requestId: 'v1',
                  stages: [],
                  artifacts: [],
                  notes: [{ text: 'Refined the bass', attempt: 2 }],
                  checks: [
                    {
                      id: '2:seam',
                      label: 'seam',
                      attempt: 2,
                      status: 'warn',
                      measures: [
                        {
                          name: 'seam.calm.step',
                          status: 'warn',
                          value: 0.1,
                          threshold: 'under 0.05',
                          detail: 'Listen to the wrap',
                          unit: 'amplitude',
                        },
                      ],
                    },
                  ],
                  historical: false,
                }
              : {
                  id: 'music1',
                  status: 'ready',
                  metadata: { license: 'CC-BY-4.0' },
                  frames: 2880000,
                },
          ),
        ),
    ),
  );
});
afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
});
it('shows measured checks and requires an explicit selected-version publication', async () => {
  render(<MusicWorkspace {...props} />);
  await screen.findByTestId('music-player');
  await screen.findByText('Validation, in detail');
  expect(publish).not.toHaveBeenCalled();
  fireEvent.click(screen.getByText('seam'));
  expect(screen.getByText('Listen to the wrap')).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Publish to music library' }));
  expect(publish).toHaveBeenCalledWith(version, 'CC-BY-4.0');
  fireEvent.click(screen.getByRole('button', { name: 'Revise this version' }));
  expect(revise).toHaveBeenCalledWith(version);
});
it('keeps an active last-credit generation inspectable but disables new paid work', async () => {
  render(
    <MusicWorkspace
      {...props}
      wallet={{ ...props.wallet, available: 0, reserved: 1 }}
      thread={{ ...thread, requests: [{ ...version, status: 'processing', release_id: null }] }}
    />,
  );
  await screen.findByText('Your composer is working…');
  expect(
    (screen.getByRole('button', { name: /Compose soundtrack/ }) as HTMLButtonElement).disabled,
  ).toBe(true);
  await waitFor(() => expect(screen.queryByTestId('music-player')).toBeNull());
});
it('provides keyboard resizing and keeps edits unsent until explicitly submitted', () => {
  render(<MusicWorkspace {...props} draft="Make the calm arrangement more spacious" />);
  const separator = screen.getByRole('separator');
  fireEvent.keyDown(separator, { key: 'ArrowRight' });
  expect(separator.getAttribute('aria-valuenow')).toBe('38');
  expect(
    (screen.getByRole('button', { name: /Compose soundtrack/ }) as HTMLButtonElement).disabled,
  ).toBe(true);
});

it('aligns comparisons only when both duration and musical timeline match', async () => {
  let secondLoads = 0;
  vi.stubGlobal(
    'fetch',
    vi.fn(
      async (path: string) =>
        new Response(
          JSON.stringify(
            path.includes('/progress')
              ? {
                  requestId: 'v2',
                  stages: [],
                  artifacts: [],
                  checks: [],
                  notes: [],
                  historical: false,
                }
              : {
                  id: path.endsWith('music1') ? 'music1' : 'music2',
                  status: 'ready',
                  metadata: { license: 'CC0-1.0' },
                  frames: 2880000,
                  timelineId: path.endsWith('music2') && secondLoads++ > 0 ? 'changed' : 'same',
                },
          ),
        ),
    ),
  );
  render(
    <MusicWorkspace
      {...props}
      thread={{ ...thread, requests: [version, { ...version, id: 'v2', release_id: 'music2' }] }}
    />,
  );
  await screen.findByText('music2');
  fireEvent.click(screen.getByRole('button', { name: 'Test seek' }));
  fireEvent.change(screen.getByLabelText('Compare revision'), { target: { value: 'v1' } });
  await screen.findByText('music1');
  expect(screen.getByTestId('music-player').getAttribute('data-position')).toBe('13');
  fireEvent.change(screen.getByLabelText('Compare revision'), { target: { value: '' } });
  await screen.findByText('music2');
  expect(screen.getByTestId('music-player').getAttribute('data-position')).toBe('0');
  expect(screen.getAllByTestId('music-player')).toHaveLength(1);
});
