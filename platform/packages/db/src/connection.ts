import { Kysely, PostgresDialect } from 'kysely';
import pg from 'pg';
import type { Database } from './schema.ts';

const INT8_OID = 20;

/**
 * int8 columns (sizes, seeds) are returned as numbers. Every int8 in this
 * schema stays below 2^53; keep it that way or parse those columns yourself.
 */
const types = {
  getTypeParser(oid: number, format?: 'text' | 'binary') {
    if (oid === INT8_OID && format !== 'binary') return (value: string) => Number(value);
    return pg.types.getTypeParser(oid, format as 'text');
  },
} as pg.CustomTypesConfig;

export interface DatabaseOptions {
  connectionString: string;
  /** Maximum pooled connections (default 10). */
  maxConnections?: number;
  applicationName?: string;
  /** Called when an idle pooled connection fails (it is discarded either way). */
  onIdleClientError?: (error: Error) => void;
}

export interface DatabaseHandle {
  pool: pg.Pool;
  db: Kysely<Database>;
  /** Closes Kysely and the pool. */
  close(): Promise<void>;
}

export function createPool(options: DatabaseOptions): pg.Pool {
  const pool = new pg.Pool({
    connectionString: options.connectionString,
    max: options.maxConnections ?? 10,
    application_name: options.applicationName ?? 'glob2-platform',
    types,
  });
  // An idle client that loses its connection (server restart, failover) emits
  // 'error' on the pool; without a listener that would crash the process. The
  // pool discards the client and the next query opens a new one.
  pool.on('error', (error) => options.onIdleClientError?.(error));
  // A checked-out client that loses its connection fails its query; this
  // listener only keeps the extra 'error' event from becoming unhandled.
  pool.on('connect', (client) => client.on('error', () => undefined));
  return pool;
}

export function createDatabase(options: DatabaseOptions): DatabaseHandle {
  const pool = createPool(options);
  const db = new Kysely<Database>({ dialect: new PostgresDialect({ pool }) });
  return {
    pool,
    db,
    // Kysely's destroy() ends the pool it was given.
    close: () => db.destroy(),
  };
}
