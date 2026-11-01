import { useVersionApplication } from '../src/components/studio/useVersionApplication.ts';
// @vitest-environment jsdom
import { useState } from 'react';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render, screen, renderHook, act } from '@testing-library/react';
import {
  ChatComposer,
  ConversationPane,
  ReleaseDialog,
  StudioTabs,
  StudioWorkspace,
} from '../src/components/studio/Studio.tsx';
import { studioSession, useStudioValue } from '../src/components/studio/storage.ts';
import { useRevisionUndo } from '../src/components/studio/useRevisionUndo.ts';
vi.mock('../src/state.tsx', () => ({ useSession: () => ({ account: { id: 'author' } }) }));
beforeEach(() => {
  sessionStorage.clear();
  localStorage.clear();
  HTMLDialogElement.prototype.showModal = function () {
    this.open = true;
  };
  HTMLDialogElement.prototype.close = function () {
    this.open = false;
    this.dispatchEvent(new Event('close'));
  };
});
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});
it('sends on Enter, preserves Shift+Enter and IME composition, and guards unavailable actions', () => {
  const send = vi.fn();
  const props = {
    value: 'Create a grove',
    onChange: vi.fn(),
    onSend: send,
    label: 'Prompt',
    pricing: '1 credit on delivery',
  };
  const view = render(<ChatComposer {...props} />);
  const input = screen.getByRole('textbox');
  fireEvent.keyDown(input, { key: 'Enter', shiftKey: true });
  fireEvent.keyDown(input, { key: 'Enter', isComposing: true });
  expect(send).not.toHaveBeenCalled();
  fireEvent.keyDown(input, { key: 'Enter' });
  expect(send).toHaveBeenCalledTimes(1);
  view.rerender(<ChatComposer {...props} disabledReason="Generation is unavailable." />);
  fireEvent.click(screen.getByRole('button', { name: 'Send' }));
  fireEvent.keyDown(input, { key: 'Enter' });
  expect(send).toHaveBeenCalledTimes(1);
  expect(screen.getByText(/Generation is unavailable/)).toBeTruthy();
});
it('moves focus across tabs without activating until the user selects one', () => {
  const change = vi.fn();
  render(
    <StudioTabs
      label="Views"
      value="preview"
      onChange={change}
      items={[
        { id: 'preview', label: 'Preview' },
        { id: 'history', label: 'History' },
      ]}
    />,
  );
  const tabs = screen.getAllByRole('tab');
  expect(tabs[0]!.hasAttribute('aria-controls')).toBe(false);
  tabs[0]!.focus();
  fireEvent.keyDown(tabs[0]!, { key: 'ArrowRight' });
  expect(document.activeElement).toBe(tabs[1]);
  expect(change).not.toHaveBeenCalled();
  fireEvent.click(tabs[1]!);
  expect(change).toHaveBeenCalledWith('history');
});
it('persists a keyboard adjustable split shared across studios', () => {
  render(<StudioWorkspace conversation="Conversation" artifact="Artifact" />);
  const separator = screen.getByRole('separator');
  fireEvent.keyDown(separator, { key: 'ArrowRight' });
  expect(separator.getAttribute('aria-valuenow')).toBe('42');
  expect(localStorage.getItem('studio-split:author')).toBe('42');
  fireEvent.click(screen.getByRole('button', { name: 'Reset · 40% chat' }));
  expect(separator.getAttribute('aria-valuenow')).toBe('40');
});
it('opens a modal panel and returns focus to its trigger', () => {
  function Panel() {
    const [open, setOpen] = useState(false);
    return (
      <>
        <button onClick={() => setOpen(true)}>Release</button>
        <ReleaseDialog open={open} onClose={() => setOpen(false)} title="Publish revision 3">
          <button>Publish</button>
        </ReleaseDialog>
      </>
    );
  }
  render(<Panel />);
  const trigger = screen.getByRole('button', { name: 'Release' });
  trigger.focus();
  fireEvent.click(trigger);
  expect(screen.getByRole('dialog').getAttribute('aria-labelledby')).toBeTruthy();
  fireEvent.click(screen.getByRole('button', { name: 'Close panel' }));
  expect(document.activeElement).toBe(trigger);
});
it('keeps editing available when session storage throws', () => {
  vi.spyOn(Storage.prototype, 'getItem').mockImplementation(() => {
    throw Error('blocked');
  });
  vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
    throw Error('full');
  });
  const { result } = renderHook(() => useStudioValue('prompt', ''));
  act(() => result.current[1]('Make a forest'));
  expect(result.current[0]).toBe('Make a forest');
  expect(studioSession.getItem('prompt')).toBeNull();
});
it('binds undo to the applied revision and blocks it after newer work', async () => {
  const before = { revision: 1, source: 'original' };
  const { result, rerender } = renderHook(
    ({ draft, revisions }) => useRevisionUndo('undo', draft, revisions),
    { initialProps: { draft: before, revisions: [] as { requestId: string; applied: boolean }[] } },
  );
  act(() => result.current.remember());
  rerender({
    draft: { revision: 2, source: 'generated' },
    revisions: [{ requestId: 'build', applied: true }],
  });
  await act(async () => {});
  expect(result.current.canUndo).toBe(true);
  expect(result.current.before).toEqual(before);
  rerender({
    draft: { revision: 3, source: 'manual' },
    revisions: [{ requestId: 'build', applied: true }],
  });
  expect(result.current.canUndo).toBe(false);
});
it('keeps the delivered undo during discussion and replaces it only for another applied edit', async () => {
  const original = { revision: 1 },
    generated = { revision: 2 };
  const build = { requestId: 'build', applied: true };
  const { result, rerender } = renderHook(
    ({ draft, revisions }) => useRevisionUndo('discussion-undo', draft, revisions),
    {
      initialProps: { draft: original, revisions: [] as { requestId: string; applied: boolean }[] },
    },
  );
  act(() => result.current.remember());
  rerender({ draft: generated, revisions: [build] });
  await act(async () => {});
  act(() => result.current.remember());
  rerender({ draft: generated, revisions: [build, { requestId: 'question', applied: false }] });
  await act(async () => {});
  expect(result.current.canUndo).toBe(true);
  expect(result.current.before).toEqual(original);
  act(() => result.current.remember());
  rerender({
    draft: { revision: 3 },
    revisions: [build, { requestId: 'next-build', applied: true }],
  });
  await act(async () => {});
  expect(result.current.before).toEqual(generated);
  expect(result.current.delivered).toBe(3);
  expect(result.current.canUndo).toBe(true);
});
it('clears private in-memory values when the account/project scope changes', () => {
  const { result, rerender } = renderHook(({ scope }) => useStudioValue(scope, ''), {
    initialProps: { scope: 'first-account:project' },
  });
  act(() => result.current[1]('Private first-account prompt'));
  rerender({ scope: 'second-account:project' });
  expect(result.current[0]).toBe('');
  expect(sessionStorage.getItem('second-account:project')).toBe('""');
  rerender({ scope: 'first-account:project' });
  expect(result.current[0]).toBe('Private first-account prompt');
});
it('preserves history anchoring and offers unread messages without moving the reader', () => {
  const view = render(
    <ConversationPane label="History" count={2} firstMessageId="first">
      <button>Load earlier</button>
      <p>First message</p>
    </ConversationPane>,
  );
  const log = screen.getByRole('log');
  let height = 1000;
  Object.defineProperty(log, 'scrollHeight', { get: () => height });
  Object.defineProperty(log, 'clientHeight', { value: 200 });
  log.scrollTop = 100;
  fireEvent.scroll(log);
  height = 1400;
  view.rerender(
    <ConversationPane label="History" count={3} firstMessageId="older">
      <button>Load earlier</button>
      <p>Older message</p>
      <p>First message</p>
    </ConversationPane>,
  );
  expect(log.scrollTop).toBe(500);
  expect(screen.queryByRole('button', { name: /New messages/ })).toBeNull();
  height = 1500;
  view.rerender(
    <ConversationPane label="History" count={4} firstMessageId="older">
      <button>Load earlier</button>
      <p>Older message</p>
      <p>First message</p>
      <p>New message</p>
    </ConversationPane>,
  );
  expect(log.scrollTop).toBe(500);
  fireEvent.click(screen.getByRole('button', { name: /New messages/ }));
  expect(log.scrollTop).toBe(1500);
});
it('applies a new immutable delivery once, ignores older history pages, and binds undo to its target', async () => {
  const first = { id: 'first', kind: 'generate', status: 'ready' };
  const latest = { ...first, id: 'latest' };
  const older = { ...first, id: 'older' };
  const apply = vi.fn();
  const { result, rerender } = renderHook(
    ({ versions, parent }) => useVersionApplication('versions', versions, parent, apply),
    { initialProps: { versions: [first], parent: 'first' } },
  );
  rerender({ versions: [first, latest], parent: 'first' });
  await act(async () => {});
  expect(apply).toHaveBeenCalledWith(latest);
  rerender({ versions: [older, first, latest], parent: 'latest' });
  await act(async () => {});
  expect(apply).toHaveBeenCalledTimes(1);
  expect(result.current.canUndo).toBe(true);
  act(() => result.current.undo());
  expect(apply).toHaveBeenLastCalledWith(first);
});
it('restores an unloaded historical parent from persisted undo without clearing the target', () => {
  sessionStorage.setItem(
    'unloaded-version-undo',
    JSON.stringify({ version: 'latest', parent: 'historical' }),
  );
  const apply = vi.fn(),
    restoreParent = vi.fn();
  const { result, rerender } = renderHook(
    ({ parent }) =>
      useVersionApplication(
        'unloaded-version-undo',
        [{ id: 'latest', kind: 'generate', status: 'ready' }],
        parent,
        apply,
        restoreParent,
      ),
    { initialProps: { parent: 'latest' } },
  );
  expect(result.current.canUndo).toBe(true);
  rerender({ parent: 'newer' });
  expect(result.current.canUndo).toBe(false);
  act(() => result.current.undo());
  expect(restoreParent).not.toHaveBeenCalled();
  rerender({ parent: 'latest' });
  act(() => result.current.undo());
  expect(restoreParent).toHaveBeenCalledWith('historical');
  expect(apply).not.toHaveBeenCalled();
  expect(result.current.canUndo).toBe(false);
});
it('applies a delivery completed while away without confusing historical inspection with a new delivery', async () => {
  const previous = { id: 'previous', kind: 'generate', status: 'ready' };
  const pending = { ...previous, id: 'pending', status: 'processing' };
  const apply = vi.fn();
  const first = renderHook(() =>
    useVersionApplication('away-delivery', [previous, pending], 'previous', apply),
  );
  await act(async () => {});
  first.unmount();
  const delivered = { ...pending, status: 'ready' };
  const returned = renderHook(() =>
    useVersionApplication('away-delivery', [previous, delivered], 'previous', apply),
  );
  await act(async () => {});
  expect(apply).toHaveBeenCalledExactlyOnceWith(delivered);
  returned.unmount();
  apply.mockClear();
  renderHook(() =>
    useVersionApplication('away-delivery', [previous, delivered], 'previous', apply),
  );
  await act(async () => {});
  expect(apply).not.toHaveBeenCalled();
});

