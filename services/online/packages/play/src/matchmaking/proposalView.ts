// What a client shows for a queue proposal: the map, the region and every
// seat with its name, rating and accept answer. Built from the stored
// proposal, so the matchmaker (new proposals) and the API (after a
// queue.respond, so everyone sees who has accepted) send the same event.
import type { Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import type { AiId, ProposalSeat, RealtimeEventData } from '@glob2/protocol';
import { aiDisplayName } from '../play/start.ts';
import { readMapPoolEntry } from '../stored.ts';
import { displayRating, isProvisional } from '../ratings/scale.ts';
import type { QueueNotifier } from './notifier.ts';

type Db = Kysely<Database>;

/** Map size in tiles from a pool entry's log2 width/height parameters. */
function side(log2: unknown): number | undefined {
  return typeof log2 === 'number' && Number.isInteger(log2) && log2 >= 5 && log2 <= 10
    ? 2 ** log2
    : undefined;
}

export interface ProposalView {
  base: Omit<RealtimeEventData<'queue.proposal'>, 'ticketId' | 'seats'>;
  seats: (ProposalSeat & { accountId?: string; ticketId?: string })[];
}

/** Loads a proposal as the queue.proposal event shows it, or undefined when it is gone. */
export async function loadProposalView(
  db: Db,
  proposalId: string,
): Promise<ProposalView | undefined> {
  const proposal = await db
    .selectFrom('match_proposals')
    .selectAll()
    .where('id', '=', proposalId)
    .executeTakeFirst();
  if (!proposal) return undefined;
  const rows = await db
    .selectFrom('match_proposal_seats')
    .leftJoin('accounts', 'accounts.id', 'match_proposal_seats.account_id')
    .select([
      'match_proposal_seats.slot',
      'match_proposal_seats.side',
      'match_proposal_seats.kind',
      'match_proposal_seats.ticket_id',
      'match_proposal_seats.account_id',
      'match_proposal_seats.ai_id',
      'match_proposal_seats.mu',
      'match_proposal_seats.sigma',
      'match_proposal_seats.response',
      'accounts.display_name',
    ])
    .where('match_proposal_seats.proposal_id', '=', proposalId)
    .orderBy('match_proposal_seats.slot')
    .execute();
  const map = readMapPoolEntry(proposal.map);
  const width = side(map.params?.width);
  const height = side(map.params?.height);
  const humans = rows.filter((r) => r.kind === 'human').length;
  const requiresAccept = rows.some((r) => r.response !== 'not_required');
  return {
    base: {
      proposalId: proposal.id,
      queueId: proposal.queue_id,
      requiresAccept,
      ...(proposal.expires_at ? { expiresAt: proposal.expires_at.toISOString() } : {}),
      humans,
      ais: rows.length - humans,
      rated: proposal.rated,
      backfilled: proposal.backfilled,
      ...(proposal.region ? { region: proposal.region } : {}),
      map: {
        generatorId: map.generatorId,
        ...(width ? { width } : {}),
        ...(height ? { height } : {}),
      },
    },
    seats: rows.map((r) => {
      const rating = { mu: r.mu, sigma: r.sigma };
      const ai = r.kind === 'ai' && r.ai_id ? (r.ai_id as AiId) : undefined;
      return {
        slot: r.slot,
        side: r.side,
        kind: r.kind,
        displayName: ai ? aiDisplayName(ai) : (r.display_name ?? `Player ${r.slot + 1}`),
        ...(ai ? { ai } : {}),
        rating: Math.round(displayRating(rating)),
        provisional: isProvisional(rating),
        response: r.response,
        ...(r.account_id ? { accountId: r.account_id } : {}),
        ...(r.ticket_id ? { ticketId: r.ticket_id } : {}),
      };
    }),
  };
}

/** queue.proposal for one human seat of the view. */
export function proposalEventFor(
  view: ProposalView,
  ticketId: string,
): RealtimeEventData<'queue.proposal'> {
  return {
    ...view.base,
    ticketId,
    seats: view.seats.map(({ accountId: _a, ticketId: seatTicket, ...seat }) => ({
      ...seat,
      ...(seatTicket === ticketId ? { you: true } : {}),
    })),
  };
}

/** Sends the proposal to every human in it (inside `db`'s transaction when it is one). */
export async function sendProposal(
  db: Db,
  notifier: QueueNotifier,
  proposalId: string,
): Promise<void> {
  const view = await loadProposalView(db, proposalId);
  if (!view) return;
  for (const seat of view.seats) {
    if (seat.kind !== 'human' || !seat.accountId || !seat.ticketId) continue;
    await notifier.send(
      db,
      seat.accountId,
      'queue.proposal',
      proposalEventFor(view, seat.ticketId),
    );
  }
}
