// Rating entities (an account, or an AI at one sim version) and their ladder
// ratings. AI entities are keyed by (AI id, sim version): a new sim version
// gets fresh entities seeded from docs/ai/ratings.md and never inherits what
// an older revision of the same AI earned.
import type { Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import { simVersionKey, type AiId, type SimVersion } from '@glob2/protocol';
import { catalogRulesVersion } from '@glob2/protocol/node';
import { DEFAULT_RATING, aiSeedRating } from './scale.ts';

type Db = Kysely<Database>;
export type RatedAi = Exclude<AiId, 'none'>;

/** Current default-catalog rating identities; executable routing stays unchanged. */
export async function currentCatalogRulesVersions(
  db: Db,
  versions: readonly SimVersion[],
): Promise<SimVersion[]> {
  if (!versions.length) return [];
  const agents = await db
    .selectFrom('engine_agents')
    .select(['sim_version', 'building_catalog_hash'])
    .where('sim_version', 'in', versions.map(simVersionKey))
    .where('building_catalog_hash', 'is not', null)
    .orderBy('last_seen_at', 'desc')
    .execute();
  return versions.map((version) =>
    catalogRulesVersion(
      version,
      agents.find((agent) => agent.sim_version === simVersionKey(version))?.building_catalog_hash ??
        undefined,
    ),
  );
}

export async function ensureAccountEntity(db: Db, accountId: string): Promise<string> {
  await db
    .insertInto('rating_entities')
    .values({ kind: 'account', account_id: accountId })
    .onConflict((oc) => oc.column('account_id').doNothing())
    .execute();
  const row = await db
    .selectFrom('rating_entities')
    .select('id')
    .where('account_id', '=', accountId)
    .executeTakeFirstOrThrow();
  return row.id;
}

export async function ensureAiEntity(db: Db, ai: RatedAi, simVersion: string): Promise<string> {
  await db
    .insertInto('rating_entities')
    .values({ kind: 'ai', ai_id: ai, ai_sim_version: simVersion })
    .onConflict((oc) => oc.columns(['ai_id', 'ai_sim_version']).doNothing())
    .execute();
  const row = await db
    .selectFrom('rating_entities')
    .select('id')
    .where('ai_id', '=', ai)
    .where('ai_sim_version', '=', simVersion)
    .executeTakeFirstOrThrow();
  return row.id;
}

export interface LadderRating {
  entityId: string;
  ladder: string;
  mu: number;
  sigma: number;
  games: number;
  wins: number;
}

/**
 * The entity's rating on a ladder, created from its prior when missing: the
 * default prior for accounts, the documented seed for AIs.
 */
export async function ensureRating(
  db: Db,
  entityId: string,
  ladder: string,
  ai?: RatedAi,
): Promise<LadderRating> {
  const seed = ai ? aiSeedRating(ai) : { ...DEFAULT_RATING, source: null };
  await db
    .insertInto('ratings')
    .values({
      entity_id: entityId,
      ladder,
      mu: seed.mu,
      sigma: seed.sigma,
      seed_source: seed.source,
    })
    .onConflict((oc) => oc.columns(['entity_id', 'ladder']).doNothing())
    .execute();
  const row = await db
    .selectFrom('ratings')
    .select(['entity_id', 'ladder', 'mu', 'sigma', 'games', 'wins'])
    .where('entity_id', '=', entityId)
    .where('ladder', '=', ladder)
    .executeTakeFirstOrThrow();
  return {
    entityId: row.entity_id,
    ladder: row.ladder,
    mu: row.mu,
    sigma: row.sigma,
    games: row.games,
    wins: row.wins,
  };
}

/** An account's current rating on a ladder without creating anything (default prior if unrated). */
export async function accountRating(
  db: Db,
  accountId: string,
  ladder: string,
): Promise<{ mu: number; sigma: number }> {
  const row = await db
    .selectFrom('ratings')
    .innerJoin('rating_entities', 'rating_entities.id', 'ratings.entity_id')
    .select(['ratings.mu', 'ratings.sigma'])
    .where('rating_entities.account_id', '=', accountId)
    .where('ratings.ladder', '=', ladder)
    .executeTakeFirst();
  return row ? { mu: row.mu, sigma: row.sigma } : { ...DEFAULT_RATING };
}

export interface AiLadderRating extends LadderRating {
  ai: RatedAi;
}

/** Ensures and returns the ladder ratings of the given AIs at one sim version. */
export async function aiLadderRatings(
  db: Db,
  ais: readonly RatedAi[],
  simVersion: string,
  ladder: string,
): Promise<AiLadderRating[]> {
  const result: AiLadderRating[] = [];
  for (const ai of ais) {
    const entityId = await ensureAiEntity(db, ai, simVersion);
    result.push({ ai, ...(await ensureRating(db, entityId, ladder, ai)) });
  }
  return result;
}
