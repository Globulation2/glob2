// Realtime fan-out across API replicas. Every replica keeps an index of its own
// sockets (by account, sign-in family and pending browser sign-in) and listens
// on one Postgres NOTIFY channel; anything that must reach a socket is
// published there and delivered by whichever replica holds it. Payloads carry
// identifiers and small data only (larger payloads are spilled by notify());
// bulky state is re-read from the database by the receiving replica.
//
// NOTIFY is at-most-once: whatever is published while this replica's listener
// reconnects is lost. After a reconnect the hub re-syncs its sockets from the
// database: pending browser sign-ins are re-checked here, and resync listeners
// (play/realtime.ts) re-send room state, match tickets and queue prompts.
import type { Kysely } from 'kysely';
import { notify, type Database, type PgPubSub } from '@glob2/db';
import type { Logger } from '@glob2/core';
import type { RealtimeEventName } from '@glob2/protocol';
import { REALTIME_CHANNEL, type PlayFanout } from '@glob2/play';
import type { RealtimeConnection } from './connection.ts';

export { REALTIME_CHANNEL };

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
  | { t: 'handoff'; attemptId: string }
  /** Rooms and matches (published by the API and the worker; see @glob2/play notify.ts). */
  | PlayFanout;

export type HandoffListener = (connection: RealtimeConnection, attemptId: string) => void;
export type PlayListener = (message: PlayFanout) => void;
/** An account's last socket on this replica went away (closed or signed out). */
export type AccountGoneListener = (accountId: string) => void;
/** An account gained a socket or its remaining sockets' generator support changed. */
export type AccountHereListener = (accountId: string) => void;
/** The listener reconnected: re-send state to these accounts' sockets on this replica. */
export type ResyncListener = (accountIds: string[]) => Promise<void>;

export class RealtimeHub {
  private readonly db: Kysely<Database>;
  private readonly pubsub: PgPubSub;
  private readonly logger: Logger;
  private readonly connections = new Map<string, RealtimeConnection>();
  private readonly byAccount = new Map<string, Set<RealtimeConnection>>();
  private readonly attempts = new Map<string, RealtimeConnection>();
  private unsubscribe: (() => Promise<void>) | undefined;
  private removeReconnect: (() => void) | undefined;
  private resyncs = 0;
  onHandoff: HandoffListener | undefined;
  onResync: ResyncListener | undefined;
  onPlay: PlayListener | undefined;
  onAccountGone: AccountGoneListener | undefined;
  onAccountHere: AccountHereListener | undefined;

  constructor(db: Kysely<Database>, pubsub: PgPubSub, logger: Logger) {
    this.db = db;
    this.pubsub = pubsub;
    this.logger = logger;
  }

  async start(): Promise<void> {
    this.unsubscribe = await this.pubsub.subscribe(REALTIME_CHANNEL, (payload) =>
      this.dispatch(payload as FanoutMessage),
    );
    this.removeReconnect = this.pubsub.addReconnectListener(() => {
      this.resync().catch((error: unknown) =>
        this.logger.error({ err: error }, 'realtime resync after reconnect failed'),
      );
    });
  }

  /** Completed resyncs (after listener reconnects), for tests and diagnostics. */
  get resyncCount(): number {
    return this.resyncs;
  }

  /**
   * Re-delivers what notifications may have carried while the listener was
   * down: finished browser sign-ins of watched attempts, then (onResync) room,
   * match and queue state of every account with a socket here.
   */
  async resync(): Promise<void> {
    const accounts = [...this.byAccount.keys()];
    this.logger.warn(
      { sockets: this.connections.size, accounts: accounts.length },
      'realtime listener reconnected; re-syncing sockets',
    );
    for (const [attemptId, holder] of [...this.attempts]) this.onHandoff?.(holder, attemptId);
    await this.onResync?.(accounts);
    this.resyncs++;
  }

  async stop(): Promise<void> {
    this.removeReconnect?.();
    this.removeReconnect = undefined;
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
      if (id === accountId) continue;
      const previousSupport = [...set].every((c) => c.generatorSharing);
      if (set.delete(connection)) {
        if (set.size === 0) {
          this.byAccount.delete(id);
          this.onAccountGone?.(id);
        } else if (previousSupport !== [...set].every((c) => c.generatorSharing)) {
          // Closing an older socket restores the remaining clients' support.
          this.onAccountHere?.(id);
        }
      }
    }
    if (accountId) {
      let set = this.byAccount.get(accountId);
      if (!set) this.byAccount.set(accountId, (set = new Set()));
      if (!set.has(connection)) {
        set.add(connection);
        this.onAccountHere?.(accountId);
      }
    }
  }

  /** Accounts with a socket on this replica. */
  accountIds(): string[] {
    return [...this.byAccount.keys()];
  }

  /** This replica's open sockets signed in as the account. */
  connectionsOf(accountId: string): RealtimeConnection[] {
    return [...(this.byAccount.get(accountId) ?? [])].filter((c) => c.open);
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
    await notify(db, REALTIME_CHANNEL, message);
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
    if (
      message.t === 'room' ||
      message.t === 'roomChat' ||
      message.t === 'roomClosed' ||
      message.t === 'matchStart'
    ) {
      this.onPlay?.(message);
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
