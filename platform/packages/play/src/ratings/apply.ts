// Verified results → ratings and history. The verify-match verdict arrives
// through the engine-job result task (applyEngineJobResult);
// handleEngineJobResult records it (outcomes, team statistics and timelines,
// match artifacts) and applies ratings in the same transaction; it also
// completes warm-map generation jobs. applyMatchRatings is idempotent on its own (matches.rating_status leaves 'pending' exactly once,
// under a row lock), so a re-delivered verdict or a sweep never applies a
// rating change twice.
import { sql, type Kysely, type Transaction } from 'kysely';
import { applyEngineJobResult } from '@glob2/core';
import type { Database } from '@glob2/db';
import { applyMapJobResult } from '../play/maps.ts';
import type { VerifyVerdict } from '@glob2/protocol';
import { STORED_MATCH_SETUP, STORED_VERIFY_VERDICT, readStored } from '../stored.ts';
import {
  contestedTeams,
  decideRating,
  isEmptySeat,
  participantOutcomes,
  type RecordedOutcome,
} from './outcome.ts';
import { ensureAccountEntity, ensureAiEntity, ensureRating, type RatedAi } from './entities.ts';
import { displayRating, rateSides } from './scale.ts';
import { recordWarmMapResult } from '../warmMaps.ts';

type Db = Kysely<Database>;

/** NOTIFY channel carrying `{ matchId }` after a match's verification or ratings change. */
export const MATCH_UPDATES_CHANNEL = 'match_updates';

async function inTransaction<T>(
  db: Db,
  fn: (trx: Transaction<Database>) => Promise<T>,
): Promise<T> {
  if (db.isTransaction) return fn(db as Transaction<Database>);
  return db.transaction().execute(fn);
}

async function notifyMatch(db: Db, matchId: string): Promise<void> {
  await sql`SELECT pg_notify(${MATCH_UPDATES_CHANNEL}, ${JSON.stringify({ matchId })})`.execute(db);
}

/**
 * Applies an engine agent's job result and, for a verify-match verdict,
 * records the verification and applies ratings, all in one transaction;
 * generate-map and validate-map results update generated maps and uploads.
 * Returns false when the job was unknown or already completed (a duplicate
 * delivery), in which case nothing changes.
 */
export async function handleEngineJobResult(db: Db, payload: unknown): Promise<boolean> {
  return inTransaction(db, async (trx) => {
    const applied = await applyEngineJobResult(trx, payload);
    if (applied) {
      const jobId = (payload as { jobId: string }).jobId;
      await recordVerification(trx, jobId);
      await recordWarmMapResult(trx, jobId);
      // Generated maps and uploads (no-op for verify-match jobs).
      await applyMapJobResult(trx, jobId);
    }
    return applied;
  });
}

export type VerificationRecord =
  | {
      recorded: false;
      reason: 'not_verify_job' | 'job_failed' | 'match_missing' | 'already_recorded';
    }
  | { recorded: true; matchId: string; verdict: VerifyVerdict['verdict']; rating: RatingResult };

/**
 * Stores a completed verify-match job's verdict on its match: the verification
 * status, per-team outcomes and participant outcomes, then applies ratings.
 * A failed job (agent error after retries) leaves the match pending for an
 * operator to re-run.
 */