it('keeps focused artifact controls visible when the workspace becomes narrow and acknowledges unseen results', () => {
  let notify = () => {};
  vi.stubGlobal(
    'ResizeObserver',
    class {
      constructor(callback: () => void) {
        notify = callback;
      }
      observe() {}
      disconnect() {}
    },
  );
  const props = {
    conversation: <textarea aria-label="Prompt" />,
    artifact: <button>Download creation</button>,
    result: { id: 'old', status: 'ready' as const },
  };
  const view = render(<StudioWorkspace {...props} />);
  const workspace = view.container.querySelector('.studio-workspace')!;
  let width = 1024;
  Object.defineProperty(workspace, 'clientWidth', { get: () => width });
  const download = screen.getByRole('button', { name: 'Download creation' });
  download.focus();
  width = 700;
  act(() => notify());
  expect(screen.getByRole('tab', { name: 'Preview' }).getAttribute('aria-selected')).toBe('true');
  expect(download.closest('section')!.hidden).toBe(false);
  fireEvent.click(screen.getByRole('tab', { name: 'Chat' }));
  view.rerender(<StudioWorkspace {...props} result={{ id: 'new', status: 'ready' }} />);
  const preview = screen.getByRole('tab', { name: 'Preview · Ready' });
  expect(screen.getByRole('tab', { name: 'Chat' }).getAttribute('aria-selected')).toBe('true');
  expect(download.closest('section')!.hidden).toBe(true);
  expect(screen.getByRole('tabpanel', { name: 'Chat' }).getAttribute('aria-labelledby')).toBe(
    screen.getByRole('tab', { name: 'Chat' }).id,
  );
  fireEvent.click(preview);
  expect(screen.getByRole('tab', { name: 'Preview' }).getAttribute('aria-selected')).toBe('true');
});
it('announces and follows completed in-place replies without re-announcing history or streaming text', () => {
  const props = { label: 'AI conversation', count: 1, firstMessageId: 'request' };
  const view = render(
    <ConversationPane {...props}>
      <p>Working...</p>
    </ConversationPane>,
  );
  const log = screen.getByRole('log');
  let height = 1000;
  Object.defineProperty(log, 'scrollHeight', { get: () => height });
  Object.defineProperty(log, 'clientHeight', { value: 200 });
  log.scrollTop = 100;
  fireEvent.scroll(log);
  height = 1400;
  view.rerender(
    <ConversationPane {...props} completion={{ id: 'request', text: 'AI response ready.' }}>
      <p>Completed answer with much more content.</p>
    </ConversationPane>,
  );
  expect(log.scrollTop).toBe(100);
  expect(screen.getByRole('status').textContent).toBe('AI response ready.');
  expect(screen.getByRole('button', { name: /New messages/ })).toBeTruthy();
  const announcement = screen.getByRole('status').firstChild;
  view.rerender(
    <ConversationPane {...props} completion={{ id: 'request', text: 'AI response ready.' }}>
      <p>Updated rendering of same response.</p>
    </ConversationPane>,
  );
  expect(screen.getByRole('status').firstChild).toBe(announcement);
  fireEvent.click(screen.getByRole('button', { name: /New messages/ }));
  height = 1700;
  view.rerender(
    <ConversationPane {...props} completion={{ id: 'next', text: 'AI response ready.' }}>
      <p>Next completed answer.</p>
    </ConversationPane>,
  );
  expect(log.scrollTop).toBe(1700);
  expect(screen.getByRole('status').firstChild).not.toBe(announcement);
  view.unmount();
  render(
    <ConversationPane {...props} completion={{ id: 'historic', text: 'AI response ready.' }}>
      <p>Already completed history.</p>
    </ConversationPane>,
  );
  expect(screen.getByRole('status').textContent).toBe('');
});
it('opens credit help for blocked Send and Enter without submitting a build', () => {
  const blocked = vi.fn(),
    send = vi.fn();
  render(
    <ChatComposer
      value="Build a map"
      onChange={() => {}}
      onSend={send}
      label="Prompt"
      pricing="1 map credit"
      disabledReason="Buy map credits to build."
      onBlocked={blocked}
    />,
  );
  fireEvent.click(screen.getByRole('button', { name: 'Send' }));
  fireEvent.keyDown(screen.getByRole('textbox'), { key: 'Enter' });
  expect(blocked).toHaveBeenCalledTimes(2);
  expect(send).not.toHaveBeenCalled();
  expect(screen.getByRole('button', { name: 'Send' }).getAttribute('aria-describedby')).toBe(
    screen.getByText(/Buy map credits/).id,
  );
});

