// Ticket operations behind the realtime methods queue.join, queue.leave and
// queue.respond. They run in the API (any replica) while the matchmaker runs
// on the worker leader; both sides only move rows under conditions on their
// current status, so they can interleave safely. AccessPolicy.canQueue and
// the sim-version check stay with the API handler, before joinQueue.
import { randomUUID } from 'node:crypto';
import type { Kysely } from 'kysely';
import type { ResolvedQueue } from '@glob2/core';
import type { Database } from '@glob2/db';
import { accountRating } from '../ratings/entities.ts';
import type { RegionRtt } from './grouping.ts';

type Db = Kysely<Database>;

export interface JoinQueueRequest {
  accountId: string;
  queue: ResolvedQueue;
  /**
   * Further queues of the same search (queue.join queueIds): one ticket in each,
   * sharing a search id. The first ticket to reach a match prompt holds the others
   * back (the matchmaker skips an account that is in a prompt); they close when its
   * match starts and resume, keeping their place, when it falls through.
   */
  alsoQueues?: readonly ResolvedQueue[];
  /** simVersionKey() of the client. */
  simVersion: string;
  regions: readonly RegionRtt[];
  allowAiOpponent?: boolean;
  now?: Date;
}

export type JoinQueueResult =
  | {
      ok: true;
      /** The ticket of `queue`. */
      ticketId: string;
      searchId: string;
      tickets: { queueId: string; ticketId: string }[];
      joinedAt: Date;
    }
  | { ok: false; code: 'guest_not_allowed' | 'already_queued' | 'account_inactive' }
  | { ok: false; code: 'cooldown'; until: Date };

/**
 * Starts a search: a waiting ticket in `queue` and in each of `alsoQueues`, all or
 * none. Rated queues are for registered accounts only (guests play rooms and casual
 * queues). Each ticket snapshots the account's rating on its queue's ladder for
 * grouping. An account has one search at a time: the account row is locked while
 * checking, so two joins cannot both start one.
 */
export async function joinQueue(db: Db, request: JoinQueueRequest): Promise<JoinQueueResult> {
  const now = request.now ?? new Date();
  const queues = [request.queue, ...(request.alsoQueues ?? [])].filter(
    (queue, index, all) => all.findIndex((q) => q.id === queue.id) === index,
  );
  return db.transaction().execute(async (trx): Promise<JoinQueueResult> => {
    const account = await trx
      .selectFrom('accounts')
      .select(['kind', 'status'])
      .where('id', '=', request.accountId)
      .forUpdate()
      .executeTakeFirst();
    if (!account || account.status !== 'active') return { ok: false, code: 'account_inactive' };
    if (queues.some((queue) => queue.rated) && account.kind === 'guest') {
      return { ok: false, code: 'guest_not_allowed' };
    }
    const cooldown = await trx
      .selectFrom('queue_cooldowns')
      .select('until')
      .where('account_id', '=', request.accountId)
      .where('until', '>', now)
      .executeTakeFirst();
    if (cooldown) return { ok: false, code: 'cooldown', until: cooldown.until };
    const active = await trx
      .selectFrom('queue_tickets')
      .select('id')
      .where('account_id', '=', request.accountId)
      .where('status', 'in', ['waiting', 'proposed'])
      .executeTakeFirst();
    if (active) return { ok: false, code: 'already_queued' };
    const searchId = randomUUID();
    const tickets: { queueId: string; ticketId: string }[] = [];
    for (const queue of queues) {
      const rating = await accountRating(trx, request.accountId, queue.id);
      const inserted = await trx
        .insertInto('queue_tickets')
        .values({
          queue_id: queue.id,
          account_id: request.accountId,
          search_id: searchId,
          sim_version: request.simVersion,
          region_rtts: JSON.stringify(request.regions),
          rating_mu: rating.mu,
          rating_sigma: rating.sigma,
          allow_ai_opponent: request.allowAiOpponent ?? true,
          created_at: now,
          updated_at: now,
        })
        .returning('id')
        .executeTakeFirstOrThrow();
      tickets.push({ queueId: queue.id, ticketId: inserted.id });
    }
    // `queue` is always first, so its ticket leads.
    const [first] = tickets;
    if (!first) throw new Error('a search enters at least one queue');
    return { ok: true, ticketId: first.ticketId, searchId, tickets, joinedAt: now };
  });
}

export type LeaveQueueResult = 'left' | 'declined' | 'starting' | 'not_found';

/**
 * Leaves the search the ticket belongs to: every waiting ticket of it is cancelled.
 * Leaving while in a proposal that waits for accepts counts as declining it (the
 * matchmaker applies the cooldown); once a proposal is starting, the player can no
 * longer leave through the queue, and the other tickets stay until it starts.
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
      .select('search_id')
      .where('id', '=', ticketId)
      .where('account_id', '=', accountId)
      .executeTakeFirst();
    if (!ticket) return 'not_found';
    const search = await trx
      .selectFrom('queue_tickets')
      .select(['id', 'status', 'proposal_id'])
      .where('search_id', '=', ticket.search_id)
      .where('account_id', '=', accountId)
      .forUpdate()
      .execute();
    let result: LeaveQueueResult = 'not_found';
    const proposed = search.find((t) => t.status === 'proposed' && t.proposal_id);
    if (proposed?.proposal_id) {
      const declined = await recordResponse(trx, accountId, proposed.proposal_id, false, now);
      if (declined !== 'recorded' && declined !== 'already') return 'starting';
      result = 'declined';
    }
    const waiting = search.filter((t) => t.status === 'waiting').map((t) => t.id);
    if (waiting.length > 0) {
      await trx
        .updateTable('queue_tickets')
        .set({ status: 'cancelled', updated_at: now })
        .where('id', 'in', waiting)
        .execute();
      if (result === 'not_found') result = 'left';
    }
    return result;
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

export type UpdateTicketResult = 'updated' | 'not_waiting' | 'not_found';

/**
 * Changes "Allow an AI opponent" of a search that is still waiting (or in a
 * proposal), keeping its queue positions.
 */
export async function updateTicket(
  db: Db,
  accountId: string,
  ticketId: string,
  allowAiOpponent: boolean,
  now: Date = new Date(),
): Promise<UpdateTicketResult> {
  // The whole search: one toggle for every queue it entered.
  const updated = await db
    .updateTable('queue_tickets')
    .set({ allow_ai_opponent: allowAiOpponent, updated_at: now })
    .where('search_id', '=', (eb) =>
      eb
        .selectFrom('queue_tickets as t')
        .select('t.search_id')
        .where('t.id', '=', ticketId)
        .where('t.account_id', '=', accountId),
    )
    .where('account_id', '=', accountId)
    .where('status', 'in', ['waiting', 'proposed'])
    .executeTakeFirst();
  if (updated.numUpdatedRows > 0n) return 'updated';
  const exists = await db
    .selectFrom('queue_tickets')
    .select('id')
    .where('id', '=', ticketId)
    .where('account_id', '=', accountId)
    .executeTakeFirst();
  return exists ? 'not_waiting' : 'not_found';
}
