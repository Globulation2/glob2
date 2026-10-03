// The step that turns an accepted proposal into a running match. The
// matchmaker only knows this interface. The production implementation is
// PlatformMatchStarter (play/start.ts), which builds the MatchSetup with
// queueMatchSetup(); the test double InMemoryMatchStarter lives in
// `@glob2/play/testing`.
import type { MapPoolEntry } from '@glob2/core';
import type { RatedAi } from '../ratings/entities.ts';

export interface ProposalSeat {
  /** 0..n-1; side 0's seats first. Use it as the MatchSetup seat number. */
  slot: number;
  /** Side (alliance) 0 or 1. */
  side: number;
  kind: 'human' | 'ai';
  ticketId?: string;
  accountId?: string;
  ai?: RatedAi;
  /** Rating entity for match_participants.rating_entity_id. */
  ratingEntityId: string | null;
  /** Rating snapshot the group was formed with. */
  mu: number;
  sigma: number;
}

export interface MatchProposal {
  id: string;
  queueId: string;
  rated: boolean;
  /** AI seats were added after the backfill delay. */
  backfilled: boolean;
  /** simVersionKey() of every member. */
  simVersion: string;
  /** Relay region minimising the worst member round trip; null = any region. */
  region: string | null;
  /** Map pool entry; the starter picks the seed (or a warm pre-generated map). */
  map: MapPoolEntry;
  seats: ProposalSeat[];
}

export interface StartedMatch {
  matchId: string;
}

/**
 * Starts a match for a proposal. Implementations must:
 * - create the `matches` row (origin 'queue', queue_id, rated, proposal_id =
 *   proposal.id) and one `match_participants` row per seat, seat = slot,
 *   each seat on its own map team with teams[team].alliance = side, and
 *   rating_entity_id copied from the seat;
 * - generate or take a warm map for `map`, allocate a relay in `region`, and
 *   push match.start tickets to the humans;
 * - be idempotent per proposal id: after a leader failover the matchmaker may
 *   call start() again for a proposal it already started, and must get the
 *   same match back (look it up by matches.proposal_id);
 * - throw when the match cannot start; the matchmaker retries a few ticks,
 *   then requeues the players.
 */
export interface MatchStarter {
  start(proposal: MatchProposal): Promise<StartedMatch>;
}