it('traps forward and reverse Tab with one control and skips unavailable dialog controls', () => {
  vi.spyOn(HTMLElement.prototype, 'getClientRects').mockReturnValue([{}] as unknown as DOMRectList);
  const view = render(
    <ReleaseDialog open title="Credits" onClose={() => {}}>
      <button hidden>Hidden</button>
      <fieldset disabled>
        <button>Unavailable</button>
      </fieldset>
      <button tabIndex={-1}>Programmatic</button>
    </ReleaseDialog>,
  );
  const close = screen.getByRole('button', { name: 'Close panel' });
  close.focus();
  expect(fireEvent.keyDown(close, { key: 'Tab' })).toBe(false);
  expect(document.activeElement).toBe(close);
  expect(fireEvent.keyDown(close, { key: 'Tab', shiftKey: true })).toBe(false);
  expect(document.activeElement).toBe(close);
  view.rerender(
    <ReleaseDialog open title="Release" onClose={() => {}}>
      <button>Publish</button>
    </ReleaseDialog>,
  );
  const publish = screen.getByRole('button', { name: 'Publish' });
  close.focus();
  fireEvent.keyDown(close, { key: 'Tab', shiftKey: true });
  expect(document.activeElement).toBe(publish);
  fireEvent.keyDown(publish, { key: 'Tab' });
  expect(document.activeElement).toBe(close);
});

