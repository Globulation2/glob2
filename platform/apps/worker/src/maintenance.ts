// Housekeeping and retention (docs/hosting/README.md, "Retention"). Runs on
// the scheduler leader every minute; every delete is bounded (RETENTION_BATCH
// rows per table and run) and served by an index, so a backlog drains over a
// few runs instead of locking a table.
import { sql, type Kysely } from 'kysely';
import { SPILLED_PAYLOAD_RETENTION_MINUTES, type Database } from '@glob2/db';

export interface MaintenanceResult {
  expiredSigninAttempts: number;
  expiredQueueTickets: number;
  deletedRefreshTokens: number;
  deletedAuthFlows: number;
  deletedWebSessions: number;
  deletedSigninAttempts: number;
  deletedRateLimits: number;
  deletedGuests: number;
  deletedChatMessages: number;
  deletedEngineJobs: number;
  deletedProposals: number;
  deletedQueueTickets: number;
  deletedEngineAgents: number;
  deletedNotificationPayloads: number;
}

/** Queue tickets waiting longer than this are expired (the client re-queues). */
export const QUEUE_TICKET_MAX_WAIT_SECONDS = 3600;
/** Refresh tokens are kept this long after expiry, then deleted. */
export const REFRESH_TOKEN_RETENTION_DAYS = 30;
/**
 * Rotated and revoked refresh tokens are kept this long, then deleted: within
 * it, presenting a rotated token is detected as reuse (and revokes its
 * family); after it, the token is simply unknown.
 */
export const REFRESH_TOKEN_REUSE_DETECTION_DAYS = 7;
/** Provider sign-in flows are kept this long after expiry, then deleted. */
export const AUTH_FLOW_RETENTION_HOURS = 24;
/** Web sessions are kept this long after expiry or revocation, then deleted. */
export const WEB_SESSION_RETENTION_DAYS = 30;
/** Finished browser sign-in attempts are kept this long, then deleted. */
export const SIGNIN_ATTEMPT_RETENTION_DAYS = 7;
/**
 * Guest accounts unused this long (no sign-in or realtime session) that never
 * played a match, host no open room and own no catalog map are deleted, with
 * their device credentials and tokens. Guests with match history are kept.
 */
export const GUEST_RETENTION_DAYS = 90;
/** Room chat messages are kept this long. */
export const CHAT_RETENTION_DAYS = 30;
/**
 * Finished engine jobs are kept this long. The newest succeeded verify job of
 * each match is kept for good (the match page shows its verdict details).
 */
export const ENGINE_JOB_RETENTION_DAYS = 30;
/** Resolved match proposals and finished queue tickets are kept this long. */
export const MATCHMAKING_RETENTION_DAYS = 30;
/** Engine agents not seen for this long are forgotten. */
export const ENGINE_AGENT_RETENTION_DAYS = 7;
/** Rows deleted per table and run. */
export const RETENTION_BATCH = 1000;

const days = (n: number) => sql<Date>`now() - make_interval(days => ${n})`;

/** Deletes up to RETENTION_BATCH rows of `table` whose ids `ids` selects. */
async function deleteBatch(
  db: Kysely<Database>,
  table: string,
  key: string,
  ids: ReturnType<typeof sql>,
): Promise<number> {
  const result = await sql`
    DELETE FROM ${sql.table(table)} WHERE ${sql.ref(key)} IN (${ids} LIMIT ${RETENTION_BATCH})`.execute(
    db,
  );
  return Number(result.numAffectedRows ?? 0n);
}
/** Rate-limit counters idle this long are deleted (the longest window is an hour). */
export const RATE_LIMIT_IDLE_HOURS = 24;

