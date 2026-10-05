import { EventEmitter } from 'node:events';
import type { FastifyReply, FastifyRequest } from 'fastify';
import type { Studio } from '@glob2/map-studio';
import type { StudioEvent, StudioThread } from '@glob2/protocol';
import { afterEach, expect, it, vi } from 'vitest';
import { streamStudioEvents } from '../src/maps/studioEvents.ts';

function fixture() {
  const raw = Object.assign(new EventEmitter(), {
    destroyed: false,
    headersSent: false,
    writableLength: 0,
    write: vi.fn((_frame: string) => true),
    writeHead: vi.fn(() => {
      raw.headersSent = true;
    }),
    end: vi.fn(),
  });
  const remove = vi.fn(async () => undefined),
    reconnect = vi.fn();
  const pubsub = {
    subscribe: vi.fn(async (_channel: string, _handler: (payload: unknown) => void) => remove),
    addReconnectListener: vi.fn((_handler: () => void) => reconnect),
  };
  const studio = {
    get: vi.fn(async () => ({ cursor: '2' }) as StudioThread),
    events: vi.fn(async (): Promise<StudioEvent[]> => []),
  };
  const reply = { raw, hijack: vi.fn() };
  const options = {
    studio: studio as unknown as Studio,
    pubsub,
    request: { log: { info: vi.fn() } } as unknown as FastifyRequest,
    reply: reply as unknown as FastifyReply,
    accountId: 'owner',
    thread: 'thread',
    cursor: '0',
    authenticate: vi.fn(async () => 'owner'),
  };
  return { raw, studio, pubsub, remove, reconnect, reply, options };
}

afterEach(() => vi.useRealTimers());

it('removes response listeners when subscription setup fails', async () => {
  const f = fixture();
  f.pubsub.subscribe.mockRejectedValueOnce(new Error('listener unavailable'));
  await expect(streamStudioEvents(f.options)).rejects.toThrow('listener unavailable');
  expect(f.raw.listenerCount('close')).toBe(0);
  expect(f.reply.hijack).not.toHaveBeenCalled();
  expect(f.raw.end).not.toHaveBeenCalled(); // Fastify can still return its normal error response.
});

it('cleans up a subscription that finishes after the browser disconnects', async () => {
  const f = fixture();
  let subscribed!: () => void;
  f.pubsub.subscribe.mockImplementationOnce(async () => {
    await new Promise<void>((resolve) => {
      subscribed = resolve;
    });
    return f.remove;
  });
  const open = streamStudioEvents(f.options);
  await vi.waitFor(() => expect(subscribed).toBeDefined());
  f.raw.emit('close');
  subscribed();
  await open;
  expect(f.remove).toHaveBeenCalledOnce();
  expect(f.reply.hijack).not.toHaveBeenCalled();
  expect(f.raw.listenerCount('close')).toBe(0);
});

it('does not leave a heartbeat running after a backpressured reset', async () => {
  vi.useFakeTimers();
  const f = fixture();
  f.options.cursor = '9';
  f.raw.write.mockReturnValueOnce(true).mockReturnValueOnce(false);
  await streamStudioEvents(f.options);
  expect(f.raw.end).toHaveBeenCalledOnce();
  expect(f.remove).toHaveBeenCalledOnce();
  expect(f.reconnect).toHaveBeenCalledOnce();
  expect(vi.getTimerCount()).toBe(0);
});

it('drains a notification arriving during a backlog read and releases every subscription', async () => {
  const f = fixture();
  const event = (id: string): StudioEvent => ({
    id,
    requestId: null,
    createdAt: new Date().toISOString(),
    type: 'state',
    payload: { status: 'processing' },
  });
  let finishRead!: () => void;
  f.studio.events
    .mockImplementationOnce(async () => {
      await new Promise<void>((resolve) => {
        finishRead = resolve;
      });
      return [event('1')];
    })
    .mockResolvedValueOnce([event('2')]);
  const open = streamStudioEvents(f.options);
  await vi.waitFor(() => expect(finishRead).toBeDefined());
  f.pubsub.subscribe.mock.calls[0]![1]({ account: 'owner', thread: 'thread' });
  finishRead();
  await open;
  expect(f.studio.events.mock.calls.map((call) => call)).toHaveLength(2);
  const frames = f.raw.write.mock.calls.map(([frame]) => frame).join('');
  expect(frames.match(/id: 1\n/g)).toHaveLength(1);
  expect(frames.match(/id: 2\n/g)).toHaveLength(1);
  expect(f.options.authenticate).toHaveBeenCalledTimes(2);
  f.raw.emit('close');
  expect(f.remove).toHaveBeenCalledOnce();
  expect(f.reconnect).toHaveBeenCalledOnce();
  expect(f.raw.listenerCount('close')).toBe(0);
});
