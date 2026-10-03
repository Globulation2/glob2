// The one way the platform sends a NOTIFY. Postgres refuses payloads of 8000
// bytes or more, and the refusal aborts the surrounding transaction, so a
// payload over the limit is stored in notification_payloads and the
// notification carries only its id ({"$spilled": "<uuid>"}); PgPubSub reads it
// back before dispatching, so subscribers always see the original payload.
// Inside a transaction the row and the notification are both committed (or
// dropped) with it.
import { sql, type Kysely } from 'kysely';
import type pg from 'pg';

/** Largest payload sent inline (Postgres' limit is 8000 bytes, exclusive). */
export const MAX_NOTIFY_PAYLOAD_BYTES = 7999;
/** Key of the stand-in payload that names a spilled row. */
export const SPILLED_PAYLOAD_KEY = '$spilled';
/** Spilled payloads older than this are deleted (worker maintenance). */
export const SPILLED_PAYLOAD_RETENTION_MINUTES = 60;

const CHANNEL_PATTERN = /^[a-z][a-z0-9_.:-]{0,62}$/;

export function assertChannel(channel: string): void {
  if (!CHANNEL_PATTERN.test(channel)) throw new Error(`invalid channel name ${channel}`);
}

/** The spilled row id if `payload` is a stand-in, else undefined. */
export function spilledId(payload: unknown): string | undefined {
  if (!payload || typeof payload !== 'object' || Array.isArray(payload)) return undefined;
  const keys = Object.keys(payload);
  const id = (payload as Record<string, unknown>)[SPILLED_PAYLOAD_KEY];
  return keys.length === 1 && typeof id === 'string' ? id : undefined;
}

/**
 * Publishes `payload` (JSON) on `channel` through Kysely. With a transaction as
 * `db`, delivery happens on commit.
 */
// The helper works below the typed schema (any Kysely<Database> or transaction).
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export async function notify(db: Kysely<any>, channel: string, payload: unknown): Promise<void> {
  assertChannel(channel);
  const text = JSON.stringify(payload ?? null);
  if (Buffer.byteLength(text) <= MAX_NOTIFY_PAYLOAD_BYTES) {
    await sql`SELECT pg_notify(${channel}, ${text})`.execute(db);
    return;
  }
  const row = await sql<{ id: string }>`
    INSERT INTO notification_payloads (channel, payload)
    VALUES (${channel}, ${text}::jsonb) RETURNING id`.execute(db);
  const id = row.rows[0]?.id;
  if (!id) throw new Error('could not store a spilled notification payload');
  await sql`SELECT pg_notify(${channel}, ${JSON.stringify({ [SPILLED_PAYLOAD_KEY]: id })})`.execute(
    db,
  );
}

/** The same as notify(), for a node-postgres pool or client. */
export async function notifyPg(
  queryable: pg.Pool | pg.PoolClient | pg.Client,
  channel: string,
  payload: unknown,
): Promise<void> {
  assertChannel(channel);
  let text = JSON.stringify(payload ?? null);
  if (Buffer.byteLength(text) > MAX_NOTIFY_PAYLOAD_BYTES) {
    const row = await queryable.query<{ id: string }>(
      'INSERT INTO notification_payloads (channel, payload) VALUES ($1, $2::jsonb) RETURNING id',
      [channel, text],
    );
    text = JSON.stringify({ [SPILLED_PAYLOAD_KEY]: row.rows[0]?.id });
  }
  await queryable.query('SELECT pg_notify($1, $2)', [channel, text]);
}