/** Housekeeping that must run on exactly one worker (the scheduler leader). */
export async function runMaintenance(db: Kysely<Database>): Promise<MaintenanceResult> {
  const signins = await db
    .updateTable('signin_attempts')
    .set({ status: 'expired', failure_reason: 'expired', completed_at: sql<Date>`now()` })
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
  const tokens =
    (await deleteBatch(
      db,
      'refresh_tokens',
      'id',
      sql`SELECT id FROM refresh_tokens WHERE expires_at < ${days(REFRESH_TOKEN_RETENTION_DAYS)}`,
    )) +
    (await deleteBatch(
      db,
      'refresh_tokens',
      'id',
      sql`SELECT id FROM refresh_tokens WHERE rotated_at < ${days(REFRESH_TOKEN_REUSE_DETECTION_DAYS)}`,
    )) +
    (await deleteBatch(
      db,
      'refresh_tokens',
      'id',
      sql`SELECT id FROM refresh_tokens WHERE revoked_at < ${days(REFRESH_TOKEN_REUSE_DETECTION_DAYS)}`,
    ));
  const flows = await db
    .deleteFrom('auth_flows')
    .where(
      'expires_at',
      '<',
      sql<Date>`now() - make_interval(hours => ${AUTH_FLOW_RETENTION_HOURS})`,
    )
    .executeTakeFirst();
  const webSessions = await db
    .deleteFrom('web_sessions')
    .where((eb) =>
      eb.or([
        eb('expires_at', '<', days(WEB_SESSION_RETENTION_DAYS)),
        eb('revoked_at', '<', days(WEB_SESSION_RETENTION_DAYS)),
      ]),
    )
    .executeTakeFirst();
  const oldSignins = await deleteBatch(
    db,
    'signin_attempts',
    'id',
    sql`SELECT id FROM signin_attempts
        WHERE created_at < ${days(SIGNIN_ATTEMPT_RETENTION_DAYS)} AND status <> 'pending'`,
  );
  const guests = await deleteBatch(
    db,
    'accounts',
    'id',
    sql`SELECT a.id FROM accounts a
        WHERE a.kind = 'guest'
          AND COALESCE(a.last_seen_at, a.created_at) < ${days(GUEST_RETENTION_DAYS)}
          AND NOT EXISTS (SELECT 1 FROM match_participants p WHERE p.account_id = a.id)
          AND NOT EXISTS (SELECT 1 FROM rooms r WHERE r.host_account_id = a.id AND r.status <> 'closed')
          AND NOT EXISTS (SELECT 1 FROM maps m WHERE m.owner_account_id = a.id)
          AND NOT EXISTS (
            SELECT 1 FROM device_credentials d
            WHERE d.account_id = a.id AND d.last_used_at >= ${days(GUEST_RETENTION_DAYS)}
          )`,
  );
  const chat = await deleteBatch(
    db,
    'room_chat_messages',
    'id',
    sql`SELECT id FROM room_chat_messages WHERE sent_at < ${days(CHAT_RETENTION_DAYS)}`,
  );
  const engineJobs = await deleteBatch(
    db,
    'engine_jobs',
    'id',
    sql`SELECT j.id FROM engine_jobs j
        WHERE j.status <> 'queued' AND j.completed_at < ${days(ENGINE_JOB_RETENTION_DAYS)}
          AND NOT (
            j.kind = 'verify-match' AND j.status = 'succeeded' AND NOT EXISTS (
              SELECT 1 FROM engine_jobs n
              WHERE n.match_id = j.match_id AND n.kind = 'verify-match'
                AND n.status = 'succeeded' AND n.completed_at > j.completed_at
            )
          )`,
  );
  const proposals = await deleteBatch(
    db,
    'match_proposals',
    'id',
    sql`SELECT id FROM match_proposals
        WHERE status IN ('started', 'cancelled', 'failed')
          AND resolved_at < ${days(MATCHMAKING_RETENTION_DAYS)}`,
  );
  const doneTickets = await deleteBatch(
    db,
    'queue_tickets',
    'id',
    sql`SELECT id FROM queue_tickets
        WHERE status IN ('matched', 'cancelled', 'declined', 'expired')
          AND updated_at < ${days(MATCHMAKING_RETENTION_DAYS)}`,
  );
  const agents = await db
    .deleteFrom('engine_agents')
    .where('last_seen_at', '<', days(ENGINE_AGENT_RETENTION_DAYS))
    .executeTakeFirst();
  const payloads = await db
    .deleteFrom('notification_payloads')
    .where(
      'created_at',
      '<',
      sql<Date>`now() - make_interval(mins => ${SPILLED_PAYLOAD_RETENTION_MINUTES})`,
    )
    .executeTakeFirst();
  const counters = await db
    .deleteFrom('rate_limits')
    .where('window_start', '<', sql<Date>`now() - make_interval(hours => ${RATE_LIMIT_IDLE_HOURS})`)
    .executeTakeFirst();
  return {
    deletedRateLimits: Number(counters.numDeletedRows),
    deletedAuthFlows: Number(flows.numDeletedRows),
    deletedWebSessions: Number(webSessions.numDeletedRows),
    expiredSigninAttempts: Number(signins.numUpdatedRows),
    expiredQueueTickets: Number(tickets.numUpdatedRows),
    deletedRefreshTokens: tokens,
    deletedSigninAttempts: oldSignins,
    deletedGuests: guests,
    deletedChatMessages: chat,
    deletedEngineJobs: engineJobs,
    deletedProposals: proposals,
    deletedQueueTickets: doneTickets,
    deletedEngineAgents: Number(agents.numDeletedRows),
    deletedNotificationPayloads: Number(payloads.numDeletedRows),
  };
}
