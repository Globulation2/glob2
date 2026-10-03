// Match-end intake from relays: the uploaded match record, the RelayMatchEnded
// report, and the verify-match job that follows. Relays spool and retry both
// calls, so each is idempotent: the same record may be uploaded again, and a
// repeated end report changes nothing (it only re-submits a verify job that
// failed to enqueue the first time).
import { sql, type Kysely } from 'kysely';
import { putContent, submitEngineJob, type BlobStore } from '@glob2/core';
import { notify, type Database } from '@glob2/db';
import {
  MATCH_RECORD_CONTENT_TYPE,
  sameSimVersion,
  type MatchSetup,
  type RelayMatchEnded,
  type RelayNetworkSeat,
} from '@glob2/protocol';
import { MATCH_UPDATES_CHANNEL } from '../ratings/apply.ts';
import { countCatalogPlay } from './catalog.ts';
import { insertBlob } from './maps.ts';
import { publishPlay } from './notify.ts';

type Db = Kysely<Database>;

export type RecordUploadOutcome =
  { ok: true; sha256: string; size: number } | { ok: false; reason: 'not_found' | 'already_ended' };

/**
 * Stores a relay's match record as the match's `record` artifact. While the
 * match has not ended a new upload replaces the previous one; after the end
 * report, only the reported record is accepted again.
 */
export async function storeMatchRecord(
  db: Db,
  blobs: BlobStore,
  matchId: string,
  bytes: Uint8Array,
): Promise<RecordUploadOutcome> {
  const match = await db
    .selectFrom('matches')
    .select(['status', 'end_report'])
    .where('id', '=', matchId)
    .executeTakeFirst();
  if (!match) return { ok: false, reason: 'not_found' };
  const stored = await putContent(blobs, bytes);
  if (match.end_report) {
    const reported = (match.end_report as unknown as RelayMatchEnded).record.sha256;
    if (reported !== stored.sha256) return { ok: false, reason: 'already_ended' };
  }
  await insertBlob(db, stored.sha256, stored.size, MATCH_RECORD_CONTENT_TYPE, 'private');
  await db
    .insertInto('match_artifacts')
    .values({ match_id: matchId, kind: 'record', blob_sha256: stored.sha256 })
    .onConflict((oc) =>
      oc.columns(['match_id', 'kind']).doUpdateSet({
        blob_sha256: stored.sha256,
        created_at: sql<Date>`now()`,
      }),
    )
    .execute();
  return { ok: true, sha256: stored.sha256, size: stored.size };
}

export type MatchEndedOutcome =
  | { ok: true; duplicate: boolean; verifyJobId?: string }
  | {
      ok: false;
      reason: 'not_found' | 'record_missing' | 'record_mismatch' | 'sim_version_mismatch';
    };

/**
 * Applies a relay's end-of-match report: match status, final tick, end reason
 * and desync flag; per seat the disconnect count and quit tick (and outcome
 * 'abandoned' for players who left a game every human abandoned), and the
 * seat's entry of the relay's network summary when the report has one. The room
 * that started the match reopens. A verify-match job is then submitted.
 */
export async function recordMatchEnded(
  db: Db,
  report: RelayMatchEnded,
): Promise<MatchEndedOutcome> {
  const outcome = await db.transaction().execute(async (trx): Promise<MatchEndedOutcome> => {
    const match = await trx
      .selectFrom('matches')
      .select(['id', 'status', 'setup', 'room_id', 'end_report', 'end_reason'])
      .where('id', '=', report.matchId)
      .forUpdate()
      .executeTakeFirst();
    if (!match) return { ok: false, reason: 'not_found' };
    if (match.end_report) return { ok: true, duplicate: true };
    // A match aborted as lost (abortMatchesOnLostRelays) whose relay was alive
    // after all: its real result replaces the abort, and verification and
    // ratings proceed as for any ended match.
    const wasLost = match.status === 'ended' && match.end_reason === 'aborted';
    const setup = match.setup as unknown as MatchSetup;
    if (!sameSimVersion(setup.simVersion, report.simVersion)) {
      return { ok: false, reason: 'sim_version_mismatch' };
    }
    const record = await trx
      .selectFrom('match_artifacts')
      .select('blob_sha256')
      .where('match_id', '=', report.matchId)
      .where('kind', '=', 'record')
      .executeTakeFirst();
    if (!record) return { ok: false, reason: 'record_missing' };
    if (record.blob_sha256 !== report.record.sha256)
      return { ok: false, reason: 'record_mismatch' };

    await trx
      .updateTable('matches')
      .set({
        status: 'ended',
        end_reason: report.reason,
        final_tick: report.finalTick,
        desync_flagged: report.desync.flagged,
        started_at: sql<Date>`coalesce(started_at, ${new Date(report.startedAt)})`,
        ended_at: new Date(report.endedAt),
        relay_id: report.relayId,
        end_report: JSON.stringify(report),
        ...(wasLost
          ? {
              verification: 'pending' as const,
              rating_status: 'pending' as const,
              rating_note: null,
            }
          : {}),
      })
      .where('id', '=', report.matchId)
      .execute();
    const network = new Map<number, RelayNetworkSeat>(
      (report.network?.seats ?? []).map((s) => [s.seat, s]),
    );
    for (const seat of report.seats) {
      const seatNetwork = network.get(seat.seat);
      await trx
        .updateTable('match_participants')
        .set({
          disconnects: seat.disconnects,
          quit_tick: seat.quitTick ?? null,
          ...(seatNetwork ? { network: JSON.stringify(seatNetwork) } : {}),
          ...(report.reason === 'abandoned' && seat.quitTick !== undefined
            ? { outcome: 'abandoned' as const }
            : {}),
        })
        .where('match_id', '=', report.matchId)
        .where('seat', '=', seat.seat)
        .execute();
    }
    if (match.room_id) await reopenRoom(trx, match.room_id, report.matchId);
    // Catalog play counts (a lost match that turns out to have ended counts now).
    await countCatalogPlay(trx, setup.map.hash);
    await notify(trx, MATCH_UPDATES_CHANNEL, { matchId: report.matchId });
    return { ok: true, duplicate: false };
  });
  if (!outcome.ok) return outcome;
  const verifyJobId = await ensureVerifyJob(db, report.matchId);
  return verifyJobId ? { ...outcome, verifyJobId } : outcome;
}

