// Realtime presence across API replicas. The hub knows only this replica's
// sockets, so "the account's last socket here closed" does not mean the
// player went away: they may hold another socket on another replica. Every
// replica therefore registers itself in api_replicas, heartbeats, and records
// in realtime_presence which accounts hold a socket on it. An account is
// present while a live replica (heartbeat within staleSeconds) has a row.
//
// A replica that stops heartbeating (it crashed, or lost the database for
// longer than staleSeconds) is expired by whichever replica notices first:
// its rows go, and onExpired reports the accounts so their room membership is
// re-evaluated. A replica that finds its own registration expired (it was
// cut off, not dead) registers again and reports it through onRevived, so it
// can re-record the accounts it still holds.
//
// Writers serialise per account with a transaction-scoped advisory lock
// (lockAccount), so a disconnect on one replica cannot interleave with a
// connect on another.
import { randomUUID } from 'node:crypto';
import { sql, type Kysely, type Transaction } from 'kysely';
import type { Database } from '@glob2/db';
import type { Logger } from '@glob2/core';

type Db = Kysely<Database> | Transaction<Database>;

/** How often a replica heartbeats and looks for expired replicas. */
export const PRESENCE_HEARTBEAT_MS = 10_000;
/** A replica without a heartbeat for this long is gone. */
export const PRESENCE_STALE_SECONDS = 45;

export interface ReplicaPresenceOptions {
  /** This replica's id (default: a random UUID per process). */
  replicaId?: string;
  heartbeatMs?: number;
  staleSeconds?: number;
  logger?: Pick<Logger, 'warn' | 'error'>;
}

export class ReplicaPresence {
  readonly replicaId: string;
  readonly staleSeconds: number;
  private readonly db: Kysely<Database>;
  private readonly options: ReplicaPresenceOptions;
  private timer: NodeJS.Timeout | undefined;
  private beating: Promise<void> | undefined;
  private started = false;
  /** Accounts whose presence rows were dropped with an expired replica. */
  onExpired: ((accountIds: string[]) => Promise<void>) | undefined;
  /** This replica's registration had expired and was renewed. */
  onRevived: (() => Promise<void>) | undefined;

  constructor(db: Kysely<Database>, options: ReplicaPresenceOptions = {}) {
    this.db = db;
    this.options = options;
    this.replicaId = options.replicaId ?? randomUUID();
    this.staleSeconds = options.staleSeconds ?? PRESENCE_STALE_SECONDS;
  }

  get running(): boolean {
    return this.started;
  }

  /** Registers this replica and starts heartbeating. */
  async start(): Promise<void> {
    await this.register();
    this.started = true;
    const every = this.options.heartbeatMs ?? PRESENCE_HEARTBEAT_MS;
    if (every > 0) {
      this.timer = setInterval(() => {
        if (this.beating) return;
        this.beating = this.heartbeat()
          .catch((error: unknown) =>
            this.options.logger?.warn({ err: error }, 'realtime presence heartbeat failed'),
          )
          .finally(() => (this.beating = undefined));
      }, every);
      this.timer.unref();
    }
  }

  /**
   * Deregisters this replica: its presence rows go with it. Returns the
   * accounts it held, for the caller to re-evaluate.
   */
  async stop(): Promise<string[]> {
    clearInterval(this.timer);
    this.timer = undefined;
    await this.beating;
    if (!this.started) return [];
    this.started = false;
    const rows = await this.db
      .deleteFrom('realtime_presence')
      .where('replica_id', '=', this.replicaId)
      .returning('account_id')
      .execute();
    await this.db.deleteFrom('api_replicas').where('id', '=', this.replicaId).execute();
    return rows.map((r) => r.account_id);
  }

  /** One heartbeat: renew this replica, then expire replicas that stopped. */
  async heartbeat(): Promise<void> {
    const revived = await this.register();
    if (revived) {
      this.options.logger?.warn(
        { replica: this.replicaId },
        'realtime presence registration had expired; re-recording local sockets',
      );
      await this.onRevived?.();
    }
    const expired = await this.expireStale();
    if (expired.length > 0) await this.onExpired?.(expired);
  }

  /** Upserts this replica's row; true when it had to be created again. */
  private async register(): Promise<boolean> {
    const row = await sql<{ inserted: boolean }>`
      INSERT INTO api_replicas (id) VALUES (${this.replicaId})
      ON CONFLICT (id) DO UPDATE SET heartbeat_at = now()
      RETURNING (xmax = 0) AS inserted`.execute(this.db);
    return this.started && row.rows[0]?.inserted === true;
  }

  /**
   * Deletes replicas without a recent heartbeat (never this one) and their
   * presence rows; returns the accounts those rows named.
   */
  async expireStale(): Promise<string[]> {
    return this.db.transaction().execute(async (trx) => {
      const stale = await trx
        .selectFrom('api_replicas')
        .select('id')
        .where('id', '!=', this.replicaId)
        .where('heartbeat_at', '<', sql<Date>`now() - make_interval(secs => ${this.staleSeconds})`)
        .forUpdate()
        .skipLocked()
        .execute();
      if (stale.length === 0) return [];
      const ids = stale.map((r) => r.id);
      // Delete the rows explicitly (rather than by cascade) to learn whose they were.
      const rows = await trx
        .deleteFrom('realtime_presence')
        .where('replica_id', 'in', ids)
        .returning('account_id')
        .execute();
      await trx.deleteFrom('api_replicas').where('id', 'in', ids).execute();
      return [...new Set(rows.map((r) => r.account_id))];
    });
  }

  /** Serialises presence changes of one account until the transaction ends. */
  async lockAccount(trx: Transaction<Database>, accountId: string): Promise<void> {
    await sql`SELECT pg_advisory_xact_lock(hashtext(${'presence:' + accountId}))`.execute(trx);
  }

  /** Records that this replica holds a socket of the account. */
  async add(db: Db, accountId: string): Promise<void> {
    await db
      .insertInto('realtime_presence')
      .values({ account_id: accountId, replica_id: this.replicaId })
      .onConflict((oc) => oc.columns(['account_id', 'replica_id']).doNothing())
      .execute();
  }

  /** Records that this replica no longer holds a socket of the account. */
  async remove(db: Db, accountId: string): Promise<void> {
    await db
      .deleteFrom('realtime_presence')
      .where('account_id', '=', accountId)
      .where('replica_id', '=', this.replicaId)
      .execute();
  }

  /** Whether any live replica holds a socket of the account. */
  async present(db: Db, accountId: string): Promise<boolean> {
    const row = await db
      .selectFrom('realtime_presence as p')
      .innerJoin('api_replicas as r', 'r.id', 'p.replica_id')
      .select('p.replica_id')
      .where('p.account_id', '=', accountId)
      .where('r.heartbeat_at', '>=', sql<Date>`now() - make_interval(secs => ${this.staleSeconds})`)
      .limit(1)
      .executeTakeFirst();
    return row !== undefined;
  }
}
