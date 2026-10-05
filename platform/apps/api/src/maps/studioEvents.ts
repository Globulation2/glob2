import type { FastifyReply, FastifyRequest } from 'fastify';
import type { PgPubSub } from '@glob2/db';
import { type Studio, STUDIO_CHANNEL } from '@glob2/map-studio';

/** Durable replay owns delivery; Postgres notifications only wake the next drain. */
export async function streamStudioEvents({
  studio,
  pubsub,
  request,
  reply,
  accountId,
  thread,
  cursor,
  authenticate,
}: {
  studio: Studio;
  pubsub: Pick<PgPubSub, 'subscribe' | 'addReconnectListener'>;
  request: FastifyRequest;
  reply: FastifyReply;
  accountId: string;
  thread: string;
  cursor: string;
  authenticate: () => Promise<string>;
}) {
  const snapshot = await studio.get(accountId, thread);
  let closed = false,
    busy = false,
    again = false;
  const cleanup: {
    remove?: () => Promise<void>;
    reconnect?: () => void;
    timer?: ReturnType<typeof setInterval>;
  } = {};
  let delivered = 0,
    maxDeliveryLagMs = 0;
  const resumed = cursor !== '0';
  const started = Date.now();
  const close = (reason = 'client_closed', errorName?: string) => {
    if (closed) return;
    closed = true;
    request.log.info(
      {
        thread,
        cursor,
        resumed,
        delivered,
        maxDeliveryLagMs,
        durationMs: Date.now() - started,
        reason,
        ...(errorName ? { errorName } : {}),
      },
      'Studio stream disconnected',
    );
    clearInterval(cleanup.timer);
    reply.raw.off('close', close);
    cleanup.reconnect?.();
    void cleanup.remove?.();
    reply.raw.end();
  };
  const send = (frame: string) => {
    if (closed) return false;
    // Do not accumulate unbounded data for a suspended or slow browser.
    // Disconnect; EventSource resumes from its last fully received event.
    if (reply.raw.writableLength > 65536 || !reply.raw.write(frame)) {
      close('slow_client');
      return false;
    }
    return true;
  };
  const catchUp = async () => {
    if (closed) return;
    if (busy) {
      again = true;
      return;
    }
    busy = true;
    try {
      do {
        again = false;
        // Session revocation/account deletion must terminate an existing stream.
        const current = await authenticate();
        if (current !== accountId) {
          close('authorization_changed');
          return;
        }
        const events = await studio.events(accountId, thread, cursor);
        for (const event of events) {
          if (!send(`id: ${event.id}\ndata: ${JSON.stringify(event)}\n\n`)) return;
          cursor = event.id;
          delivered++;
          maxDeliveryLagMs = Math.max(maxDeliveryLagMs, Date.now() - Date.parse(event.createdAt));
        }
        if (events.length === 100) again = true;
      } while (again && !closed);
    } catch (error) {
      close('authorization_or_read_error', error instanceof Error ? error.name : 'unknown');
    } finally {
      busy = false;
    }
  };
  reply.raw.on('close', close);
  if (reply.raw.destroyed) close();
  // Subscribe before reading backlog: notifications during the read trigger another drain.
  try {
    cleanup.remove = await pubsub.subscribe(STUDIO_CHANNEL, (payload) => {
      const p = payload as { account?: string; thread?: string };
      if (p.account === accountId && p.thread === thread) {
        if (busy) again = true;
        else if (reply.raw.headersSent) void catchUp();
      }
    });
  } catch (error) {
    reply.raw.off('close', close);
    throw error;
  }
  if (closed) {
    await cleanup.remove();
    return reply;
  }
  cleanup.reconnect = pubsub.addReconnectListener(() => {
    if (reply.raw.headersSent) void catchUp();
  });
  reply.hijack();
  reply.raw.writeHead(200, {
    'content-type': 'text/event-stream; charset=utf-8',
    'cache-control': 'no-cache, no-transform',
    'x-accel-buffering': 'no',
    connection: 'keep-alive',
  });
  request.log.info({ thread, cursor, resumed }, 'Studio stream connected');
  if (!send('retry: 2000\n: connected\n\n')) return reply;
  if (BigInt(cursor) > BigInt(snapshot.cursor ?? '0')) {
    const previousCursor = cursor;
    cursor = snapshot.cursor ?? '0';
    request.log.info({ thread, previousCursor, cursor }, 'Studio stream reset');
    if (!send(`event: reset\ndata: ${JSON.stringify({ cursor })}\n\n`)) return reply;
  }
  cleanup.timer = setInterval(() => {
    send(': heartbeat\n\n');
    void catchUp();
  }, 15000);
  cleanup.timer.unref();
  await catchUp();
  return reply;
}
