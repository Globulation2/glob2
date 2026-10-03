import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';

export interface MaintenanceResult {
  expiredSigninAttempts: number;
  expiredQueueTickets: number;
  deletedRefreshTokens: number;
}

/** Queue tickets waiting longer than this are expired (the client re-queues). */
export const QUEUE_TICKET_MAX_WAIT_SECONDS = 3600;
/** Refresh tokens are kept this long after expiry for reuse detection, then deleted. */
export const REFRESH_TOKEN_RETENTION_DAYS = 30;

/** Housekeeping that must run on exactly one worker (the scheduler leader). */
export async function runMaintenance(db: Kysely<Database>): Promise<MaintenanceResult> {
  const signins = await db
    .updateTable('signin_attempts')
    .set({ status: 'expired' })
    .where('status', '=', 'pending')
    .where('expires_at', '<', sql<Date>`now()`)
    .executeTakeFirst();
  const tickets = await db
    .updateTable('queue_tickets')
    .set({ status: 'expired', updated_at: sql<Date>`now()` })
    .where('status', '=', 'waiting')
    .where(
      'created_at',
      '<',
      sql<Date>`now() - make_interval(secs => ${QUEUE_TICKET_MAX_WAIT_SECONDS})`,
    )
    .executeTakeFirst();
  const tokens = await db
    .deleteFrom('refresh_tokens')
    .where(
      'expires_at',
      '<',
      sql<Date>`now() - make_interval(days => ${REFRESH_TOKEN_RETENTION_DAYS})`,
    )
    .executeTakeFirst();
  return {
    expiredSigninAttempts: Number(signins.numUpdatedRows),
    expiredQueueTickets: Number(tickets.numUpdatedRows),
    deletedRefreshTokens: Number(tokens.numDeletedRows),
  };
}
