// Shared public participant reads. AI identities always include the simulation revision.
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import {
  parseSimVersionKey,
  simVersionKey,
  type AiId,
  type SimVersion,
  type PlayerDirectory,
  type AiProfile,
  type MatchList,
} from '@glob2/protocol';
import { currentCatalogRulesVersions, displayRating, PROVISIONAL_SIGMA } from '@glob2/play';
import { avatarUrl } from '../avatars/urls.ts';
import { overallRanks } from './rankings.ts';
import { apiError } from '../errors.ts';
import {
  catalogTitles,
  generatorLabel,
  summarize,
  decodeCursor,
  encodeCursor,
  MATCH_TIME,
  type MatchRow,
} from './summaries.ts';

function present<T>(value: T | null | undefined): T {
  if (value === null || value === undefined) throw new Error('Incomplete public player row');
  return value;
}
export type ParticipantFilter = 'all' | 'humans' | 'ai';
export function participantFilter(
  value: string | undefined,
  fallback: ParticipantFilter = 'all',
): ParticipantFilter {
  if (value === undefined) return fallback;
  if (value === 'all' || value === 'humans' || value === 'ai') return value;
  throw apiError('bad_request', 'participants must be all, humans or ai.');
}
export const AI_NAMES: Record<Exclude<AiId, 'none'>, string> = {
  maxima: 'Maxima',
  cabino: 'Cabino',
  nicowar: 'Nicowar',
  cortex: 'Cortex',
  warrush: 'Warrush',
  econo: 'Econo',
  castor: 'Castor',
  numbi: 'Numbi',
};
export function newestVersions(versions: SimVersion[]): SimVersion[] {
  return [...versions].sort(
    (a, b) =>
      b.versionMinor - a.versionMinor ||
      b.netProtocol - a.netProtocol ||
      a.dataHash.localeCompare(b.dataHash),
  );
}
type Db = Kysely<Database>;

interface DirectoryPosition {
  relevance: number;
  name: string;
  kind: 'account' | 'ai';
  id: string;
}

/** Bound each branch before combining; seek by the last row instead of scanning old pages. */
export function directoryQuery(
  q: string,
  options: { participants: ParticipantFilter; limit: number },
  ais: { id: string; name: string }[],
  after?: DirectoryPosition,
) {
  const pattern = q.replace(/[\\%_]/g, '\\$&');
  const relevance = (name: ReturnType<typeof sql>) =>
    q
      ? sql`CASE WHEN lower(${name}) = ${q} THEN 0 WHEN lower(${name}) LIKE ${pattern + '%'} THEN 1 ELSE 2 END`
      : sql`0`;
  const humanRelevance = relevance(sql`display_name`);
  const aiRelevance = relevance(sql`name`);
  const seek = (name: ReturnType<typeof sql>, kind: string, id: ReturnType<typeof sql>) => {
    if (!after) return sql`TRUE`;
    return q
      ? sql`(${relevance(name)}, lower(${name}), ${kind}::text, ${id}::text) >
            (${after.relevance}, ${after.name}, ${after.kind}, ${after.id})`
      : sql`lower(${name}) >= ${after.name} AND
            (lower(${name}), ${kind}::text, ${id}::text) > (${after.name}, ${after.kind}, ${after.id})`;
  };
  return sql<{
    id: string;
    name: string;
    sort_name: string;
    relevance: number;
    kind: 'account' | 'ai';
    created_at: Date | null;
    revision: number;
  }>`SELECT * FROM (
      (SELECT id::text, display_name AS name, lower(display_name) AS sort_name,
        ${humanRelevance} AS relevance, 'account' AS kind, created_at, avatar_revision AS revision
      FROM accounts WHERE kind = 'registered' AND status = 'active' AND ${options.participants !== 'ai'}
        AND lower(display_name) LIKE ${'%' + pattern + '%'}
        AND ${seek(sql`display_name`, 'account', sql`id`)}
      ORDER BY ${q ? sql`${humanRelevance},` : sql``} lower(display_name), accounts.id
      LIMIT ${options.limit + 1})
      UNION ALL
      SELECT id, name, lower(name), ${aiRelevance}, 'ai', NULL::timestamptz, 0
      FROM jsonb_to_recordset(${JSON.stringify(ais)}::jsonb) AS ai(id text, name text)
      WHERE ${options.participants !== 'humans'} AND lower(name) LIKE ${'%' + pattern + '%'}
        AND ${seek(sql`name`, 'ai', sql`id`)}
    ) found ORDER BY relevance, sort_name, kind, id LIMIT ${options.limit + 1}`;
}

