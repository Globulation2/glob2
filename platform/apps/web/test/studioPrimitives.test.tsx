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
