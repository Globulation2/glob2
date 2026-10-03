// Ticket operations behind the realtime methods queue.join, queue.leave and
// queue.respond. They run in the API (any replica) while the matchmaker runs
// on the worker leader; both sides only move rows under conditions on their
// current status, so they can interleave safely. AccessPolicy.canQueue and
// the sim-version check stay with the API handler, before joinQueue.
import { sql, type Kysely } from 'kysely';
import type { ResolvedQueue } from '@glob2/core';
import type { Database } from '@glob2/db';
import { accountRating } from '../ratings/entities.ts';
import type { RegionRtt } from './grouping.ts';

type Db = Kysely<Database>;

export interface JoinQueueRequest {
  accountId: string;
  queue: ResolvedQueue;
  /** simVersionKey() of the client. */
  simVersion: string;
  regions: readonly RegionRtt[];
  allowAiOpponent?: boolean;
  now?: Date;
}

export type JoinQueueResult =
  | { ok: true; ticketId: string; joinedAt: Date }
  | { ok: false; code: 'guest_not_allowed' | 'already_queued' | 'account_inactive' }
  | { ok: false; code: 'cooldown'; until: Date };

/**
 * Adds a waiting ticket. Rated queues are for registered accounts only
 * (guests play rooms and casual queues). The ticket snapshots the account's
 * rating on the queue's ladder for grouping.
 */
export async function joinQueue(db: Db, request: JoinQueueRequest): Promise<JoinQueueResult> {
  const now = request.now ?? new Date();
  const account = await db
    .selectFrom('accounts')
    .select(['kind', 'status'])
    .where('id', '=', request.accountId)
    .executeTakeFirst();
  if (!account || account.status !== 'active') return { ok: false, code: 'account_inactive' };
  if (request.queue.rated && account.kind === 'guest') {
    return { ok: false, code: 'guest_not_allowed' };
  }
  const cooldown = await db
    .selectFrom('queue_cooldowns')
    .select('until')
    .where('account_id', '=', request.accountId)
    .where('until', '>', now)
    .executeTakeFirst();
  if (cooldown) return { ok: false, code: 'cooldown', until: cooldown.until };
  const rating = await accountRating(db, request.accountId, request.queue.id);
  const inserted = await db
    .insertInto('queue_tickets')
    .values({
      queue_id: request.queue.id,
      account_id: request.accountId,
      sim_version: request.simVersion,
      region_rtts: JSON.stringify(request.regions),
      rating_mu: rating.mu,
      rating_sigma: rating.sigma,
      allow_ai_opponent: request.allowAiOpponent ?? true,
      created_at: now,
      updated_at: now,
    })
    .onConflict((oc) =>
      // Must repeat the partial index predicate literally for inference.
      oc
        .column('account_id')
        .where(sql<boolean>`status IN ('waiting', 'proposed')`)
        .doNothing(),
    )
    .returning(['id', 'created_at'])
    .executeTakeFirst();
  if (!inserted) return { ok: false, code: 'already_queued' };
  return { ok: true, ticketId: inserted.id, joinedAt: inserted.created_at };
}

export type LeaveQueueResult = 'left' | 'declined' | 'starting' | 'not_found';

/**
 * Leaves the queue. Leaving while in a proposal that waits for accepts counts
 * as declining it (the matchmaker applies the cooldown); once a proposal is
 * starting, the player can no longer leave through the queue.
 */
export async function leaveQueue(
  db: Db,
  accountId: string,
  ticketId: string,
  now: Date = new Date(),
): Promise<LeaveQueueResult> {
  return db.transaction().execute(async (trx) => {
    const ticket = await trx
      .selectFrom('queue_tickets')
      .select(['status', 'proposal_id'])
      .where('id', '=', ticketId)
      .where('account_id', '=', accountId)
      .forUpdate()
      .executeTakeFirst();
    if (!ticket) return 'not_found';
    if (ticket.status === 'waiting') {
      await trx
        .updateTable('queue_tickets')
        .set({ status: 'cancelled', updated_at: now })
        .where('id', '=', ticketId)
        .execute();
      return 'left';
    }
    if (ticket.status === 'proposed' && ticket.proposal_id) {
      const declined = await recordResponse(trx, accountId, ticket.proposal_id, false, now);
      return declined === 'recorded' || declined === 'already' ? 'declined' : 'starting';
    }
    return 'not_found';
  });
}

export type RespondResult = 'recorded' | 'already' | 'not_pending' | 'not_found';

/** Records a player's answer to a ranked accept prompt (realtime queue.respond). */
export async function respondToProposal(
  db: Db,
  accountId: string,
  proposalId: string,
  accept: boolean,
  now: Date = new Date(),
): Promise<RespondResult> {
  return db.transaction().execute((trx) => recordResponse(trx, accountId, proposalId, accept, now));
}

async function recordResponse(
  trx: Db,
  accountId: string,
  proposalId: string,
  accept: boolean,
  now: Date,
): Promise<RespondResult> {
  const proposal = await trx
    .selectFrom('match_proposals')
    .select(['status', 'expires_at'])
    .where('id', '=', proposalId)
    .forShare()
    .executeTakeFirst();
  if (!proposal) return 'not_found';
  if (proposal.status !== 'pending' || (proposal.expires_at && now >= proposal.expires_at)) {
    return 'not_pending';
  }
  const seat = await trx
    .selectFrom('match_proposal_seats')
    .select(['slot', 'response'])
    .where('proposal_id', '=', proposalId)
    .where('account_id', '=', accountId)
    .executeTakeFirst();
  if (!seat) return 'not_found';
  if (seat.response !== 'pending') return 'already';
  await trx
    .updateTable('match_proposal_seats')
    .set({ response: accept ? 'accepted' : 'declined', responded_at: now })
    .where('proposal_id', '=', proposalId)
    .where('slot', '=', seat.slot)
    .execute();
  return 'recorded';
}
