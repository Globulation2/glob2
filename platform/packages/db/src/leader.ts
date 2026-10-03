// Single-leader election on a Postgres session advisory lock. Each process
// keeps one dedicated connection and polls pg_try_advisory_lock; the holder
// runs `lead(signal, lease)` until it stops, the connection drops (Postgres
// releases the lock with the session) or stop() is called. Used by the worker
// scheduler (matchmaker, sweeps) so exactly one replica runs it at a time.
//
// A held lock is only as good as the session behind it: a half-open TCP
// connection can leave a process believing it leads after Postgres ended its
// session and handed the lock to another. So the leader connection uses TCP
// keepalive and a query timeout, the leader re-checks every `checkMs` (and on
// verify(), which the scheduler calls before each task run) that its session
// still holds the lock, and with `fencing` each new leader bumps an epoch in
// leader_leases: leader-only writes call assertLease() inside their
// transaction, so a stale leader cannot commit once a newer one exists.
import { createHash } from 'node:crypto';
import { hostname } from 'node:os';
import { sql, type Kysely } from 'kysely';
import pg from 'pg';

/** First half of every platform advisory lock key, so other users of the database don't collide. */
const LOCK_NAMESPACE = 0x67326c62; // 'g2lb'

export function advisoryLockKey(name: string): [number, number] {
  return [LOCK_NAMESPACE, createHash('sha256').update(name).digest().readInt32BE(0)];
}

export interface LeaderLogger {
  info(object: object, message: string): void;
  warn(object: object, message: string): void;
}

/** The current leadership: its lock name and (with fencing) its epoch. */
export interface LeaderLease {
  name: string;
  /** Increases with every new leader; 0 without fencing. */
  epoch: number;
}

/** Thrown when this process no longer (provably) holds the leadership. */
export class LeaderLostError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'LeaderLostError';
  }
}

export interface LeaderOptions {
  connectionString: string;
  /** Lock name, e.g. "scheduler". */
  name: string;
  /** Runs while this process leads; must return (or reject) soon after `signal` aborts. */
  lead: (signal: AbortSignal, lease: LeaderLease) => Promise<void>;
  /** How often a follower retries the lock (default 2000 ms). */
  retryMs?: number;
  /** How often the leader re-checks its lock and lease (default 5000 ms). */
  checkMs?: number;
  /** Timeout of every query on the leader connection (default 10 s). */
  queryTimeoutMs?: number;
  /** Bump and renew an epoch in leader_leases (needs the migrated schema). */
  fencing?: boolean;
  logger?: LeaderLogger;
}

export class LeaderElection {
  private readonly options: LeaderOptions;
  private readonly key: [number, number];
  private readonly holder = `${hostname()}:${process.pid}`;
  private client: pg.Client | undefined;
  private leading: AbortController | undefined;
  private lease: LeaderLease | undefined;
  private stopped = false;
  private timer: NodeJS.Timeout | undefined;
  private checkTimer: NodeJS.Timeout | undefined;
  private loop: Promise<void> | undefined;
  private checking: Promise<void> | undefined;
  private lastVerified = 0;

  constructor(options: LeaderOptions) {
    this.options = options;
    this.key = advisoryLockKey(options.name);
  }

  get isLeader(): boolean {
    return this.leading !== undefined;
  }

  /** The current lease while leading. */
  get currentLease(): LeaderLease | undefined {
    return this.leading ? this.lease : undefined;
  }

  start(): void {
    this.stopped = false;
    this.schedule(0);
  }

  async stop(): Promise<void> {
    this.stopped = true;
    clearTimeout(this.timer);
    clearInterval(this.checkTimer);
    this.leading?.abort();
    await this.loop?.catch(() => undefined);
    await this.disconnect();
  }

  /**
   * Confirms this process still leads: its session holds the lock and (with
   * fencing) its epoch is current. Results are reused for up to `maxAgeMs`.
   * On failure leadership is given up and LeaderLostError is thrown.
   */
  async verify(maxAgeMs = 500): Promise<void> {
    if (!this.leading || this.leading.signal.aborted) {
      throw new LeaderLostError(`not leading ${this.options.name}`);
    }
    if (Date.now() - this.lastVerified <= maxAgeMs) return;
    this.checking ??= this.check().finally(() => {
      this.checking = undefined;
    });
    await this.checking;
    if (!this.leading || this.leading.signal.aborted)
      throw new LeaderLostError(`lost the ${this.options.name} leadership`);
  }

  private schedule(ms: number): void {
    if (this.stopped) return;
    this.timer = setTimeout(() => {
      this.loop = this.attempt().finally(() => this.schedule(this.options.retryMs ?? 2000));
    }, ms);
  }

