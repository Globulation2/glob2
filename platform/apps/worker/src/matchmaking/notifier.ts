// Queue events for clients. The worker has no client sockets: it NOTIFYs the
// API replicas, which forward each event to the account's realtime sockets
// as the protocol event of the same name.
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import type { RealtimeEventData } from '@glob2/protocol';

export type QueueEventName =
  'queue.status' | 'queue.proposal' | 'queue.proposalEnded' | 'queue.matchFound';

/** NOTIFY channel: payload `{ accountId, event, data }` (QueueNotification). */
export const QUEUE_EVENTS_CHANNEL = 'queue_events';

export interface QueueNotification<E extends QueueEventName = QueueEventName> {
  accountId: string;
  event: E;
  data: RealtimeEventData<E>;
}

export interface QueueNotifier {
  /**
   * Sends an event to one account. `db` is the executor of the change it
   * reports: inside a transaction, the event is delivered only on commit.
   */
  send<E extends QueueEventName>(
    db: Kysely<Database>,
    accountId: string,
    event: E,
    data: RealtimeEventData<E>,
  ): Promise<void>;
}

export class PgQueueNotifier implements QueueNotifier {
  async send<E extends QueueEventName>(
    db: Kysely<Database>,
    accountId: string,
    event: E,
    data: RealtimeEventData<E>,
  ): Promise<void> {
    const payload: QueueNotification<E> = { accountId, event, data };
    await sql`SELECT pg_notify(${QUEUE_EVENTS_CHANNEL}, ${JSON.stringify(payload)})`.execute(db);
  }
}

/** Test double: keeps every event in order. */
export class RecordingQueueNotifier implements QueueNotifier {
  readonly events: QueueNotification[] = [];

  async send<E extends QueueEventName>(
    _db: Kysely<Database>,
    accountId: string,
    event: E,
    data: RealtimeEventData<E>,
  ): Promise<void> {
    this.events.push({ accountId, event, data } as QueueNotification);
  }

  of<E extends QueueEventName>(event: E, accountId?: string): QueueNotification<E>[] {
    return this.events.filter(
      (e): e is QueueNotification<E> =>
        e.event === event && (accountId === undefined || e.accountId === accountId),
    );
  }

  clear(): void {
    this.events.length = 0;
  }
}