export async function recordVerification(db: Db, jobId: string): Promise<VerificationRecord> {
  return inTransaction(db, async (trx) => {
    const job = await trx
      .selectFrom('engine_jobs')
      .select(['kind', 'status', 'payload', 'result'])
      .where('id', '=', jobId)
      .executeTakeFirst();
    if (!job || job.kind !== 'verify-match') return { recorded: false, reason: 'not_verify_job' };
    if (job.status !== 'succeeded') return { recorded: false, reason: 'job_failed' };
    const matchId = (job.payload as { matchId: string }).matchId;
    const verdict = readStored(STORED_VERIFY_VERDICT, job.result);

    const match = await trx
      .selectFrom('matches')
      .select(['id', 'verification', 'final_tick', 'setup'])
      .where('id', '=', matchId)
      .forUpdate()
      .executeTakeFirst();
    if (!match) return { recorded: false, reason: 'match_missing' };
    if (match.verification !== 'pending') return { recorded: false, reason: 'already_recorded' };

    if (verdict.verdict === 'unverifiable') {
      await trx
        .updateTable('matches')
        .set({ verification: 'unverifiable' })
        .where('id', '=', matchId)
        .execute();
    } else {
      const outcome = verdict.outcome;
      // A shared win (teams of more than one alliance won, e.g. a sudden-death
      // tie) is recorded as a draw for those teams and their participants.
      // Empty seats never win or share a win: closed teams have lost from the
      // start, and older matches' idle colonies (AI `none`) are left out.
      const setup = readStored(STORED_MATCH_SETUP, match.setup);
      const recorded = participantOutcomes(
        new Map(setup.teams.map((t) => [t.team, t.alliance])),
        new Map(outcome.teams.map((t) => [t.team, t.outcome])),
        contestedTeams(setup.seats),
      );
      // A closed team has no colony and nobody to show it for: no stats row.
      const closed = new Set(setup.seats.flatMap((s) => (s.kind === 'closed' ? [s.team] : [])));
      for (const team of outcome.teams) {
        if (closed.has(team.team)) continue;
        const teamOutcome = recorded.get(team.team) ?? team.outcome;
        // Final counters and the 512-tick timeline come from the verifier's
        // result.json (engine-agent); older agents send neither.
        const history = {
          statistics: JSON.stringify(team.statistics ?? {}),
          timeline: JSON.stringify(team.timeline ?? []),
        };
        await trx
          .insertInto('match_team_stats')
          .values({
            match_id: matchId,
            team: team.team,
            outcome: teamOutcome,
            prestige: team.prestige,
            eliminated_tick: team.eliminatedTick ?? null,
            ...history,
          })
          .onConflict((oc) =>
            oc.columns(['match_id', 'team']).doUpdateSet({
              outcome: teamOutcome,
              prestige: team.prestige,
              eliminated_tick: team.eliminatedTick ?? null,
              ...history,
            }),
          )
          .execute();
        // Participants take their team's outcome unless intake marked them abandoned.
        await trx
          .updateTable('match_participants')
          .set({ outcome: teamOutcome })
          .where('match_id', '=', matchId)
          .where('team', '=', team.team)
          .where((eb) => eb.or([eb('outcome', 'is', null), eb('outcome', '!=', 'abandoned')]))
          .execute();
      }
      await recordMatchArtifacts(trx, matchId, {
        record: (job.payload as { recordHash?: string }).recordHash,
        replay: outcome.replayHash,
        result: outcome.resultHash,
      });
      await trx
        .updateTable('matches')
        .set({
          verification: verdict.verdict,
          desync_flagged: verdict.verdict === 'diverged' ? true : undefined,
          final_tick: match.final_tick ?? outcome.finalTick,
        })
        .where('id', '=', matchId)
        .execute();
    }
    const rating = await applyMatchRatings(trx, matchId);
    if (rating.status !== 'applied') await notifyMatch(trx, matchId);
    return { recorded: true, matchId, verdict: verdict.verdict, rating };
  });
}

/**
 * Links a match to its record, replay and verifier result blobs. A blob the
 * `blobs` table does not know (not registered by whoever stored it) is
 * skipped rather than failing the verdict.
 */
export async function recordMatchArtifacts(
  db: Db,
  matchId: string,
  artifacts: Partial<Record<'record' | 'replay' | 'result', string | undefined>>,
): Promise<void> {
  for (const [kind, hash] of Object.entries(artifacts) as [
    'record' | 'replay' | 'result',
    string | undefined,
  ][]) {
    if (!hash) continue;
    await sql`
      INSERT INTO match_artifacts (match_id, kind, blob_sha256)
      SELECT ${matchId}, ${kind}, sha256 FROM blobs WHERE sha256 = ${hash}
      ON CONFLICT (match_id, kind) DO UPDATE SET blob_sha256 = EXCLUDED.blob_sha256`.execute(db);
  }
}

