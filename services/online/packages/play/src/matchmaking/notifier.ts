// Queue events for clients. The worker has no client sockets: it NOTIFYs the
// API replicas, which forward each event to the account's realtime sockets
// as the protocol event of the same name.
import type { Kysely } from 'kysely';
import { notify, type Database } from '@glob2/db';
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
    await notify(db, QUEUE_EVENTS_CHANNEL, payload);
  }
}
