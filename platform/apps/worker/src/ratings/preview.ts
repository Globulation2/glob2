// The rating preview a player sees before a rated match is verified: their
// displayed rating now, and what it would be if their side won or lost. It
// uses the same model and side rules as applyMatchRatings, from the ratings
// as they are when the match starts, and writes nothing (entities that do not
// exist yet are read as their priors).
import type { Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import type { MatchRatingPreview, MatchSetup } from '@glob2/protocol';
import type { Rating } from 'openskill';
import { DEFAULT_RATING, aiSeedRating, displayRating, isProvisional, rateSides } from './scale.ts';
import type { RatedAi } from './entities.ts';

type Db = Kysely<Database>;

/** Ratings of every seat of a match on one ladder, without creating anything. */
async function seatRatings(
  db: Db,
  setup: MatchSetup,
  simVersion: string,
  ladder: string,
): Promise<Map<number, Rating>> {
  const accounts = setup.seats.flatMap((s) =>
    s.kind === 'human' && s.accountId ? [s.accountId] : [],
  );
  const ais = setup.seats.flatMap((s) => (s.kind === 'ai' && s.ai !== 'none' ? [s.ai] : []));
  const accountRows =
    accounts.length === 0
      ? []
      : await db
          .selectFrom('ratings')
          .innerJoin('rating_entities', 'rating_entities.id', 'ratings.entity_id')
          .select(['rating_entities.account_id', 'ratings.mu', 'ratings.sigma'])
          .where('ratings.ladder', '=', ladder)
          .where('rating_entities.account_id', 'in', accounts)
          .execute();
  const aiRows =
    ais.length === 0
      ? []
      : await db
          .selectFrom('ratings')
          .innerJoin('rating_entities', 'rating_entities.id', 'ratings.entity_id')
          .select(['rating_entities.ai_id', 'ratings.mu', 'ratings.sigma'])
          .where('ratings.ladder', '=', ladder)
          .where('rating_entities.ai_sim_version', '=', simVersion)
          .where('rating_entities.ai_id', 'in', ais)
          .execute();
  const byAccount = new Map(accountRows.map((r) => [r.account_id, { mu: r.mu, sigma: r.sigma }]));
  const byAi = new Map(aiRows.map((r) => [r.ai_id, { mu: r.mu, sigma: r.sigma }]));
  const ratings = new Map<number, Rating>();
  for (const seat of setup.seats) {
    if (seat.kind === 'human') {
      ratings.set(seat.seat, (seat.accountId && byAccount.get(seat.accountId)) || DEFAULT_RATING);
    } else if (seat.ai !== 'none') {
      const seed = aiSeedRating(seat.ai as RatedAi);
      ratings.set(seat.seat, byAi.get(seat.ai) ?? { mu: seed.mu, sigma: seed.sigma });
    }
  }
  return ratings;
}

/**
 * The preview for `accountId` in a rated queue match, or undefined when the
 * match is not rated, the account has no seat, or the match does not have
 * exactly two sides (such matches change no rating).
 */
export async function matchRatingPreview(
  db: Db,
  matchId: string,
  accountId: string,
): Promise<MatchRatingPreview | undefined> {
  const match = await db
    .selectFrom('matches')
    .select(['rated', 'origin', 'queue_id', 'setup', 'sim_version'])
    .where('id', '=', matchId)
    .executeTakeFirst();
  if (!match || !match.rated || match.origin !== 'queue' || !match.queue_id) return undefined;
  const setup = match.setup as unknown as MatchSetup;
  const mine = setup.seats.find((s) => s.kind === 'human' && s.accountId === accountId);
  if (!mine) return undefined;
  const ratings = await seatRatings(db, setup, match.sim_version, match.queue_id);
  return previewFor(setup, mine.seat, ratings, match.queue_id);
}

/** Pure part of matchRatingPreview: the preview for one seat given every seat's rating. */
export function previewFor(
  setup: MatchSetup,
  seat: number,
  ratings: ReadonlyMap<number, Rating>,
  ladder: string,
): MatchRatingPreview | undefined {
  const allianceOf = new Map(setup.teams.map((t) => [t.team, t.alliance]));
  const rated = setup.seats.filter((s) => ratings.has(s.seat));
  const sides = [...new Set(rated.map((s) => allianceOf.get(s.team)))].sort(
    (a, b) => (a ?? 0) - (b ?? 0),
  );
  if (sides.length !== 2 || sides.includes(undefined)) return undefined;
  const mine = rated.find((s) => s.seat === seat);
  const before = ratings.get(seat);
  if (!mine || !before) return undefined;
  const mySide = sides.indexOf(allianceOf.get(mine.team));
  const groups = sides.map((side) => rated.filter((s) => allianceOf.get(s.team) === side));
  const position = groups[mySide]!.findIndex((s) => s.seat === seat);
  const after = (myRank: number) => {
    const ranks = sides.map((_, i) => (i === mySide ? myRank : 3 - myRank));
    const result = rateSides(
      groups.map((g) => g.map((s) => ratings.get(s.seat)!)),
      ranks,
    );
    return displayRating(result[mySide]![position]!);
  };
  return {
    ladder,
    before: displayRating(before),
    ifWon: after(1),
    ifLost: after(2),
    provisional: isProvisional(before),
  };
}
