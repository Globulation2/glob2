// Cross-replica pub/sub on Postgres LISTEN/NOTIFY. One dedicated connection
// per process listens on every subscribed channel and reconnects with backoff
// (re-issuing LISTEN). NOTIFY payloads are limited to ~8000 bytes, so publish
// identifiers and small deltas, then read the full state from the database.
// Delivery is at-most-once: notifications sent while a listener reconnects
// are lost, so subscribers should re-read state after `onReconnect`.
import pg from 'pg';

export const MAX_NOTIFY_PAYLOAD_BYTES = 7999;
const CHANNEL_PATTERN = /^[a-z][a-z0-9_.:-]{0,62}$/;

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
  if (!CHANNEL_PATTERN.test(channel)) throw new Error(`invalid channel name ${channel}`);
  return `"${channel}"`;
}

export class PgPubSub {
  private readonly options: PubSubOptions;
  private readonly handlers = new Map<string, Set<NotificationHandler>>();
  private client: pg.Client | undefined;
  private connecting: Promise<void> | undefined;
  private closed = false;
  private reconnectTimer: NodeJS.Timeout | undefined;
  private delay: number;

  constructor(options: PubSubOptions) {
    this.options = options;
    this.delay = options.reconnectDelayMs ?? 250;
  }

  /** Subscribes to a channel; resolves once LISTEN is active. Returns an unsubscribe function. */
  async subscribe(channel: string, handler: NotificationHandler): Promise<() => Promise<void>> {
    const quoted = quoteChannel(channel);
    let set = this.handlers.get(channel);
    const first = !set;
    if (!set) {
      set = new Set();
      this.handlers.set(channel, set);
    }
    set.add(handler);
    await this.ensureConnected();
    if (first && this.client) await this.client.query(`LISTEN ${quoted}`);
    return async () => {
      const current = this.handlers.get(channel);
      if (!current) return;
      current.delete(handler);
      if (current.size === 0) {
        this.handlers.delete(channel);
        if (this.client) await this.client.query(`UNLISTEN ${quoted}`).catch(() => undefined);
      }
    };
  }

  /** Publishes a JSON payload. `queryable` may be a pool or a transaction client (NOTIFY is sent on commit). */
  async publish(
    queryable: pg.Pool | pg.PoolClient,
    channel: string,
    payload: unknown,
  ): Promise<void> {
    quoteChannel(channel);
    const text = JSON.stringify(payload);
    if (Buffer.byteLength(text) > MAX_NOTIFY_PAYLOAD_BYTES) {
      throw new Error(`notification on ${channel} exceeds ${MAX_NOTIFY_PAYLOAD_BYTES} bytes`);
    }
    await queryable.query('SELECT pg_notify($1, $2)', [channel, text]);
  }

  async close(): Promise<void> {
    this.closed = true;
    clearTimeout(this.reconnectTimer);
    const client = this.client;
    this.client = undefined;
    this.handlers.clear();
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
    await client.connect();
    for (const channel of this.handlers.keys())
      await client.query(`LISTEN ${quoteChannel(channel)}`);
    this.client = client;
    this.delay = this.options.reconnectDelayMs ?? 250;
  }

  private dispatch(channel: string, raw: string | undefined): void {
    const set = this.handlers.get(channel);
    if (!set) return;
    let payload: unknown = raw;
    try {
      payload = raw === undefined ? undefined : JSON.parse(raw);
    } catch {
      // Non-JSON payloads (e.g. from psql) are delivered as the raw string.
    }
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
    client.removeAllListeners();
    client.end().catch(() => undefined);
    if (this.closed) return;
    this.options.logger?.warn({ err: error }, 'pub/sub connection lost; reconnecting');
    this.scheduleReconnect();
  }

  private scheduleReconnect(): void {
    clearTimeout(this.reconnectTimer);
    this.reconnectTimer = setTimeout(() => {
      this.ensureConnected().then(
        () => this.options.onReconnect?.(),
        (error: unknown) => {
          this.options.logger?.warn({ err: error }, 'pub/sub reconnect failed');
          this.delay = Math.min(this.delay * 2, 30_000);
          this.scheduleReconnect();
        },
      );
    }, this.delay);
  }
}
