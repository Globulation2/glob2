// Realtime fan-out across API replicas. Every replica keeps an index of its own
// sockets (by account, sign-in family and pending browser sign-in) and listens
// on one Postgres NOTIFY channel; anything that must reach a socket is
// published there and delivered by whichever replica holds it. Payloads carry
// identifiers and small data only (NOTIFY is limited to 8000 bytes); bulky
// state is re-read from the database by the receiving replica.
import { sql, type Kysely } from 'kysely';
import type { Database, PgPubSub } from '@glob2/db';
import type { Logger } from '@glob2/core';
import type { RealtimeEventName } from '@glob2/protocol';
import type { RealtimeConnection } from './connection.ts';

export const REALTIME_CHANNEL = 'realtime';

export type FanoutTarget = { account: string } | { family: string } | { connection: string };

export type FanoutMessage =
  | {
      t: 'event';
      to: FanoutTarget;
      event: RealtimeEventName;
      data: Record<string, unknown>;
      /** Close the matching sockets after sending (ban, revoked sign-in). */
      close?: boolean;
      /** Drop the matching sockets' authentication instead of closing them. */
      signOut?: boolean;
    }
  | { t: 'handoff'; attemptId: string };

export type HandoffListener = (connection: RealtimeConnection, attemptId: string) => void;

export class RealtimeHub {
  private readonly db: Kysely<Database>;
  private readonly pubsub: PgPubSub;
  private readonly logger: Logger;
  private readonly connections = new Map<string, RealtimeConnection>();
  private readonly byAccount = new Map<string, Set<RealtimeConnection>>();
  private readonly attempts = new Map<string, RealtimeConnection>();
  private unsubscribe: (() => Promise<void>) | undefined;
  onHandoff: HandoffListener | undefined;

  constructor(db: Kysely<Database>, pubsub: PgPubSub, logger: Logger) {
    this.db = db;
    this.pubsub = pubsub;
    this.logger = logger;
  }

  async start(): Promise<void> {
    this.unsubscribe = await this.pubsub.subscribe(REALTIME_CHANNEL, (payload) =>
      this.dispatch(payload as FanoutMessage),
    );
  }

  async stop(): Promise<void> {
    await this.unsubscribe?.();
    this.unsubscribe = undefined;
    for (const connection of this.connections.values())
      connection.close(1001, 'server shutting down');
  }

  get size(): number {
    return this.connections.size;
  }

  add(connection: RealtimeConnection): void {
    this.connections.set(connection.id, connection);
  }

  remove(connection: RealtimeConnection): void {
    this.connections.delete(connection.id);
    this.setAccount(connection, undefined);
    for (const [attemptId, holder] of this.attempts) {
      if (holder === connection) this.attempts.delete(attemptId);
    }
  }

  /** Re-indexes a socket after it authenticated as `accountId` (or signed out). */
  setAccount(connection: RealtimeConnection, accountId: string | undefined): void {
    for (const [id, set] of this.byAccount) {
      if (set.delete(connection) && set.size === 0) this.byAccount.delete(id);
    }
    if (accountId) {
      let set = this.byAccount.get(accountId);
      if (!set) this.byAccount.set(accountId, (set = new Set()));
      set.add(connection);
    }
  }

  watchAttempt(attemptId: string, connection: RealtimeConnection): void {
    this.attempts.set(attemptId, connection);
  }

  unwatchAttempt(attemptId: string): void {
    this.attempts.delete(attemptId);
  }

  holderOf(attemptId: string): RealtimeConnection | undefined {
    return this.attempts.get(attemptId);
  }

  /** Publishes to every replica (including this one), after the surrounding transaction commits. */
  async publish(message: FanoutMessage, db: Kysely<Database> = this.db): Promise<void> {
    await sql`SELECT pg_notify(${REALTIME_CHANNEL}, ${JSON.stringify(message)})`.execute(db);
  }

  /** Sends an event to the sockets of an account on every replica. */
  sendToAccount(
    accountId: string,
    event: RealtimeEventName,
    data: Record<string, unknown>,
    options: { close?: boolean; signOut?: boolean } = {},
  ): Promise<void> {
    return this.publish({ t: 'event', to: { account: accountId }, event, data, ...options });
  }

  private targets(to: FanoutTarget): RealtimeConnection[] {
    if ('account' in to) return [...(this.byAccount.get(to.account) ?? [])];
    if ('connection' in to) {
      const connection = this.connections.get(to.connection);
      return connection ? [connection] : [];
    }
    return [...this.connections.values()].filter((c) => c.familyId === to.family);
  }

  private dispatch(message: FanoutMessage): void {
    if (!message || typeof message !== 'object') return;
    if (message.t === 'handoff') {
      const holder = this.attempts.get(message.attemptId);
      if (holder) this.onHandoff?.(holder, message.attemptId);
      return;
    }
    if (message.t === 'event') {
      for (const connection of this.targets(message.to)) {
        connection.sendEvent(message.event, message.data);
        if (message.close)
          connection.close(1008, String(message.data['reason'] ?? 'session ended'));
        else if (message.signOut) connection.signOut();
      }
      return;
    }
    this.logger.warn({ message }, 'unknown realtime fan-out message');
  }
}