export class PublicPlayers {
  private db: Db;
  private queues: ReadonlyMap<string, string>;
  private configuredAis: readonly string[];
  private currentVersions: () => Promise<SimVersion[]>;
  constructor(
    db: Db,
    queues: ReadonlyMap<string, string>,
    configuredAis: readonly string[],
    currentVersions: () => Promise<SimVersion[]>,
  ) {
    this.db = db;
    this.queues = queues;
    this.configuredAis = configuredAis;
    this.currentVersions = async () => currentCatalogRulesVersions(db, await currentVersions());
  }

  private async orderedCurrentVersions(): Promise<SimVersion[]> {
    const versions = await this.currentVersions();
    if (versions.length < 2) return versions;
    // Hashes encode simulation revisions but cannot tell us their chronology. For
    // equal protocol/minor numbers, prefer the newest introduction among agent records.
    const starts = await this.db
      .selectFrom('engine_agents')
      .select(['sim_version', sql<Date>`min(started_at)`.as('introduced_at')])
      .where('sim_version', 'in', versions.map(simVersionKey))
      .groupBy('sim_version')
      .execute();
    const introduced = new Map(starts.map((r) => [r.sim_version, r.introduced_at.getTime()]));
    return [...versions].sort(
      (a, b) =>
        b.versionMinor - a.versionMinor ||
        b.netProtocol - a.netProtocol ||
        (introduced.get(simVersionKey(b)) ?? 0) - (introduced.get(simVersionKey(a)) ?? 0) ||
        simVersionKey(a).localeCompare(simVersionKey(b)),
    );
  }

  async directory(options: {
    q: string;
    participants: ParticipantFilter;
    cursor?: string;
    limit: number;
  }): Promise<PlayerDirectory> {
    if (typeof options.q !== 'string') throw apiError('bad_request', 'Search must be text.');
    const q = options.q.trim().toLowerCase();
    if (q.length > 64) throw apiError('bad_request', 'Search must be at most 64 characters.');
    let after: DirectoryPosition | undefined;
    if (options.cursor) {
      try {
        if (options.cursor.length > 2048) throw new Error();
        const c = JSON.parse(Buffer.from(options.cursor, 'base64url').toString()) as {
          q: string;
          participants: string;
          after: DirectoryPosition;
        };
        if (
          c.q !== q ||
          c.participants !== options.participants ||
          !c.after ||
          !Number.isInteger(c.after.relevance) ||
          c.after.relevance < 0 ||
          c.after.relevance > 2 ||
          typeof c.after.name !== 'string' ||
          c.after.name.length > 128 ||
          !['account', 'ai'].includes(c.after.kind) ||
          typeof c.after.id !== 'string' ||
          c.after.id.length > 64
        )
          throw new Error();
        after = c.after;
      } catch {
        throw apiError('bad_request', 'Invalid cursor.');
      }
    }
    const names = this.configuredAis.filter((ai): ai is keyof typeof AI_NAMES =>
      Object.hasOwn(AI_NAMES, ai),
    );
    const ais = names.map((ai) => ({ id: ai, name: AI_NAMES[ai] }));
    const result = await directoryQuery(q, options, ais, after).execute(this.db);
    const current = (await this.orderedCurrentVersions())[0];
    const page = result.rows.slice(0, options.limit);
    const last = page.at(-1);
    return {
      items: page.map((row) =>
        row.kind === 'account'
          ? {
              kind: 'account',
              account: {
                id: row.id,
                displayName: row.name,
                kind: 'registered',
                createdAt: present(row.created_at).toISOString(),
                avatarUrl: avatarUrl(row.id, row.revision),
              },
            }
          : {
              kind: 'ai',
              ai: row.id as AiId,
              displayName: row.name,
              ...(current ? { simVersion: current } : {}),
            },
      ),
      ...(result.rows.length > options.limit && last
        ? {
            nextCursor: Buffer.from(
              JSON.stringify({
                q,
                participants: options.participants,
                after: {
                  relevance: last.relevance,
                  name: last.sort_name,
                  kind: last.kind,
                  id: last.id,
                },
              }),
            ).toString('base64url'),
          }
        : {}),
    };
  }

