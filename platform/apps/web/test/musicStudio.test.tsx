// @vitest-environment jsdom
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { render, screen, fireEvent, cleanup, waitFor } from '@testing-library/react';
import { MusicWorkspace } from '../src/pages/music-studio/Workspace.tsx';
import type { PlaybackSettings, PlaybackSnapshot } from '../src/music/useMusicPlayback.ts';
import type { MusicStudioThread } from '@glob2/protocol';
vi.mock('../src/music/Player.tsx', () => ({
  MusicPlayer: ({
    release,
    initialPosition,
    onPosition,
    onSnapshot,
    initialSettings,
  }: {
    release: { id: string };
    initialPosition: number;
    onPosition: (n: number) => void;
    initialSettings?: PlaybackSettings;
    onSnapshot?: (snapshot: PlaybackSnapshot) => void;
  }) => (
    <div
      data-testid="music-player"
      data-position={initialPosition}
      data-settings={JSON.stringify(initialSettings)}
    >
      {release.id}
      <button onClick={() => onPosition(13)}>Test seek</button>
      <button
        onClick={() =>
          onSnapshot?.({ position: 13, mood: 2, volume: 0.4, muted: true, playing: true })
        }
      >
        Test playback settings
      </button>
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
  await screen.findByText('Audio quality checks');
  expect(publish).not.toHaveBeenCalled();
  fireEvent.click(screen.getByText('Show technical results'));
  fireEvent.click(screen.getAllByText('Loop continuity')[1] as HTMLElement);
  expect(screen.getAllByText('Listen to the wrap').length).toBeGreaterThan(0);
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

function mockRevisionEvidence() {
  vi.stubGlobal(
    'fetch',
    vi.fn(async (path: string) => {
      const first = path.includes('/requests/v1/') || path.endsWith('music1');
      const id = first ? 'v1' : 'v2';
      return new Response(
        JSON.stringify(
          path.includes('/progress')
            ? {
                requestId: id,
                stages: [],
                artifacts: [
                  {
                    id: `${id}:preview`,
                    kind: 'preview',
                    label: `${id} preview`,
                    url: `/${id}.opus`,
                  },
                ],
                checks: [
                  {
                    id: `${id}:seam`,
                    label: `${id} seam`,
                    attempt: 1,
                    status: 'warn',
                    measures: [],
                    detail: `${id} measured evidence`,
                  },
                ],
                notes: [],
                historical: false,
              }
            : {
                id: first ? 'music1' : 'music2',
                status: 'ready',
                metadata: { license: 'CC0-1.0' },
                frames: 2880000,
              },
        ),
      );
    }),
  );
}
const secondVersion = { ...version, id: 'v2', release_id: 'music2' };

it('shows checks and candidate artifacts belonging to the audible comparison revision', async () => {
  mockRevisionEvidence();
  render(<MusicWorkspace {...props} thread={{ ...thread, requests: [version, secondVersion] }} />);
  await screen.findByText('music2');
  await screen.findAllByText('v2 measured evidence');
  fireEvent.change(screen.getByLabelText('Compare revision'), { target: { value: 'v1' } });
  await screen.findByText('music1');
  await screen.findAllByText('v1 measured evidence');
  expect(screen.queryByText('v2 measured evidence')).toBeNull();
  expect(screen.getByRole('heading', { name: 'Version 1 · comparing' })).toBeTruthy();
  expect(screen.getByText('v1 preview')).toBeTruthy();
  expect(screen.queryByText('v2 preview')).toBeNull();
});

it('clears an older candidate preview when following a live revision without delivery', async () => {
  mockRevisionEvidence();
  const view = render(
    <MusicWorkspace
      {...props}
      thread={{
        ...thread,
        requests: [version, { ...secondVersion, status: 'processing', release_id: null }],
      }}
    />,
  );
  fireEvent.click(screen.getByRole('button', { name: 'V1 ready' }));
  await screen.findByText('music1');
  fireEvent.click(screen.getByText('v1 preview'));
  expect(view.container.querySelector('audio')?.getAttribute('src')).toBe('/v1.opus');
  fireEvent.click(screen.getByRole('button', { name: 'Follow latest generation' }));
  await screen.findAllByText('v2 measured evidence');
  expect(view.container.querySelector('audio')).toBeNull();
  expect(screen.queryByText('Candidate preview · may need repairs')).toBeNull();
});

it('does not carry a candidate preview across an automatically followed new request', async () => {
  mockRevisionEvidence();
  const view = render(<MusicWorkspace {...props} />);
  await screen.findByText('music1');
  fireEvent.click(screen.getByText('v1 preview'));
  expect(view.container.querySelector('audio')).not.toBeNull();
  view.rerender(
    <MusicWorkspace
      {...props}
      thread={{
        ...thread,
        requests: [version, { ...secondVersion, status: 'processing', release_id: null }],
      }}
    />,
  );
  await screen.findAllByText('v2 measured evidence');
  expect(view.container.querySelector('audio')).toBeNull();
});

it('preserves mood, volume, mute and authorized playback through A/B switching', async () => {
  mockRevisionEvidence();
  render(<MusicWorkspace {...props} thread={{ ...thread, requests: [version, secondVersion] }} />);
  await screen.findByText('music2');
  fireEvent.click(screen.getByRole('button', { name: 'Test playback settings' }));
  fireEvent.change(screen.getByLabelText('Compare revision'), { target: { value: 'v1' } });
  await screen.findByText('music1');
  expect(
    JSON.parse(screen.getByTestId('music-player').getAttribute('data-settings') ?? '{}'),
  ).toMatchObject({ mood: 2, volume: 0.4, muted: true, playing: true, position: 0 });
  expect(
    screen.getByText(
      'This version has a different or unverified musical timeline. Playback starts from the beginning.',
    ),
  ).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'A · V2' }));
  await screen.findByText('music2');
  expect(screen.getAllByTestId('music-player')).toHaveLength(1);
});
it('keeps manually selected history audible when a new delivery arrives', async () => {
  mockRevisionEvidence();
  const view = render(
    <MusicWorkspace {...props} thread={{ ...thread, requests: [version, secondVersion] }} />,
  );
  await screen.findByText('music2');
  fireEvent.click(screen.getByRole('button', { name: 'V1 ready' }));
  await screen.findByText('music1');
  view.rerender(
    <MusicWorkspace
      {...props}
      revision={1}
      thread={{
        ...thread,
        requests: [version, secondVersion, { ...secondVersion, id: 'v3', release_id: 'music3' }],
      }}
    />,
  );
  expect(screen.getByTestId('music-player').textContent).toContain('music1');
  expect(screen.getByRole('button', { name: 'Follow latest generation' })).toBeTruthy();
});
it('ignores a stale release response after switching versions', async () => {
  let finish: ((response: Response) => void) | undefined;
  const immediate = vi.fn(
    async (path: string) =>
      new Response(
        JSON.stringify(
          path.includes('/progress')
            ? {
                requestId: path.includes('v1') ? 'v1' : 'v2',
                stages: [],
                artifacts: [],
                checks: [],
                notes: [],
              }
            : { id: 'music1', metadata: { license: 'CC0-1.0' }, frames: 480000 },
        ),
      ),
  );
  vi.stubGlobal(
    'fetch',
    vi.fn((path: string) =>
      path.endsWith('music2')
        ? new Promise<Response>((resolve) => {
            finish = resolve;
          })
        : immediate(path),
    ),
  );
  render(<MusicWorkspace {...props} thread={{ ...thread, requests: [version, secondVersion] }} />);
  await waitFor(() => expect(finish).toBeDefined());
  fireEvent.click(screen.getByRole('button', { name: 'V1 ready' }));
  await screen.findByText('music1');
  finish?.(
    new Response(
      JSON.stringify({ id: 'music2', metadata: { license: 'CC0-1.0' }, frames: 480000 }),
    ),
  );
  await waitFor(() => expect(screen.getByTestId('music-player').textContent).toContain('music1'));
});
