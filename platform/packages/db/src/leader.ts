// Single-leader election on a Postgres session advisory lock. Each process
// keeps one dedicated connection and polls pg_try_advisory_lock; the holder
// runs `lead(signal)` until it stops, the connection drops (Postgres releases
// the lock with the session) or stop() is called. Used by the matchmaker so
// exactly one worker replica groups players at a time.
import { createHash } from 'node:crypto';
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

export interface LeaderOptions {
  connectionString: string;
  /** Lock name, e.g. "matchmaker". */
  name: string;
  /** Runs while this process leads; must return (or reject) soon after `signal` aborts. */
  lead: (signal: AbortSignal) => Promise<void>;
  /** How often a follower retries the lock (default 2000 ms). */
  retryMs?: number;
  logger?: LeaderLogger;
}

export class LeaderElection {
  private readonly options: LeaderOptions;
  private readonly key: [number, number];
  private client: pg.Client | undefined;
  private leading: AbortController | undefined;
  private stopped = false;
  private timer: NodeJS.Timeout | undefined;
  private loop: Promise<void> | undefined;

  constructor(options: LeaderOptions) {
    this.options = options;
    this.key = advisoryLockKey(options.name);
  }

  get isLeader(): boolean {
    return this.leading !== undefined;
  }

  start(): void {
    this.stopped = false;
    this.schedule(0);
  }

  async stop(): Promise<void> {
    this.stopped = true;
    clearTimeout(this.timer);
    this.leading?.abort();
    await this.loop?.catch(() => undefined);
    await this.disconnect();
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
        });
        client.on('error', (error) => this.lost(client, error));
        await client.connect();
        this.client = client;
      }
      const result = await this.client.query<{ locked: boolean }>(
        'SELECT pg_try_advisory_lock($1, $2) AS locked',
        this.key,
      );
      if (!result.rows[0]?.locked || this.stopped) return;
      await this.runLeader();
    } catch (error) {
      this.options.logger?.warn(
        { err: error, lock: this.options.name },
        'leader election attempt failed',
      );
      await this.disconnect();
    }
  }

  private async runLeader(): Promise<void> {
    const controller = new AbortController();
    this.leading = controller;
    this.options.logger?.info({ lock: this.options.name }, 'became leader');
    try {
      await this.options.lead(controller.signal);
    } catch (error) {
      if (!controller.signal.aborted) {
        this.options.logger?.warn({ err: error, lock: this.options.name }, 'leader task failed');
      }
    } finally {
      this.leading = undefined;
      // Release explicitly when the connection is still healthy.
      await this.client
        ?.query('SELECT pg_advisory_unlock($1, $2)', this.key)
        .catch(() => undefined);
      this.options.logger?.info({ lock: this.options.name }, 'stopped leading');
    }
  }

  private lost(client: pg.Client, error: Error): void {
    if (this.client !== client) return;
    this.options.logger?.warn({ err: error, lock: this.options.name }, 'leader connection lost');
    this.client = undefined;
    this.leading?.abort();
    client.end().catch(() => undefined);
  }

  private async disconnect(): Promise<void> {
    const client = this.client;
    this.client = undefined;
    if (client) await client.end().catch(() => undefined);
  }
}
