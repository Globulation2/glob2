// Cross-replica pub/sub on Postgres LISTEN/NOTIFY. One dedicated connection
// per process listens on every subscribed channel and reconnects with backoff
// (re-issuing LISTEN). Publish identifiers and small deltas, then read the full
// state from the database; payloads over NOTIFY's limit are spilled to a table
// by notify() and read back here before dispatch (notify.ts).
// Delivery is at-most-once: notifications sent while a listener reconnects
// are lost, so subscribers re-read state from reconnect listeners
// (addReconnectListener / `onReconnect`).
import pg from 'pg';
import { assertChannel, notifyPg, spilledId } from './notify.ts';

export type NotificationHandler = (payload: unknown, channel: string) => void;

export interface PubSubLogger {
  warn(object: object, message: string): void;
}

export interface PubSubOptions {
  connectionString: string;
  logger?: PubSubLogger;
  /** Called after the listener reconnected, when notifications may have been missed. */
  onReconnect?: () => void;
  /** First reconnect delay in ms (doubles up to 30 s). */
  reconnectDelayMs?: number;
}

function quoteChannel(channel: string): string {
  assertChannel(channel);
  return `"${channel}"`;
}

export class PgPubSub {
  private readonly options: PubSubOptions;
  private readonly handlers = new Map<string, Set<NotificationHandler>>();
  /** Every subscriber waits for its channel's LISTEN, including concurrent callers. */
  private readonly listening = new Map<string, Promise<void>>();
  private client: pg.Client | undefined;
  private connecting: Promise<void> | undefined;
  private closed = false;
  private reconnectTimer: NodeJS.Timeout | undefined;
  private delay: number;
  private readonly reconnectListeners = new Set<() => void>();
  /** Dispatches wait here while a spilled payload is read, so order is kept. */
  private dispatchQueue: Promise<void> = Promise.resolve();
  private pendingDispatches = 0;
  private reconnects = 0;

  constructor(options: PubSubOptions) {
    this.options = options;
    this.delay = options.reconnectDelayMs ?? 250;
    if (options.onReconnect) this.reconnectListeners.add(options.onReconnect);
  }

  /** True while the listening connection is up (for readiness checks). */
  get connected(): boolean {
    return this.client !== undefined;
  }

  /** How many times the listener reconnected after losing its connection. */
  get reconnectCount(): number {
    return this.reconnects;
  }

  /**
   * Calls `listener` after every reconnect, when notifications may have been
   * missed. Returns a function that removes it.
   */
  addReconnectListener(listener: () => void): () => void {
    this.reconnectListeners.add(listener);
    return () => this.reconnectListeners.delete(listener);
  }

  /** Subscribes to a channel; resolves once LISTEN is active. Returns an unsubscribe function. */
  async subscribe(channel: string, handler: NotificationHandler): Promise<() => Promise<void>> {
    const quoted = quoteChannel(channel);
    let set = this.handlers.get(channel);
    if (!set) {
      set = new Set();
      this.handlers.set(channel, set);
    }
    set.add(handler);
    const unsubscribe = async () => {
      const current = this.handlers.get(channel);
      if (!current) return;
      current.delete(handler);
      if (current.size === 0) {
        this.handlers.delete(channel);
        this.listening.delete(channel);
        if (this.client) await this.client.query(`UNLISTEN ${quoted}`).catch(() => undefined);
      }
    };
    try {
      await this.ensureConnected();
      if (this.closed || !this.client) throw new Error('pub/sub is disconnected');
      const client = this.client;
      let ready = this.listening.get(channel);
      if (!ready) {
        ready = client.query(`LISTEN ${quoted}`).then(() => undefined);
        this.listening.set(channel, ready);
      }
      await ready;
      if (this.closed || this.client !== client) throw new Error('pub/sub is disconnected');
      return unsubscribe;
    } catch (error) {
      // Failed subscriptions have no caller-owned cleanup handle. Do not retain
      // their request closures or deliver to them when a later connection works.
      await unsubscribe();
      throw error;
    }
  }

