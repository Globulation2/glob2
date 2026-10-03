// Test doubles for the matchmaker's collaborators. They are exported from
// `@glob2/play/testing` only, never from the production package index.
import { randomUUID } from 'node:crypto';
import type { Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import { playerSeats, type MatchSetup, type RealtimeEventData } from '@glob2/protocol';
import type {
  QueueEventName,
  QueueNotification,
  QueueNotifier,
} from '../src/matchmaking/notifier.ts';
import type { MatchProposal, MatchStarter, StartedMatch } from '../src/matchmaking/starter.ts';
import { queueMatchSetup } from '../src/play/start.ts';

/** Placeholder map of the test double: all-zero hash, fixed seed. */
export const PLACEHOLDER_SEED = 12345;
export const PLACEHOLDER_MAP_HASH = '0'.repeat(64);

/** The setup PlatformMatchStarter would build, with a fixed seed and map hash. */
export function testQueueMatchSetup(
  proposal: MatchProposal,
  seed = PLACEHOLDER_SEED,
  mapHash = PLACEHOLDER_MAP_HASH,
): MatchSetup {
  return queueMatchSetup(proposal, {
    seed,
    generator: { ...proposal.map, seed },
    mapHash,
  });
}

/**
 * Records proposals and returns one match id per proposal. With a database it
 * also writes the matches/match_participants rows the real starter would
 * (through the production setup builder, with a placeholder map), following
 * the MatchStarter contract.
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

async function insertQueueMatch(
  db: Kysely<Database>,
  proposal: MatchProposal,
  matchId: string,
): Promise<void> {
  const setup = testQueueMatchSetup(proposal);
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
        seed: setup.seed,
        map_hash: PLACEHOLDER_MAP_HASH,
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

/** Keeps every queue event in order instead of NOTIFYing. */
export class RecordingQueueNotifier implements QueueNotifier {
  readonly events: QueueNotification[] = [];

  async send<E extends QueueEventName>(
    _db: Kysely<Database>,
    accountId: string,
    event: E,
    data: RealtimeEventData<E>,
  ): Promise<void> {
    this.events.push({ accountId, event, data } as QueueNotification);
  }

  of<E extends QueueEventName>(event: E, accountId?: string): QueueNotification<E>[] {
    return this.events.filter(
      (e): e is QueueNotification<E> =>
        e.event === event && (accountId === undefined || e.accountId === accountId),
    );
  }

  clear(): void {
    this.events.length = 0;
  }
}