  async versions(ai: string, requested?: string) {
    if (!Object.hasOwn(AI_NAMES, ai)) throw apiError('not_found', 'No such AI.');
    const stored = await this.db
      .selectFrom('rating_entities')
      .select('ai_sim_version')
      .where('ai_id', '=', ai)
      .execute();
    const played = await this.db
      .selectFrom('match_participants as p')
      .innerJoin('matches as m', 'm.id', 'p.match_id')
      .select(sql<string>`coalesce(m.rules_identity, m.sim_version)`.as('sim_version'))
      .distinct()
      .where('p.kind', '=', 'ai')
      .where('p.ai_id', '=', ai)
      .where('m.origin', '=', 'queue')
      .execute();
    const current = await this.orderedCurrentVersions();
    if (!this.configuredAis.includes(ai) && !stored.length && !played.length)
      throw apiError('not_found', 'No such AI.');
    const active = new Set(current.map(simVersionKey));
    const keys = new Set([
      ...active,
      ...stored.flatMap((r) => (r.ai_sim_version ? [r.ai_sim_version] : [])),
      ...played.map((r) => r.sim_version),
    ]);
    const versions = newestVersions(
      [...keys].flatMap((k) => {
        const v = parseSimVersionKey(k);
        return v ? [v] : [];
      }),
    );
    const selected =
      requested ??
      (current[0]
        ? simVersionKey(current[0])
        : versions[0]
          ? simVersionKey(versions[0])
          : undefined);
    if (requested && !keys.has(requested)) throw apiError('not_found', 'No such AI version.');
    return {
      selected,
      versions: versions.map((simVersion) => ({
        simVersion,
        current: active.has(simVersionKey(simVersion)),
      })),
      active: [...active],
    };
  }

  async matches(
    ai: string,
    version: string | undefined,
    options: { limit: number; cursor?: string; queue?: string },
  ): Promise<MatchList> {
    const { selected } = await this.versions(ai, version);
    if (!selected) return { items: [] };
    let query = this.db
      .selectFrom('matches as m')
      .selectAll('m')
      .where('m.origin', '=', 'queue')
      .where(sql<string>`coalesce(m.rules_identity, m.sim_version)`, '=', selected)
      .where('m.status', '!=', 'cancelled')
      .where((eb) =>
        eb.exists(
          eb
            .selectFrom('match_participants as p')
            .select('p.seat')
            .whereRef('p.match_id', '=', 'm.id')
            .where('p.kind', '=', 'ai')
            .where('p.ai_id', '=', ai),
        ),
      );
    if (options.queue) query = query.where('m.queue_id', '=', options.queue);
    const cursor = decodeCursor(options.cursor);
    if (cursor)
      query = query.where(sql<boolean>`(${MATCH_TIME}, m.id) < (${cursor.at}, ${cursor.id})`);
    const rows = await query
      .orderBy(MATCH_TIME, 'desc')
      .orderBy('m.id', 'desc')
      .limit(options.limit + 1)
      .execute();
    const page = rows.slice(0, options.limit);
    const last = page.at(-1);
    return {
      items: await summarize(this.db, page as MatchRow[], this.queues),
      ...(rows.length > options.limit && last
        ? {
            nextCursor: encodeCursor({
              at: last.ended_at ?? last.started_at ?? last.created_at,
              id: last.id,
            }),
          }
        : {}),
    };
  }