  /**
   * Publishes a JSON payload. `queryable` may be a pool or a transaction client
   * (NOTIFY is sent on commit). Large payloads are spilled (notify.ts).
   */
  async publish(
    queryable: pg.Pool | pg.PoolClient,
    channel: string,
    payload: unknown,
  ): Promise<void> {
    await notifyPg(queryable, channel, payload);
  }

  async close(): Promise<void> {
    this.closed = true;
    clearTimeout(this.reconnectTimer);
    const client = this.client;
    this.client = undefined;
    this.handlers.clear();
    this.listening.clear();
    if (client) await client.end().catch(() => undefined);
  }

  private ensureConnected(): Promise<void> {
    if (this.client) return Promise.resolve();
    this.connecting ??= this.connect().finally(() => {
      this.connecting = undefined;
    });
    return this.connecting;
  }

  private async connect(): Promise<void> {
    if (this.closed) throw new Error('pub/sub is closed');
    const client = new pg.Client({
      connectionString: this.options.connectionString,
      application_name: 'glob2-pubsub',
    });
    client.on('notification', (message) => this.dispatch(message.channel, message.payload));
    client.on('error', (error) => this.lost(client, error));
    client.on('end', () => this.lost(client, new Error('connection ended')));
    try {
      await client.connect();
      for (const channel of this.handlers.keys())
        await client.query(`LISTEN ${quoteChannel(channel)}`);
      if (this.closed) throw new Error('pub/sub is closed');
      this.client = client;
      this.delay = this.options.reconnectDelayMs ?? 250;
    } catch (error) {
      client.removeAllListeners();
      await client.end().catch(() => undefined);
      throw error;
    }
  }

  private dispatch(channel: string, raw: string | undefined): void {
    if (!this.handlers.has(channel)) return;
    let payload: unknown = raw;
    try {
      payload = raw === undefined ? undefined : JSON.parse(raw);
    } catch {
      // Non-JSON payloads (e.g. from psql) are delivered as the raw string.
    }
    const spilled = spilledId(payload);
    if (spilled === undefined && this.pendingDispatches === 0) {
      this.deliver(channel, payload);
      return;
    }
    // A spilled payload is read back first; later notifications wait for it.
    const client = this.client;
    this.pendingDispatches++;
    this.dispatchQueue = this.dispatchQueue
      .then(async () => {
        if (spilled === undefined) return this.deliver(channel, payload);
        const row = await client?.query<{ payload: unknown }>(
          'SELECT payload FROM notification_payloads WHERE id = $1',
          [spilled],
        );
        if (!row?.rows[0]) {
          this.options.logger?.warn(
            { channel, id: spilled },
            'spilled notification payload missing',
          );
          return;
        }
        this.deliver(channel, row.rows[0].payload);
      })
      .catch((error: unknown) =>
        this.options.logger?.warn({ err: error, channel }, 'spilled notification unreadable'),
      )
      .finally(() => {
        this.pendingDispatches--;
      });
  }

  private deliver(channel: string, payload: unknown): void {
    const set = this.handlers.get(channel);
    if (!set) return;
    for (const handler of set) {
      try {
        handler(payload, channel);
      } catch (error) {
        this.options.logger?.warn({ err: error, channel }, 'notification handler failed');
      }
    }
  }

  private lost(client: pg.Client, error: Error): void {
    if (this.client !== client) return;
    this.client = undefined;
    this.listening.clear();
    client.removeAllListeners();
    client.end().catch(() => undefined);
    if (this.closed) return;
    this.options.logger?.warn({ err: error }, 'pub/sub connection lost; reconnecting');
    this.scheduleReconnect();
  }

  private scheduleReconnect(): void {
    clearTimeout(this.reconnectTimer);
    if (this.closed) return;
    this.reconnectTimer = setTimeout(() => {
      this.ensureConnected().then(
        () => {
          if (this.closed) return;
          this.reconnects++;
          for (const listener of this.reconnectListeners) {
            try {
              listener();
            } catch (error) {
              this.options.logger?.warn({ err: error }, 'pub/sub reconnect listener failed');
            }
          }
        },
        (error: unknown) => {
          if (this.closed) return;
          this.options.logger?.warn({ err: error }, 'pub/sub reconnect failed');
          this.delay = Math.min(this.delay * 2, 30_000);
          this.scheduleReconnect();
        },
      );
    }, this.delay);
  }
}
