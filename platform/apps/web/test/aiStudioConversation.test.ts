import { expect, it } from 'vitest';
import { projectConversation } from '../src/pages/aiStudio/conversation.ts';
it('reads API descending requests chronologically while selecting the newest terminal result', () => {
  const requests = [
    { id: 'active', status: 'running', response: '' },
    { id: 'newest-complete', status: 'completed', response: 'New result' },
    { id: 'old-failure', status: 'failed', response: 'Earlier result' },
  ];
  const conversation = projectConversation(requests);
  expect(conversation.messages.map((request) => request.id)).toEqual([
    'old-failure',
    'newest-complete',
    'active',
  ]);
  expect(conversation.terminal?.id).toBe('newest-complete');
  expect(requests.map((request) => request.id)).toEqual([
    'active',
    'newest-complete',
    'old-failure',
  ]);
});
it('selects a newer failure instead of announcing an older completion as ready', () => {
  const conversation = projectConversation([
    { id: 'new-failure', status: 'failed' },
    { id: 'old-complete', status: 'completed' },
  ]);
  expect(conversation.terminal).toEqual({ id: 'new-failure', status: 'failed' });
  expect(projectConversation([])).toEqual({
    messages: [],
    terminal: undefined,
    pending: undefined,
  });
});

it('keeps an uncertain provider result pending while cancellation is a terminal result', () => {
  const conversation = projectConversation([
    { id: 'reconcile', status: 'uncertain' },
    { id: 'cancelled', status: 'cancelled' },
    { id: 'older', status: 'completed' },
  ]);
  expect(conversation.pending).toEqual({ id: 'reconcile', status: 'uncertain' });
  expect(conversation.terminal).toEqual({ id: 'cancelled', status: 'cancelled' });
});
