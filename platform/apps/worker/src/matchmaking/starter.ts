// The step that turns an accepted proposal into a running match. Rooms,
// relay allocation, map warm pools and match tickets arrive with M4, so the
// matchmaker only knows this interface; InMemoryMatchStarter is the test double.
import { randomUUID } from 'node:crypto';
import type { Kysely } from 'kysely';
import type { MapPoolEntry } from '@glob2/core';
import type { Database } from '@glob2/db';
import { STANDARD_RULES, type MatchSetup, parseSimVersionKey, playerSeats } from '@glob2/protocol';
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
 * Starts a match for a proposal. Implementations (M4/M6) must:
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

/**
 * Test double: records proposals and returns one match id per proposal. With
 * a database it also writes the matches/match_participants rows the real
 * starter would (placeholder map hash and seed), following the contract above.
 */
export class InMemoryMatchStarter implements MatchStarter {
  readonly started = new Map<string, StartedMatch>();
  readonly calls: MatchProposal[] = [];
  /** Number of upcoming start() calls that throw. */
  failNext = 0;
  private readonly db: Kysely<Database> | undefined;

  constructor(db?: Kysely<Database>) {
    this.db = db;
  }

  async start(proposal: MatchProposal): Promise<StartedMatch> {
    this.calls.push(proposal);
    if (this.failNext > 0) {
      this.failNext--;
      throw new Error('no relay available');
    }
    const existing = this.started.get(proposal.id);
    if (existing) return existing;
    const match: StartedMatch = { matchId: randomUUID() };
    if (this.db) {
      const found = await this.db
        .selectFrom('matches')
        .select('id')
        .where('proposal_id', '=', proposal.id)
        .executeTakeFirst();
      if (found) match.matchId = found.id;
      else await insertQueueMatch(this.db, proposal, match.matchId);
    }
    this.started.set(proposal.id, match);
    return match;
  }
}

/** The MatchSetup a starter would build for a proposal (seed and map hash given). */
export function proposalSetup(proposal: MatchProposal, seed: number, mapHash: string): MatchSetup {
  const simVersion = parseSimVersionKey(proposal.simVersion);
  if (!simVersion) throw new Error(`bad sim version ${proposal.simVersion}`);
  const seats = [...proposal.seats].sort((a, b) => a.slot - b.slot);
  return {
    schemaVersion: 1,
    simVersion,
    seed,
    map: { kind: 'generated', generator: { ...proposal.map, seed }, hash: mapHash },
    teams: seats.map((s) => ({ team: s.slot, alliance: s.side })),
    seats: seats.map((s) =>
      s.kind === 'human' || !s.ai
        ? {
            seat: s.slot,
            kind: 'human',
            team: s.slot,
            name: `Player ${s.slot + 1}`,
            ...(s.accountId ? { accountId: s.accountId } : {}),
          }
        : { seat: s.slot, kind: 'ai', team: s.slot, name: `AI ${s.ai}`, ai: s.ai },
    ),
    rules: STANDARD_RULES,
    experiments: [],
  };
}

async function insertQueueMatch(
  db: Kysely<Database>,
  proposal: MatchProposal,
  matchId: string,
): Promise<void> {
  const seed = 12345;
  const mapHash = '0'.repeat(64);
  const setup = proposalSetup(proposal, seed, mapHash);
  await db.transaction().execute(async (trx) => {
    await trx
      .insertInto('matches')
      .values({
        id: matchId,
        sim_version: proposal.simVersion,
        origin: 'queue',
        queue_id: proposal.queueId,
        rated: proposal.rated,
        status: 'starting',
        setup: JSON.stringify(setup),
        seed,
        map_hash: mapHash,
        proposal_id: proposal.id,
      })
      .execute();
    await trx
      .insertInto('match_participants')
      .values(
        playerSeats(setup).map((seat) => ({
          match_id: matchId,
          seat: seat.seat,
          team: seat.team,
          kind: seat.kind,
          account_id: seat.kind === 'human' ? (seat.accountId ?? null) : null,
          ai_id: seat.kind === 'ai' ? seat.ai : null,
          rating_entity_id:
            proposal.seats.find((s) => s.slot === seat.seat)?.ratingEntityId ?? null,
          display_name: seat.name,
        })),
      )
      .execute();
  });
}