it('announces terminal results while mobile Preview hides Chat and stays silent for history', () => {
  const originalWidth = window.innerWidth;
  Object.defineProperty(window, 'innerWidth', { configurable: true, value: 700 });
  const props = {
    conversation: 'Chat',
    artifact: 'Creation',
    result: { id: 'history', status: 'ready' as const, text: 'Historic result.' },
  };
  const view = render(<StudioWorkspace {...props} />);
  expect(screen.getByRole('status').textContent).toBe('');
  fireEvent.click(screen.getByRole('tab', { name: 'Preview' }));
  view.rerender(
    <StudioWorkspace
      {...props}
      result={{ id: 'created', status: 'ready', text: 'Map version 2 ready.' }}
    />,
  );
  expect(screen.getByRole('status').textContent).toBe('Map version 2 ready.');
  const ready = screen.getByRole('status').firstChild;
  view.rerender(
    <StudioWorkspace
      {...props}
      result={{ id: 'failed', status: 'failed', text: 'Map creation failed.' }}
    />,
  );
  expect(screen.getByRole('status').textContent).toBe('Map creation failed.');
  expect(screen.getByRole('status').firstChild).not.toBe(ready);
  fireEvent.click(screen.getByRole('tab', { name: 'Chat' }));
  const previous = screen.getByRole('status').firstChild;
  view.rerender(
    <StudioWorkspace
      {...props}
      result={{ id: 'chat-ready', status: 'ready', text: 'Chat announces this.' }}
    />,
  );
  expect(screen.getByRole('status').firstChild).toBe(previous);
  expect(screen.getByRole('tab', { name: 'Preview · Ready' })).toBeTruthy();
  Object.defineProperty(window, 'innerWidth', { configurable: true, value: originalWidth });
});