export type RatingResult =
  | { status: 'applied'; reason: 'verified' | 'abandoned'; changes: RatingChange[] }
  | { status: 'unchanged' | 'not_rated'; reason: string }
  | { status: 'waiting' | 'already' | 'missing' };

export interface RatingChange {
  seat: number;
  entityId: string;
  result: 'won' | 'lost';
  before: { mu: number; sigma: number; display: number };
  after: { mu: number; sigma: number; display: number };
}

async function finish(
  trx: Db,
  matchId: string,
  status: 'unchanged' | 'not_rated',
  reason: string,
): Promise<RatingResult> {
  await trx
    .updateTable('matches')
    .set({ rating_status: status, rating_note: reason })
    .where('id', '=', matchId)
    .execute();
  return { status, reason };
}

/**
 * Applies a match's verified result to the ladder of its queue, once.
 * Ratings change only for rated queue matches with verification 'verified'.
 */
export async function applyMatchRatings(
  db: Db,
  matchId: string,
  now: Date = new Date(),
): Promise<RatingResult> {
  return inTransaction(db, async (trx) => {
    const match = await trx
      .selectFrom('matches')
      .select([
        'id',
        'origin',
        'queue_id',
        'rated',
        'verification',
        'rating_status',
        'setup',
        'sim_version',
        'final_tick',
      ])
      .where('id', '=', matchId)
      .forUpdate()
      .executeTakeFirst();
    if (!match) return { status: 'missing' };
    if (match.rating_status !== 'pending') return { status: 'already' };
    if (!match.rated || match.origin !== 'queue' || !match.queue_id) {
      return finish(trx, matchId, 'not_rated', match.origin === 'room' ? 'room' : 'unrated_queue');
    }
    if (match.verification === 'pending') return { status: 'waiting' };
    if (match.verification !== 'verified') {
      return finish(trx, matchId, 'unchanged', `verification_${match.verification}`);
    }
    const ladder = match.queue_id;
    const setup = readStored(STORED_MATCH_SETUP, match.setup);

    // Empty seats (AI `none`) are not rated and take no side.
    const participants = (
      await trx
        .selectFrom('match_participants')
        .selectAll()
        .where('match_id', '=', matchId)
        .orderBy('seat')
        .execute()
    ).filter((p) => !isEmptySeat({ team: p.team, kind: p.kind, ai: p.ai_id }));
    const teamStats = await trx
      .selectFrom('match_team_stats')
      .select(['team', 'outcome'])
      .where('match_id', '=', matchId)
      .execute();
    const decision = decideRating({
      teamAlliance: new Map(setup.teams.map((t) => [t.team, t.alliance])),
      participants: participants.map((p) => ({
        seat: p.seat,
        team: p.team,
        kind: p.kind,
        quitTick: p.quit_tick ?? undefined,
        abandoned: p.outcome === 'abandoned',
      })),
      teamOutcomes: new Map(
        teamStats.map((t) => [
          t.team,
          t.outcome === 'abandoned' ? 'lost' : (t.outcome as RecordedOutcome),
        ]),
      ),
      finalTick: match.final_tick ?? 0,
    });
    if (decision.kind === 'unchanged') return finish(trx, matchId, 'unchanged', decision.reason);

    // Resolve every participant's rating entity.
    const entities = new Map<number, { entityId: string; ai?: RatedAi }>();
    for (const p of participants) {
      if (p.kind === 'human') {
        if (!p.account_id) return finish(trx, matchId, 'unchanged', 'deleted_account');
        entities.set(p.seat, { entityId: await ensureAccountEntity(trx, p.account_id) });
      } else {
        const ai = p.ai_id as RatedAi;
        entities.set(p.seat, { entityId: await ensureAiEntity(trx, ai, match.sim_version), ai });
      }
    }
    const entityIds = [...entities.values()].map((e) => e.entityId);
    if (new Set(entityIds).size !== entityIds.length) {
      // The same entity on two seats (e.g. one AI twice) cannot be rated as two players.
      return finish(trx, matchId, 'unchanged', 'duplicate_entity');
    }
    for (const { entityId, ai } of entities.values()) await ensureRating(trx, entityId, ladder, ai);
    // Lock in a fixed order so concurrent matches sharing players cannot deadlock.
    const locked = await trx
      .selectFrom('ratings')
      .select(['entity_id', 'mu', 'sigma'])
      .where('ladder', '=', ladder)
      .where('entity_id', 'in', [...entityIds].sort())
      .orderBy('entity_id')
      .forUpdate()
      .execute();
    const current = new Map(locked.map((r) => [r.entity_id, { mu: r.mu, sigma: r.sigma }]));

    const sideOf = (team: number) => setup.teams.find((t) => t.team === team)?.alliance;
    const seatsBySide = decision.sides.map((side) =>
      participants
        .filter((p) => sideOf(p.team) === side)
        .map((p) => {
          const entityId = entities.get(p.seat)?.entityId;
          const rating = entityId ? current.get(entityId) : undefined;
          if (!entityId || !rating) throw new Error(`seat ${p.seat} has no locked rating`);
          return { participant: p, entityId, rating };
        }),
    );
    const after = rateSides(
      seatsBySide.map((seats) => seats.map((s) => s.rating)),
      decision.ranks,
    );

    const changes: RatingChange[] = [];
    for (const [s, seats] of seatsBySide.entries()) {
      const won = decision.ranks[s] === 1;
      for (const [k, { participant: p, entityId, rating: b }] of seats.entries()) {
        const a = after[s]?.[k];
        if (!a) throw new Error('openskill returned fewer ratings than it was given');
        const change: RatingChange = {
          seat: p.seat,
          entityId,
          result: won ? 'won' : 'lost',
          before: { ...b, display: displayRating(b) },
          after: { mu: a.mu, sigma: a.sigma, display: displayRating(a) },
        };
        changes.push(change);
        await trx
          .updateTable('ratings')
          .set((eb) => ({
            mu: a.mu,
            sigma: a.sigma,
            games: eb('games', '+', 1),
            wins: eb('wins', '+', won ? 1 : 0),
            last_match_id: matchId,
            updated_at: now,
          }))
          .where('entity_id', '=', entityId)
          .where('ladder', '=', ladder)
          .execute();
        await trx
          .insertInto('rating_history')
          .values({
            match_id: matchId,
            entity_id: entityId,
            ladder,
            result: change.result,
            mu_before: b.mu,
            sigma_before: b.sigma,
            mu_after: a.mu,
            sigma_after: a.sigma,
            display_before: change.before.display,
            display_after: change.after.display,
            created_at: now,
          })
          .execute();
        const abandonedSeat =
          p.outcome === 'abandoned' ||
          (decision.reason === 'abandoned' && decision.sides[s] === decision.abandonedSide);
        await trx
          .updateTable('match_participants')
          .set({
            rating_entity_id: entityId,
            rating_before: change.before.display,
            rating_after: change.after.display,
            outcome: abandonedSeat ? 'abandoned' : change.result,
          })
          .where('match_id', '=', matchId)
          .where('seat', '=', p.seat)
          .execute();
      }
    }
    await trx
      .updateTable('matches')
      .set({ rating_status: 'applied', rating_note: decision.reason, ratings_applied_at: now })
      .where('id', '=', matchId)
      .execute();
    await notifyMatch(trx, matchId);
    return { status: 'applied', reason: decision.reason, changes };
  });
}

/**
 * Backstop sweep (scheduler): applies ratings for matches whose verification
 * is known but whose ratings are still pending, e.g. a verdict recorded by an
 * admin re-run rather than the job-result task.
 */
export async function applyPendingRatings(db: Db, limit = 50): Promise<number> {
  const pending = await db
    .selectFrom('matches')
    .select('id')
    .where('rating_status', '=', 'pending')
    .where('verification', '!=', 'pending')
    .orderBy('created_at')
    .limit(limit)
    .execute();
  let settled = 0;
  for (const { id } of pending) {
    const result = await applyMatchRatings(db, id);
    if (result.status !== 'waiting' && result.status !== 'already') settled++;
  }
  return settled;
}