  async profile(ai: string, requested?: string): Promise<AiProfile> {
    const { selected, versions, active } = await this.versions(ai, requested);
    const base: AiProfile = {
      ai: ai as AiId,
      displayName: AI_NAMES[ai as keyof typeof AI_NAMES],
      versions,
      ratings: [],
      ratingHistory: [],
      recentMatches: [],
      aggregates: { windowDays: 90, games: 0, wins: 0, losses: 0, winRates: [] },
    };
    if (!selected) return base;
    base.simVersion = present(parseSimVersionKey(selected));
    const entity = await this.db
      .selectFrom('rating_entities')
      .select('id')
      .where('ai_id', '=', ai)
      .where('ai_sim_version', '=', selected)
      .executeTakeFirst();
    if (entity) {
      const ratings = await this.db
        .selectFrom('ratings')
        .selectAll()
        .where('entity_id', '=', entity.id)
        .orderBy('ladder')
        .execute();
      const ranks = await overallRanks(
        this.db,
        ratings.map((r) => r.ladder),
        active,
        { entityId: entity.id },
      );
      base.ratings = ratings.map((r) => {
        const overallRank = ranks.get(r.ladder);
        return {
          ladder: r.ladder,
          rating: Math.round(displayRating(r) * 10) / 10,
          mu: r.mu,
          sigma: r.sigma,
          games: r.games,
          wins: r.wins,
          provisional: r.sigma > PROVISIONAL_SIGMA,
          ...(overallRank !== undefined ? { overallRank } : {}),
        };
      });
      const history = await this.db
        .selectFrom('rating_history')
        .selectAll()
        .where('entity_id', '=', entity.id)
        .orderBy('created_at', 'desc')
        .limit(500)
        .execute();
      base.ratingHistory = history.reverse().map((h) => ({
        ladder: h.ladder,
        matchId: h.match_id,
        at: h.created_at.toISOString(),
        result: h.result,
        before: h.display_before,
        after: h.display_after,
        provisional: h.sigma_after > PROVISIONAL_SIGMA,
      }));
    }
    base.recentMatches = (await this.matches(ai, selected, { limit: 10 })).items;
    const recent = sql`SELECT * FROM match_results_view WHERE kind = 'ai' AND ai_id = ${ai} AND sim_version = ${selected} AND origin = 'queue' AND ended_at > now() - interval '90 days'`;
    const totals = await sql<{
      games: number;
      wins: number;
      losses: number;
      median: number | null;
      mean: number | null;
    }>`SELECT count(*) FILTER (WHERE outcome IS NOT NULL)::int AS games, count(*) FILTER (WHERE outcome = 'won')::int AS wins,
      count(*) FILTER (WHERE outcome IN ('lost','abandoned'))::int AS losses,
      percentile_cont(0.5) WITHIN GROUP (ORDER BY final_tick) AS median, avg(final_tick)::float8 AS mean FROM (${recent}) r`.execute(
      this.db,
    );
    const rates = await sql<{
      dimension: 'queue' | 'map' | 'generator';
      key: string;
      games: number;
      wins: number;
    }>`WITH recent AS (${recent})
      SELECT dimension, key, count(*)::int AS games, count(*) FILTER (WHERE outcome='won')::int AS wins
      FROM recent CROSS JOIN LATERAL (VALUES ('queue',queue_id),('map',map_hash),('generator',generator_id)) d(dimension,key)
      WHERE outcome IS NOT NULL AND key IS NOT NULL GROUP BY dimension,key ORDER BY games DESC,key LIMIT 60`.execute(
      this.db,
    );
    const t = present(totals.rows[0]);
    const titles = await catalogTitles(
      this.db,
      rates.rows.filter((r) => r.dimension === 'map').map((r) => r.key),
    );
    base.aggregates = {
      windowDays: 90,
      games: t.games,
      wins: t.wins,
      losses: t.losses,
      winRates: rates.rows.map((r) => ({
        ...r,
        winRate: r.wins / r.games,
        ...(r.dimension === 'queue'
          ? { label: this.queues.get(r.key) ?? r.key }
          : r.dimension === 'generator'
            ? { label: generatorLabel(r.key) }
            : titles.has(r.key)
              ? { label: titles.get(r.key)?.title, mapId: titles.get(r.key)?.mapId }
              : {}),
      })),
      ...(t.median !== null ? { medianTicks: t.median } : {}),
      ...(t.mean !== null ? { meanTicks: t.mean } : {}),
    };
    const curves = await sql<{
      match_id: string;
      seat: number;
      tick: number;
      units: number;
      buildings: number;
      prestige: number;
      average_units: number;
      average_buildings: number;
      average_prestige: number;
      games_at_tick: number;
    }>`
      WITH points AS MATERIALIZED (SELECT r.match_id,r.seat,r.ended_at,t.tick,t.units,t.buildings,t.prestige FROM (${recent}) r JOIN team_timeline_view t ON t.match_id=r.match_id AND t.team=r.team),
      latest AS (SELECT match_id FROM points ORDER BY ended_at DESC,match_id DESC LIMIT 1),
      averages AS (SELECT tick,avg(units)::float8 AS average_units,avg(buildings)::float8 AS average_buildings,avg(prestige)::float8 AS average_prestige,count(*)::int AS games_at_tick FROM points GROUP BY tick)
      SELECT p.match_id,p.seat,p.tick,p.units,p.buildings,p.prestige,a.average_units,a.average_buildings,a.average_prestige,a.games_at_tick FROM points p JOIN latest l ON l.match_id=p.match_id JOIN averages a USING(tick) ORDER BY p.tick`.execute(
      this.db,
    );
    const first = curves.rows[0];
    if (first)
      base.aggregates.economy = {
        matchId: first.match_id,
        seat: first.seat,
        points: curves.rows.map((p) => ({
          tick: p.tick,
          units: p.units,
          buildings: p.buildings,
          prestige: p.prestige,
          averageUnits: p.average_units,
          averageBuildings: p.average_buildings,
          averagePrestige: p.average_prestige,
          gamesAtTick: p.games_at_tick,
        })),
      };
    return base;
  }
}
