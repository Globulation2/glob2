// @vitest-environment jsdom
import { act, cleanup, fireEvent, render, renderHook, screen } from '@testing-library/react';
import { afterEach, expect, it, vi } from 'vitest';
import type { StudioProgress, StudioRequest } from '@glob2/protocol';
import { mergeThread, STAGES, type Thread } from '../src/pages/studio/types.ts';
import { useDeliveryCelebration } from '../src/pages/studio/useDeliveryCelebration.ts';
import { StudioWorkspace } from '../src/pages/studio/StudioWorkspace.tsx';
import { Conversation } from '../src/pages/studio/Conversation.tsx';
const progressOverride = vi.hoisted(() => ({ value: undefined as StudioProgress | undefined }));
vi.mock('../src/pages/studio/MapViewer.tsx', () => ({
  MapViewer: ({
    artifact,
    onReady,
  }: {
    artifact: { url: string; label: string };
    onReady?: (url: string) => void;
  }) => <img src={artifact.url} alt={artifact.label} onLoad={() => onReady?.(artifact.url)} />,
}));
vi.mock('../src/pages/studio/useStudioProgress.ts', () => ({
  useStudioProgress: () => ({
    progress: progressOverride.value ?? {
      requestId: 'second',
      stages: STAGES.map((s) => ({ ...s, status: 'complete' })),
      artifacts: [
        {
          id: 'reference',
          requestId: 'second',
          stage: 'prepare',
          kind: 'reference',
          url: '/reference.png',
          label: 'Reference',
        },
      ],
      checks: [],
    },
  }),
}));
vi.mock('../src/router.tsx', () => ({
  Link: ({ children }: { children: React.ReactNode }) => <span>{children}</span>,
}));
afterEach(() => {
  cleanup();
  progressOverride.value = undefined;
  vi.useRealTimers();
});
const settings = { width: 256, height: 128, players: 4 } as const;
function generation(id: string, status: StudioRequest['status'] = 'ready'): StudioRequest {
  return {
    id,
    thread_id: 'thread',
    kind: 'generate',
    status,
    input: { settings, brief: '', messages: [], pipelineVersion: 'v1' },
    map_id: id,
    map_hash: 'a'.repeat(64),
    charged: status === 'ready',
    error: null,
    created_at: '2026-10-04T12:00:00Z',
  };
}
const thread = (cursor: string, requests: StudioRequest[], brief = ''): Thread => ({
  id: 'thread',
  title: 'Map',
  messages: [],
  requests,
  cursor,
  brief,
});
it('keeps newer committed state when a mutation or history snapshot resolves after the live snapshot', () => {
  const ready = thread('12', [generation('new')], 'Latest brief');
  const delayed = thread('11', [generation('old'), generation('new', 'processing')], 'Old brief');
  const result = mergeThread(ready, delayed);
  expect(result.cursor).toBe('12');
  expect(result.brief).toBe('Latest brief');
  expect(result.requests.find((r) => r.id === 'new')).toMatchObject({
    status: 'ready',
    charged: true,
  });
  expect(result.requests.find((r) => r.id === 'old')).toBeTruthy();
  expect(mergeThread(delayed, ready)).toEqual(result);
});
it('reveals a newly delivered map once, including an immediate inspect-away and return', () => {
  vi.useFakeTimers();
  const hook = renderHook(({ viewed, ready }) => useDeliveryCelebration('new', viewed, ready), {
    initialProps: { viewed: 'new', ready: false },
  });
  expect(hook.result.current).toBe(false);
  hook.rerender({ viewed: 'new', ready: true });
  expect(hook.result.current).toBe(true);
  hook.rerender({ viewed: 'old', ready: true });
  hook.rerender({ viewed: 'new', ready: true });
  expect(hook.result.current).toBe(false);
  act(() => vi.advanceTimersByTime(1000));
  expect(hook.result.current).toBe(false);
});
it('compares delivered previews and keeps both versions pinned when a newer generation arrives', () => {
  const props = {
    id: 'thread',
    thread: thread('10', [generation('first'), generation('second')]),
    wallet: { enabled: true, available: 3, reserved: 0, packs: [], usage: [] },
    busy: false,
    draft: '',
    setDraft: vi.fn(),
    send: vi.fn(),
    settings,
    changeSettings: vi.fn(),
    revise: vi.fn(),
    revision: 1,
    loadEarlier: vi.fn(),
    versionAction: vi.fn(),
  };
  const view = render(<StudioWorkspace {...props} />);
  fireEvent.click(screen.getByText('Build details', { selector: 'summary' }));
  fireEvent.click(screen.getByRole('button', { name: 'Prepare the design' }));
  expect(screen.getByRole('img', { name: 'Reference' })).toBeTruthy();
  fireEvent.change(screen.getByRole('combobox', { name: 'Compare with version' }), {
    target: { value: 'first' },
  });
  expect(screen.getByRole('img', { name: 'Version 2' }).getAttribute('src')).toContain(
    '/maps/second/',
  );
  expect(screen.getByRole('img', { name: 'Version 1' }).getAttribute('src')).toContain(
    '/maps/first/',
  );
  view.rerender(
    <StudioWorkspace
      {...props}
      thread={thread('11', [...props.thread.requests, generation('third', 'processing')])}
    />,
  );
  expect(screen.getByRole('region', { name: 'Map comparison' })).toBeTruthy();
  expect(
    (screen.getByRole('combobox', { name: 'Inspect version' }) as HTMLSelectElement).value,
  ).toBe('second');
  expect(screen.getByRole('img', { name: 'Version 2' }).getAttribute('src')).toContain(
    '/maps/second/',
  );
});
it('anchors the visible conversation when earlier messages are prepended', () => {
  const current = {
    ...thread('1', []),
    messages: [
      { id: 'new', role: 'user' as const, text: 'New', created_at: '2026-10-04T12:00:00Z' },
    ],
    history: { messagesBefore: 'new' },
  };
  const props = {
    thread: current,
    active: false,
    loadEarlier: vi.fn(),
    busy: false,
    choose: vi.fn(),
    inspect: vi.fn(),
  };
  const view = render(<Conversation {...props} />);
  const log = screen.getByRole('log');
  let height = 500;
  Object.defineProperty(log, 'scrollHeight', { get: () => height });
  Object.defineProperty(log, 'clientHeight', { value: 100 });
  log.scrollTop = 40;
  fireEvent.scroll(log);
  fireEvent.click(screen.getByRole('button', { name: /Load earlier/ }));
  height = 750;
  view.rerender(
    <Conversation
      {...props}
      thread={{
        ...current,
        messages: [{ ...current.messages[0]!, id: 'old', text: 'Old' }, ...current.messages],
      }}
    />,
  );
  expect(log.scrollTop).toBe(290);
});
function inspectingProps() {
  return {
    id: 'thread',
    thread: thread('10', [generation('second', 'processing')]),
    wallet: { enabled: true, available: 3, reserved: 0, packs: [], usage: [] },
    busy: false,
    draft: '',
    setDraft: vi.fn(),
    send: vi.fn(),
    settings,
    changeSettings: vi.fn(),
    revise: vi.fn(),
    revision: 1,
    loadEarlier: vi.fn(),
    versionAction: vi.fn(),
  };
}
it('pins a detail image and its stage when live progress advances', () => {
  progressOverride.value = {
    requestId: 'second',
    historical: false,
    checks: [],
    stages: STAGES.map((s) => ({ ...s, status: s.id === 'prepare' ? 'complete' : 'pending' })),
    artifacts: ['First reference', 'Second reference'].map((label) => ({
      id: label,
      label,
      stage: 'prepare',
      kind: 'reference',
      requestId: 'second',
      url: `/${label}.png`,
    })),
  };
  const props = inspectingProps();
  const view = render(<StudioWorkspace {...props} />);
  fireEvent.click(screen.getByRole('button', { name: 'First reference' }));
  progressOverride.value = {
    ...progressOverride.value,
    stages: STAGES.map((s) => ({
      ...s,
      status: s.id === 'terrain' ? 'running' : s.id === 'prepare' ? 'complete' : 'pending',
    })),
    artifacts: [
      ...progressOverride.value.artifacts,
      {
        id: 'terrain',
        label: 'New terrain',
        stage: 'terrain',
        kind: 'generated',
        requestId: 'second',
        url: '/terrain.png',
      },
    ],
  };
  view.rerender(<StudioWorkspace {...props} revision={2} />);
  expect(screen.getByRole('img', { name: 'First reference' })).toBeTruthy();
  expect(
    screen.getByRole('button', { name: 'Prepare the design' }).getAttribute('aria-pressed'),
  ).toBe('true');
});
it('selects the native image when a validation check is opened after a crop detail', () => {
  progressOverride.value = {
    requestId: 'second',
    historical: false,
    stages: STAGES.map((s) => ({
      ...s,
      status: s.id === 'checks' ? 'running' : s.id === 'ready' ? 'pending' : 'complete',
    })),
    artifacts: [
      {
        id: 'crop',
        label: 'Crop',
        stage: 'build',
        kind: 'crop',
        requestId: 'second',
        url: '/crop.png',
      },
      {
        id: 'native',
        label: 'Native',
        stage: 'build',
        kind: 'preview',
        requestId: 'second',
        url: '/native.png',
      },
    ],
    checks: [
      { id: 'food', label: 'Accessible food', status: 'passed', location: { x: 10, y: 20 } },
    ],
  };
  render(<StudioWorkspace {...inspectingProps()} />);
  fireEvent.click(screen.getByRole('button', { name: 'Build the playable map' }));
  fireEvent.click(screen.getByRole('button', { name: 'Crop' }));
  expect(screen.getByRole('img', { name: 'Crop' })).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: /Accessible food/ }));
  expect(screen.getByRole('img', { name: 'Native' })).toBeTruthy();
});
it('waits for the final preview to decode before starting the completion reveal', () => {
  vi.useFakeTimers();
  const props = {
    ...inspectingProps(),
    thread: thread('12', [generation('second')]),
    celebrate: 'second',
  };
  const view = render(<StudioWorkspace {...props} />);
  expect(view.container.querySelector('.ms-celebrate')).toBeNull();
  act(() => vi.advanceTimersByTime(2000));
  expect(view.container.querySelector('.ms-celebrate')).toBeNull();
  fireEvent.load(screen.getByRole('img', { name: 'Version 1' }));
  expect(view.container.querySelector('.ms-celebrate')).toBeTruthy();
  act(() => vi.advanceTimersByTime(700));
  expect(view.container.querySelector('.ms-celebrate')).toBeNull();
  fireEvent.click(screen.getByText('Build details', { selector: 'summary' }));
  fireEvent.click(screen.getByRole('button', { name: 'Prepare the design' }));
  fireEvent.click(screen.getByRole('button', { name: 'Ready' }));
  fireEvent.load(screen.getByRole('img', { name: 'Version 1' }));
  expect(view.container.querySelector('.ms-celebrate')).toBeNull();
});
it('does not defer a live celebration into a later return from stage inspection', () => {
  const hook = renderHook(
    ({ following, decoded }) => useDeliveryCelebration('new', 'new', decoded, following),
    { initialProps: { following: true, decoded: false } },
  );
  hook.rerender({ following: false, decoded: false });
  hook.rerender({ following: true, decoded: true });
  expect(hook.result.current).toBe(false);
});
