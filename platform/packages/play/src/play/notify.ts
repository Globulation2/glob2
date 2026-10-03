// Notifications about rooms, matches and map jobs. Rooms and matches change
// in the API and in the worker; whichever process commits a change publishes
// it, and every API replica delivers it to the sockets it holds. Payloads carry
// ids only (NOTIFY payloads are small); receivers re-read the state.
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';

type Db = Kysely<Database>;

/** The API's realtime fan-out channel (apps/api/src/realtime/hub.ts). */
export const REALTIME_CHANNEL = 'realtime';
/** NOTIFY channel: `{ jobId, kind }` after a generate-map or validate-map result was applied. */
export const MAP_JOBS_CHANNEL = 'map_jobs';

/** Realtime fan-out messages about rooms and matches (subset of the hub's FanoutMessage). */
export type PlayFanout =
  /** Room state changed: members get room.state. */
  | { t: 'room'; roomId: string }
  /** A chat message: members get room.chat. */
  | { t: 'roomChat'; roomId: string; messageId: string }
  /** Room closed (or `accountIds` removed from it): they get room.closed. */
  | {
      t: 'roomClosed';
      roomId: string;
      reason: 'host_closed' | 'kicked' | 'expired';
      accountIds: string[];
    }
  /** A match was placed on a relay (or moved): every human participant gets match.start. */
  | { t: 'matchStart'; matchId: string; accountIds?: string[] };

/** Publishes after the surrounding transaction commits (NOTIFY is transactional). */
export async function publishPlay(db: Db, message: PlayFanout): Promise<void> {
  await sql`SELECT pg_notify(${REALTIME_CHANNEL}, ${JSON.stringify(message)})`.execute(db);
}

export interface MapJobNotification {
  jobId: string;
  kind: 'generate-map' | 'validate-map';
}

export async function notifyMapJob(db: Db, message: MapJobNotification): Promise<void> {
  await sql`SELECT pg_notify(${MAP_JOBS_CHANNEL}, ${JSON.stringify(message)})`.execute(db);
}