/** Puts a room whose match ended (or never started) back to open, every seat not ready. */
export async function reopenRoom(db: Db, roomId: string, matchId: string): Promise<void> {
  const room = await db
    .updateTable('rooms')
    .set((eb) => ({
      status: 'open',
      revision: eb('revision', '+', 1),
      updated_at: sql<Date>`now()`,
    }))
    .where('id', '=', roomId)
    .where('match_id', '=', matchId)
    .where('status', 'in', ['starting', 'in_match'])
    .returning('id')
    .executeTakeFirst();
  if (!room) return;
  await db.updateTable('room_seats').set({ ready: false }).where('room_id', '=', roomId).execute();
  await publishPlay(db, { t: 'room', roomId });
}

/**
 * Submits the match's verify-match job unless one exists (in flight or done);
 * returns its id. Concurrent calls (relay retries) submit one job: at most one
 * verify job per match can be queued (engine_jobs_one_active_verify_idx).
 */
export async function ensureVerifyJob(db: Db, matchId: string): Promise<string | undefined> {
  const existing = await db
    .selectFrom('engine_jobs')
    .select('id')
    .where('match_id', '=', matchId)
    .where('kind', '=', 'verify-match')
    .orderBy('created_at', 'desc')
    .executeTakeFirst();
  if (existing) return existing.id;
  return submitVerifyJob(db, matchId);
}

/** The queued verify job of a match, if any. */
async function activeVerifyJob(db: Db, matchId: string): Promise<string | undefined> {
  const row = await db
    .selectFrom('engine_jobs')
    .select('id')
    .where('match_id', '=', matchId)
    .where('kind', '=', 'verify-match')
    .where('status', '=', 'queued')
    .executeTakeFirst();
  return row?.id;
}

/** Submits a verify job for an ended match with a record; a concurrent submit's job wins. */
async function submitVerifyJob(db: Db, matchId: string): Promise<string | undefined> {
  const match = await db
    .selectFrom('matches as m')
    .innerJoin('match_artifacts as a', (join) =>
      join.onRef('a.match_id', '=', 'm.id').on('a.kind', '=', 'record'),
    )
    .select(['m.setup', 'm.status', 'a.blob_sha256'])
    .where('m.id', '=', matchId)
    .executeTakeFirst();
  if (!match || match.status !== 'ended') return undefined;
  const setup = match.setup as unknown as MatchSetup;
  try {
    return await submitEngineJob(db, {
      kind: 'verify-match',
      simVersion: setup.simVersion,
      payload: { matchId, setup, recordHash: match.blob_sha256 },
    });
  } catch (error) {
    if ((error as { code?: string }).code !== UNIQUE_VIOLATION) throw error;
    return activeVerifyJob(db, matchId);
  }
}

const UNIQUE_VIOLATION = '23505';

export type ReverifyOutcome =
  | { ok: true; jobId: string; previous: string }
  | {
      ok: false;
      reason:
        | 'not_found'
        | 'not_ended'
        | 'record_missing'
        | 'already_verified'
        | 'already_rated'
        | 'in_progress';
    };

/** Verification states an operator may re-run. */
const REVERIFIABLE = new Set(['pending', 'failed', 'unverifiable']);

/**
 * Runs verification of an ended match again (operator action: platform
 * matches reverify, POST /api/v1/admin/matches/:id/reverify): for matches
 * whose verification failed, is unverifiable, or is pending with no verify job
 * in flight. The match goes back to pending (and, unless ratings were applied,
 * to rating 'pending'), and a new verify job is submitted in the same
 * transaction. With `force`, a queued job is retired first (e.g. one stuck on
 * an agent that disappeared).
 */
