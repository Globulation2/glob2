// Shared fixtures for match-domain tests (this package, the worker and the
// API): accounts, queue matches, verify jobs and the matchmaker test doubles.
import { randomUUID } from 'node:crypto';
import type { Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import type { VerifyVerdict } from '@glob2/protocol';
import type { MatchProposal } from '../src/matchmaking/starter.ts';
import { testQueueMatchSetup } from './doubles.ts';

export * from './doubles.ts';

export const SIM_A = `125-49-${'aa'.repeat(32)}`;
export const SIM_B = `126-49-${'bb'.repeat(32)}`;
export const HASH = 'cd'.repeat(32);

type Db = Kysely<Database>;

export async function createAccount(
  db: Db,
  name: string,
  kind: 'guest' | 'registered' = 'registered',
): Promise<string> {
  // Registered display names are unique per instance (migration 0003), and tests
  // reuse names, so a random suffix keeps them distinct.
  const displayName = kind === 'registered' ? `${name}-${randomUUID().slice(0, 8)}` : name;
  const row = await db
    .insertInto('accounts')
    .values({ kind, display_name: displayName })
    .returning('id')
    .executeTakeFirstOrThrow();
  return row.id;
}

export interface SeatSpec {
  side: number;
  accountId?: string;
  ai?: MatchProposal['seats'][number]['ai'];
  quitTick?: number;
  abandoned?: boolean;
}

/** Inserts a queue (or room) match whose seat i plays map team i on alliance `side`. */
export async function createMatch(
  db: Db,
  seats: SeatSpec[],
  options: {
    queueId?: string | null;
    rated?: boolean;
    simVersion?: string;
    finalTick?: number;
  } = {},
): Promise<string> {
  const queueId = options.queueId === undefined ? 'ranked-1v1' : options.queueId;
  const simVersion = options.simVersion ?? SIM_A;
  const proposal: MatchProposal = {
    id: randomUUID(),
    queueId: queueId ?? 'room',
    rated: options.rated ?? true,
    backfilled: false,
    simVersion,
    region: null,
    map: {
      generatorId: 'even-ground',
      revision: 2,
      params: { width: 7, height: 7, teams: seats.length },
      candidates: 5,
      startingUnitLevel: 0,
    },
    seats: seats.map((s, slot) => ({
      slot,
      side: s.side,
      kind: s.ai ? 'ai' : 'human',
      ...(s.accountId ? { accountId: s.accountId } : {}),
      ...(s.ai ? { ai: s.ai } : {}),
      ratingEntityId: null,
      mu: 25,
      sigma: 25 / 3,
    })),
  };
  const setup = testQueueMatchSetup(proposal, 7, HASH);
  const match = await db
    .insertInto('matches')
    .values({
      sim_version: simVersion,
      origin: queueId ? 'queue' : 'room',
      queue_id: queueId,
      rated: options.rated ?? true,
      status: 'ended',
      setup: JSON.stringify(setup),
      seed: 7,
      map_hash: HASH,
      final_tick: options.finalTick ?? 30_000,
      end_reason: 'completed',
    })
    .returning('id')
    .executeTakeFirstOrThrow();
  await db
    .insertInto('match_participants')
    .values(
      seats.map((s, seat) => ({
        match_id: match.id,
        seat,
        team: seat,
        kind: s.ai ? ('ai' as const) : ('human' as const),
        account_id: s.accountId ?? null,
        ai_id: s.ai ?? null,
        display_name: s.ai ? `AI ${s.ai}` : `Player ${seat}`,
        quit_tick: s.quitTick ?? null,
        outcome: s.abandoned ? ('abandoned' as const) : null,
      })),
    )
    .execute();
  return match.id;
}

/** Records a queued verify-match engine job for a match and returns its id. */
export async function createVerifyJob(db: Db, matchId: string): Promise<string> {
  const match = await db
    .selectFrom('matches')
    .select(['setup', 'sim_version'])
    .where('id', '=', matchId)
    .executeTakeFirstOrThrow();
  const job = await db
    .insertInto('engine_jobs')
    .values({
      kind: 'verify-match',
      sim_version: match.sim_version,
      payload: JSON.stringify({ matchId, setup: match.setup, recordHash: HASH }),
    })
    .returning('id')
    .executeTakeFirstOrThrow();
  return job.id;
}

/** A verified verdict where each listed team has the given outcome. */
export function verified(
  outcomes: ('won' | 'lost' | 'unresolved')[],
  finalTick = 30_000,
): VerifyVerdict {
  return {
    verdict: 'verified',
    outcome: {
      finalTick,
      teams: outcomes.map((outcome, team) => ({ team, outcome, prestige: 0 })),
      resultHash: HASH,
      replayHash: HASH,
    },
  };
}

export function resultPayload(jobId: string, verdict: VerifyVerdict): unknown {
  return { jobId, kind: 'verify-match', ok: true, result: verdict, agent: 'agent-test' };
}

export async function ratingOf(db: Db, accountId: string, ladder = 'ranked-1v1') {
  return db
    .selectFrom('ratings')
    .innerJoin('rating_entities', 'rating_entities.id', 'ratings.entity_id')
    .select(['ratings.mu', 'ratings.sigma', 'ratings.games', 'ratings.wins'])
    .where('rating_entities.account_id', '=', accountId)
    .where('ratings.ladder', '=', ladder)
    .executeTakeFirst();
}

export async function waitFor(
  condition: () => Promise<boolean> | boolean,
  timeoutMs = 10_000,
): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (!(await condition())) {
    if (Date.now() > deadline) throw new Error('timed out waiting for condition');
    await new Promise((resolve) => setTimeout(resolve, 25));
  }
}