  private async attempt(): Promise<void> {
    try {
      if (!this.client) {
        const client = new pg.Client({
          connectionString: this.options.connectionString,
          application_name: `glob2-leader-${this.options.name}`,
          // Detect a dead peer on an otherwise idle connection.
          keepAlive: true,
          keepAliveInitialDelayMillis: 10_000,
          query_timeout: this.options.queryTimeoutMs ?? 10_000,
        });
        client.on('error', (error) => this.lost(client, error));
        client.on('end', () => this.lost(client, new Error('connection ended')));
        await client.connect();
        this.client = client;
      }
      const result = await this.client.query<{ locked: boolean }>(
        'SELECT pg_try_advisory_lock($1, $2) AS locked',
        this.key,
      );
      if (!result.rows[0]?.locked || this.stopped) return;
      const epoch = this.options.fencing ? await this.bumpEpoch(this.client) : 0;
      await this.runLeader({ name: this.options.name, epoch });
    } catch (error) {
      this.options.logger?.warn(
        { err: error, lock: this.options.name },
        'leader election attempt failed',
      );
      await this.disconnect();
    }
  }

  private async bumpEpoch(client: pg.Client): Promise<number> {
    const row = await client.query<{ epoch: string }>(
      `INSERT INTO leader_leases (name, epoch, holder) VALUES ($1, 1, $2)
       ON CONFLICT (name) DO UPDATE
         SET epoch = leader_leases.epoch + 1, holder = EXCLUDED.holder,
             acquired_at = now(), renewed_at = now()
       RETURNING epoch`,
      [this.options.name, this.holder],
    );
    return Number(row.rows[0]?.epoch ?? 0);
  }

  private async runLeader(lease: LeaderLease): Promise<void> {
    const controller = new AbortController();
    this.leading = controller;
    this.lease = lease;
    this.lastVerified = Date.now();
    this.options.logger?.info({ lock: this.options.name, epoch: lease.epoch }, 'became leader');
    const checkMs = this.options.checkMs ?? 5000;
    this.checkTimer = setInterval(() => {
      void this.verify(checkMs / 2).catch(() => undefined);
    }, checkMs);
    try {
      await this.options.lead(controller.signal, lease);
    } catch (error) {
      if (!controller.signal.aborted) {
        this.options.logger?.warn({ err: error, lock: this.options.name }, 'leader task failed');
      }
    } finally {
      clearInterval(this.checkTimer);
      this.leading = undefined;
      // Release explicitly when the connection is still healthy.
      await this.client
        ?.query('SELECT pg_advisory_unlock($1, $2)', this.key)
        .catch(() => undefined);
      this.options.logger?.info({ lock: this.options.name }, 'stopped leading');
    }
  }

  /** Re-validates the lock (and lease); gives up leadership when either is gone. */
  private async check(): Promise<void> {
    const client = this.client;
    const leading = this.leading;
    const lease = this.lease;
    if (!client || !leading || !lease) return;
    let reason: string | undefined;
    try {
      const held = await client.query<{ held: boolean }>(
        `SELECT EXISTS (
           SELECT 1 FROM pg_locks
           WHERE locktype = 'advisory' AND pid = pg_backend_pid() AND granted
             AND objsubid = 2 AND classid::int8 = $1 AND objid::int8 = ($2::int8 & 4294967295)
         ) AS held`,
        this.key,
      );
      if (!held.rows[0]?.held) reason = 'advisory lock no longer held';
      else if (this.options.fencing) {
        const renewed = await client.query(
          'UPDATE leader_leases SET renewed_at = now() WHERE name = $1 AND epoch = $2',
          [lease.name, lease.epoch],
        );
        if (renewed.rowCount !== 1) reason = 'a newer leader took over';
      }
    } catch (error) {
      reason = `lease check failed: ${(error as Error).message}`;
    }
    if (reason === undefined) {
      this.lastVerified = Date.now();
      return;
    }
    if (this.leading !== leading) return;
    this.options.logger?.warn({ lock: this.options.name, reason }, 'giving up leadership');
    leading.abort();
    // A connection that failed a check cannot be trusted with the lock.
    if (this.client === client) {
      this.client = undefined;
      client.removeAllListeners();
      client.on('error', () => undefined);
      await client.end().catch(() => undefined);
    }
  }

  private lost(client: pg.Client, error: Error): void {
    if (this.client !== client) return;
    this.options.logger?.warn({ err: error, lock: this.options.name }, 'leader connection lost');
    this.client = undefined;
    this.leading?.abort();
    client.removeAllListeners();
    client.on('error', () => undefined);
    client.end().catch(() => undefined);
  }

  private async disconnect(): Promise<void> {
    const client = this.client;
    this.client = undefined;
    if (client) {
      client.removeAllListeners();
      client.on('error', () => undefined);
      await client.end().catch(() => undefined);
    }
  }
}

/**
 * Fencing check for leader-only writes: call inside the write's transaction.
 * Holds a share lock on the lease row until commit, so a new leader's epoch
 * bump waits for this transaction, and throws LeaderLostError when `lease` is
 * no longer the current epoch. A lease with epoch 0 (no fencing) passes.
 */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export async function assertLease(db: Kysely<any>, lease: LeaderLease | undefined): Promise<void> {
  if (!lease) throw new LeaderLostError('not leading');
  if (lease.epoch === 0) return;
  const row = await sql<{ epoch: string }>`
    SELECT epoch FROM leader_leases WHERE name = ${lease.name} FOR SHARE`.execute(db);
  if (Number(row.rows[0]?.epoch) !== lease.epoch) {
    throw new LeaderLostError(`${lease.name} epoch ${lease.epoch} is no longer current`);
  }
}