export async function reverifyMatch(
  db: Db,
  matchId: string,
  options: { force?: boolean } = {},
): Promise<ReverifyOutcome> {
  return db.transaction().execute(async (trx): Promise<ReverifyOutcome> => {
    const match = await trx
      .selectFrom('matches')
      .select(['id', 'status', 'verification', 'rating_status', 'setup'])
      .where('id', '=', matchId)
      .forUpdate()
      .executeTakeFirst();
    if (!match) return { ok: false, reason: 'not_found' };
    if (match.status !== 'ended') return { ok: false, reason: 'not_ended' };
    if (!REVERIFIABLE.has(match.verification)) return { ok: false, reason: 'already_verified' };
    if (match.rating_status === 'applied') return { ok: false, reason: 'already_rated' };
    const active = await activeVerifyJob(trx, matchId);
    if (active && !options.force) return { ok: false, reason: 'in_progress' };
    if (active) {
      await trx
        .updateTable('engine_jobs')
        .set({
          status: 'failed',
          completed_at: sql<Date>`now()`,
          error: JSON.stringify({ code: 'internal', message: 'retired by an operator re-verify' }),
        })
        .where('id', '=', active)
        .where('status', '=', 'queued')
        .execute();
      await sql`SELECT graphile_worker.remove_job(${active}::text)`.execute(trx);
    }
    const record = await trx
      .selectFrom('match_artifacts')
      .select('blob_sha256')
      .where('match_id', '=', matchId)
      .where('kind', '=', 'record')
      .executeTakeFirst();
    if (!record) return { ok: false, reason: 'record_missing' };
    await trx
      .updateTable('matches')
      .set({ verification: 'pending', rating_status: 'pending', rating_note: null })
      .where('id', '=', matchId)
      .execute();
    const setup = match.setup as unknown as MatchSetup;
    const jobId = await submitEngineJob(trx, {
      kind: 'verify-match',
      simVersion: setup.simVersion,
      payload: { matchId, setup, recordHash: record.blob_sha256 },
    });
    await notify(trx, MATCH_UPDATES_CHANNEL, { matchId });
    return { ok: true, jobId, previous: match.verification };
  });
}

/**
 * A running match its relay has not listed as active in a heartbeat for this
 * long is lost: the relay died, or restarted and forgot it. Relays heartbeat
 * every RELAY_HEARTBEAT_SECONDS (15 s).
 */
export const LOST_MATCH_GRACE_SECONDS = 180;

/**
 * Ends running matches whose relay stopped reporting them (abortMatchesOnLostRelays):
 * end reason 'aborted', no verification and no rating change. Rooms reopen and
 * participants get match.updated through the match_updates NOTIFY. If the
 * relay's end report arrives after all, recordMatchEnded applies it instead.
 * Runs on the scheduler leader.
 */
export async function abortMatchesOnLostRelays(
  db: Db,
  graceSeconds: number = LOST_MATCH_GRACE_SECONDS,
): Promise<string[]> {
  return db.transaction().execute(async (trx) => {
    const lost = await trx
      .updateTable('matches')
      .set({
        status: 'ended',
        end_reason: 'aborted',
        ended_at: sql<Date>`now()`,
        verification: 'not_applicable',
        rating_status: 'not_rated',
        rating_note: 'relay_lost',
      })
      .where('status', '=', 'running')
      .where(
        sql<Date>`coalesce(relay_seen_at, started_at, relay_assigned_at, created_at)`,
        '<',
        sql<Date>`now() - make_interval(secs => ${graceSeconds})`,
      )
      .returning(['id', 'room_id'])
      .execute();
    for (const match of lost) {
      if (match.room_id) await reopenRoom(trx, match.room_id, match.id);
      await notify(trx, MATCH_UPDATES_CHANNEL, { matchId: match.id });
    }
    return lost.map((m) => m.id);
  });
}

/** A starting match nobody reached within this long is cancelled. */
export const STARTING_MATCH_TIMEOUT_SECONDS = 600;

/**
 * Cancels matches that never started (no player reached the relay) and
 * reopens their rooms. Runs on the scheduler leader.
 */
export async function expireStartingMatches(db: Db): Promise<number> {
  return db.transaction().execute(async (trx) => {
    const cancelled = await trx
      .updateTable('matches')
      .set({ status: 'cancelled', ended_at: sql<Date>`now()`, verification: 'not_applicable' })
      .where('status', '=', 'starting')
      .where(
        'created_at',
        '<',
        sql<Date>`now() - make_interval(secs => ${STARTING_MATCH_TIMEOUT_SECONDS})`,
      )
      .returning(['id', 'room_id'])
      .execute();
    for (const match of cancelled) {
      if (match.room_id) await reopenRoom(trx, match.room_id, match.id);
      await notify(trx, MATCH_UPDATES_CHANNEL, { matchId: match.id });
    }
    return cancelled.length;
  });
}
