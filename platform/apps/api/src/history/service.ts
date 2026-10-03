// Read side of match history (plan section G): leaderboards, player profiles,
// match lists and match detail, over the tables and 0004 views the worker
// fills from verified results. Nothing here writes.
//
// Visibility rules:
// - Leaderboards list registered, active accounts only; AI entities are listed
//   separately, per sim version. Guests are never ranked.
// - Profiles of deleted accounts do not exist; banned accounts are visible to
//   moderators only. Guests get a minimal profile (no ratings or aggregates).
// - Every match is reachable by id (match links are shareable); the public
//   recent-matches list shows queue matches and matches of public rooms.
// - Replays and verifier results are public with their match; the raw match
//   record only to its players and moderators.
// - Each human player's connection quality (round trip to the relay,
//   disconnects, delayed orders; history/network.ts) is public with its match.
import { sql, type Kysely } from 'kysely';
import type { Account, Database } from '@glob2/db';
import {
  parseSimVersionKey,
  simVersionKey,
  type AiId,
  type AiLeaderboard,
  type EconomyCurve,
  type LeaderboardEntry,
  type LeaderboardPage,
  type MatchArtifactInfo,
  type MatchDetail,
  type MatchList,
  type MatchSetup,
  type MatchTeamStats,
  type OrderRejection,
  type PlayerProfile,
  type PlayerRating,
  type SimVersion,
  type TeamTimelinePoint,
  type VerificationDetail,
  type WinRate,
} from '@glob2/protocol';
import { PROVISIONAL_SIGMA, displayRating } from '@glob2/worker';
import { hasRole } from '../auth/admin.ts';
import { apiError } from '../errors.ts';
import { participantNetwork, reportTickRate } from './network.ts';
import {
  MATCH_TIME,
  UUID,
  catalogTitles,
  decodeCursor,
  encodeCursor,
  generatorLabel,
  generatorOf,
  summarize,
  type MatchRow,
} from './summaries.ts';

type Db = Kysely<Database>;

/** Aggregates cover the views' window ("recent games"). */
export const RECENT_DAYS = 90;
export const ARTIFACT_KINDS = ['record', 'replay', 'result'] as const;
export type ArtifactKind = (typeof ARTIFACT_KINDS)[number];

const LADDER = /^[a-z0-9][a-z0-9-]{0,63}$/;

export interface HistoryOptions {
  db: Db;
  origin: string;
  /** Queue id → display name, from instance config. */
  queueNames: ReadonlyMap<string, string>;
  /** Sim versions an engine agent currently serves. */
  currentSimVersions: () => Promise<SimVersion[]>;
}

export interface Viewer {
  account: Account;
}

const isModerator = (viewer: Viewer | undefined) =>
  viewer !== undefined && hasRole(viewer.account, 'moderator');

/** Deepest leaderboard offset served (each page numbers the ladder up to it). */
export const MAX_LEADERBOARD_OFFSET = 50_000;

function rankCursor(value: string | undefined): number {
  if (!value) return 0;
  const n = Number(Buffer.from(value, 'base64url').toString('utf8'));
  if (!Number.isInteger(n) || n < 0 || n > MAX_LEADERBOARD_OFFSET) {
    throw apiError('bad_request', 'Invalid cursor.');
  }
  return n;
}

function entry(
  rank: number,
  entity: LeaderboardEntry['entity'],
  r: { mu: number; sigma: number; games: number; wins: number },
): LeaderboardEntry {
  return {
    rank,
    entity,
    rating: Math.round(displayRating({ mu: r.mu, sigma: r.sigma }) * 10) / 10,
    mu: r.mu,
    sigma: r.sigma,
    games: r.games,
    wins: r.wins,
    provisional: r.sigma > PROVISIONAL_SIGMA,
  };
}

function timelinePoints(value: unknown): TeamTimelinePoint[] {
  if (!Array.isArray(value)) return [];
  return value.flatMap((point) => {
    if (!point || typeof point !== 'object' || Array.isArray(point)) return [];
    const p = point as Record<string, unknown>;
    const n = (key: string) => (typeof p[key] === 'number' ? Math.trunc(p[key]) : 0);
    if (typeof p['tick'] !== 'number') return [];
    return [
      {
        tick: n('tick'),
        units: n('units'),
        buildings: n('buildings'),
        prestige: n('prestige'),
        hp: n('hp'),
        attack: n('attack'),
        defense: n('defense'),
      },
    ];
  });
}

function numberRecord(value: unknown): Record<string, number> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return {};
  return Object.fromEntries(
    Object.entries(value as Record<string, unknown>).filter(
      (e): e is [string, number] => typeof e[1] === 'number' && Number.isFinite(e[1]),
    ),
  );
}

function orderRejections(value: unknown): OrderRejection[] | undefined {
  if (!Array.isArray(value)) return undefined;
  const count = (v: unknown) => (typeof v === 'number' && v >= 0 ? Math.trunc(v) : 0);
  return value.flatMap((item) => {
    if (!item || typeof item !== 'object') return [];
    const r = item as Record<string, unknown>;
    if (typeof r['seat'] !== 'number' || r['seat'] < 0 || r['seat'] > 11) return [];
    const reasons = Object.fromEntries(
      Object.entries(numberRecord(r['reasons'])).map(([k, v]) => [k.slice(0, 64), count(v)]),
    );
    return [
      {
        seat: Math.trunc(r['seat']),
        rejected: count(r['rejected']),
        stale: count(r['stale']),
        reasons,
        ...(typeof r['firstRejectedTick'] === 'number'
          ? { firstRejectedTick: count(r['firstRejectedTick']) }
          : {}),
      },
    ];
  });
}

export class HistoryService {
  private readonly db: Db;
  private readonly origin: string;
  private readonly queueNames: ReadonlyMap<string, string>;
  private readonly currentSimVersions: () => Promise<SimVersion[]>;

  constructor(options: HistoryOptions) {
    this.db = options.db;
    this.origin = options.origin;
    this.queueNames = options.queueNames;
    this.currentSimVersions = options.currentSimVersions;
  }

  // ------------------------------------------------------- leaderboards

  private async requireLadder(ladder: string): Promise<void> {
    if (!LADDER.test(ladder)) throw apiError('not_found', 'No such leaderboard.');
    if (this.queueNames.has(ladder)) return;
    const any = await this.db
      .selectFrom('ratings')
      .select('ladder')
      .where('ladder', '=', ladder)
      .limit(1)
      .executeTakeFirst();
    if (!any) throw apiError('not_found', 'No such leaderboard.');
  }

  async leaderboard(
    ladder: string,
    options: { cursor?: string; limit: number; provisional: 'include' | 'exclude' },
  ): Promise<LeaderboardPage> {
    await this.requireLadder(ladder);
    const offset = rankCursor(options.cursor);
    const settledOnly = options.provisional === 'exclude';
    const rows = await sql<{
      rank: string;
      mu: number;
      sigma: number;
      games: number;
      wins: number;
      id: string;
      display_name: string;
      kind: 'guest' | 'registered';
      created_at: Date;
    }>`
      SELECT * FROM (
        SELECT row_number() OVER (ORDER BY r.ordinal DESC, r.games DESC, a.id) AS rank,
               r.mu, r.sigma, r.games, r.wins, a.id, a.display_name, a.kind, a.created_at
        FROM ratings r
        JOIN rating_entities e ON e.id = r.entity_id AND e.kind = 'account'
        JOIN accounts a ON a.id = e.account_id
        WHERE r.ladder = ${ladder} AND r.games > 0
          AND a.kind = 'registered' AND a.status = 'active'
          AND (${settledOnly}::boolean IS FALSE OR r.sigma <= ${PROVISIONAL_SIGMA})
      ) ranked
      ORDER BY rank
      OFFSET ${offset} LIMIT ${options.limit + 1}`.execute(this.db);
    const page = rows.rows.slice(0, options.limit);
    const name = this.queueNames.get(ladder);
    return {
      ladder,
      ...(name ? { name } : {}),
      entries: page.map((row) =>
        entry(
          Number(row.rank),
          {
            kind: 'account',
            account: {
              id: row.id,
              displayName: row.display_name,
              kind: row.kind,
              createdAt: row.created_at.toISOString(),
            },
          },
          row,
        ),
      ),
      ...(rows.rows.length > options.limit
        ? {
            nextCursor: Buffer.from(String(offset + options.limit)).toString('base64url'),
          }
        : {}),
    };
  }

  async aiLeaderboard(ladder: string): Promise<AiLeaderboard> {
    await this.requireLadder(ladder);
    const rows = await this.db
      .selectFrom('ratings as r')
      .innerJoin('rating_entities as e', 'e.id', 'r.entity_id')
      .select(['e.ai_id', 'e.ai_sim_version', 'r.mu', 'r.sigma', 'r.games', 'r.wins', 'r.ordinal'])
      .where('r.ladder', '=', ladder)
      .where('e.kind', '=', 'ai')
      .orderBy('r.ordinal', 'desc')
      .execute();
    const current = new Set((await this.currentSimVersions()).map(simVersionKey));
    const groups = new Map<string, { version: SimVersion; entries: LeaderboardEntry[] }>();
    for (const row of rows) {
      const key = row.ai_sim_version;
      const version = key ? parseSimVersionKey(key) : undefined;
      if (!key || !version || !row.ai_id) continue;
      const group = groups.get(key) ?? { version, entries: [] };
      group.entries.push(
        entry(
          group.entries.length + 1,
          { kind: 'ai', ai: row.ai_id as AiId, simVersion: version },
          row,
        ),
      );
      groups.set(key, group);
    }
    const ordered = [...groups.entries()]
      .map(([key, group]) => ({ key, ...group }))
      .sort(
        (a, b) =>
          b.version.versionMinor - a.version.versionMinor ||
          b.version.netProtocol - a.version.netProtocol ||
          a.key.localeCompare(b.key),
      );
    return {
      ladder,
      groups: ordered.map((g) => ({
        simVersion: g.version,
        current: current.has(g.key),
        entries: g.entries,
      })),
    };
  }

  // ------------------------------------------------------------ players

  private async visibleAccount(id: string, viewer: Viewer | undefined): Promise<Account> {
    if (!UUID.test(id)) throw apiError('not_found', 'No such player.');
    const account = await this.db
      .selectFrom('accounts')
      .selectAll()
      .where('id', '=', id)
      .executeTakeFirst();
    if (!account || account.status === 'deleted') throw apiError('not_found', 'No such player.');
    if (account.status === 'banned' && !isModerator(viewer)) {
      throw apiError('not_found', 'No such player.');
    }
    return account;
  }

  async profile(id: string, viewer: Viewer | undefined): Promise<PlayerProfile> {
    const account = await this.visibleAccount(id, viewer);
    const recent = await this.playerMatches(account.id, viewer, { limit: 10 });
    const base = {
      account: {
        id: account.id,
        displayName: account.display_name,
        kind: account.kind,
        createdAt: account.created_at.toISOString(),
      },
      ...(isModerator(viewer) ? { status: account.status } : {}),
      recentMatches: recent.items,
    };
    if (account.kind === 'guest') {
      return { ...base, detail: 'minimal', ratings: [], ratingHistory: [] };
    }
    const [ratings, history, aggregates] = await Promise.all([
      this.ratingsOf(account.id),
      this.ratingHistoryOf(account.id),
      this.aggregatesOf(account.id),
    ]);
    return { ...base, detail: 'full', ratings, ratingHistory: history, aggregates };
  }

  private async ratingsOf(accountId: string): Promise<PlayerRating[]> {
    const rows = await sql<{
      ladder: string;
      mu: number;
      sigma: number;
      games: number;
      wins: number;
      rank: string | null;
    }>`
      SELECT r.ladder, r.mu, r.sigma, r.games, r.wins,
        CASE WHEN a.kind = 'registered' AND a.status = 'active' AND r.games > 0 THEN (
          SELECT count(*) + 1 FROM ratings o
          JOIN rating_entities oe ON oe.id = o.entity_id AND oe.kind = 'account'
          JOIN accounts oa ON oa.id = oe.account_id
          WHERE o.ladder = r.ladder AND o.games > 0 AND oa.kind = 'registered'
            AND oa.status = 'active'
            AND (o.ordinal > r.ordinal OR (o.ordinal = r.ordinal AND (o.games > r.games
                 OR (o.games = r.games AND oa.id < a.id))))
        ) END AS rank
      FROM ratings r
      JOIN rating_entities e ON e.id = r.entity_id
      JOIN accounts a ON a.id = e.account_id
      WHERE e.account_id = ${accountId}
      ORDER BY r.games DESC, r.ladder`.execute(this.db);
    return rows.rows.map((row) => ({
      ladder: row.ladder,
      ...(this.queueNames.get(row.ladder) ? { name: this.queueNames.get(row.ladder) } : {}),
      rating: Math.round(displayRating({ mu: row.mu, sigma: row.sigma }) * 10) / 10,
      mu: row.mu,
      sigma: row.sigma,
      games: row.games,
      wins: row.wins,
      provisional: row.sigma > PROVISIONAL_SIGMA,
      ...(row.rank !== null ? { rank: Number(row.rank) } : {}),
    }));
  }

  private async ratingHistoryOf(accountId: string) {
    const rows = await this.db
      .selectFrom('rating_history as h')
      .innerJoin('rating_entities as e', 'e.id', 'h.entity_id')
      .select([
        'h.ladder',
        'h.match_id',
        'h.created_at',
        'h.result',
        'h.display_before',
        'h.display_after',
        'h.sigma_after',
      ])
      .where('e.account_id', '=', accountId)
      .orderBy('h.created_at', 'desc')
      .limit(500)
      .execute();
    return rows.reverse().map((row) => ({
      ladder: row.ladder,
      matchId: row.match_id,
      at: row.created_at.toISOString(),
      result: row.result,
      before: row.display_before,
      after: row.display_after,
      provisional: row.sigma_after > PROVISIONAL_SIGMA,
    }));
  }

  private async aggregatesOf(accountId: string) {
    const [totals, lengths, rates, economy] = await Promise.all([
      sql<{ games: number; wins: number; losses: number }>`
        SELECT count(*)::integer AS games,
               count(*) FILTER (WHERE outcome = 'won')::integer AS wins,
               count(*) FILTER (WHERE outcome IN ('lost', 'abandoned'))::integer AS losses
        FROM match_results_view
        WHERE account_id = ${accountId} AND outcome IS NOT NULL
          AND ended_at > now() - make_interval(days => ${RECENT_DAYS})`.execute(this.db),
      sql<{ median: number | null; mean: number | null }>`
        SELECT percentile_cont(0.5) WITHIN GROUP (ORDER BY final_tick)::double precision AS median,
               avg(final_tick)::double precision AS mean
        FROM match_results_view
        WHERE account_id = ${accountId} AND final_tick IS NOT NULL
          AND ended_at > now() - make_interval(days => ${RECENT_DAYS})`.execute(this.db),
      this.db
        .selectFrom('recent_win_rates_view')
        .selectAll()
        .where('account_id', '=', accountId)
        .orderBy('games', 'desc')
        .orderBy('key')
        .limit(60)
        .execute(),
      this.latestEconomy(accountId),
    ]);
    const t = totals.rows[0] ?? { games: 0, wins: 0, losses: 0 };
    const l = lengths.rows[0];
    const titles = await catalogTitles(
      this.db,
      rates.filter((r) => r.dimension === 'map').map((r) => r.key),
    );
    const winRates: WinRate[] = rates.map((row) => {
      const title = row.dimension === 'map' ? titles.get(row.key) : undefined;
      const label =
        row.dimension === 'queue'
          ? row.key === 'room'
            ? 'Rooms'
            : this.queueNames.get(row.key)
          : row.dimension === 'generator'
            ? generatorLabel(row.key)
            : title?.title;
      return {
        dimension: row.dimension as WinRate['dimension'],
        key: row.key,
        ...(label ? { label: label.slice(0, 128) } : {}),
        ...(title ? { mapId: title.mapId } : {}),
        games: row.games,
        wins: row.wins,
        winRate: row.win_rate,
      };
    });
    return {
      windowDays: RECENT_DAYS,
      games: t.games,
      wins: t.wins,
      losses: t.losses,
      winRates,
      ...(l?.median !== null && l?.median !== undefined ? { medianTicks: l.median } : {}),
      ...(l?.mean !== null && l?.mean !== undefined ? { meanTicks: l.mean } : {}),
      ...(economy ? { economy } : {}),
    };
  }

  private async latestEconomy(accountId: string): Promise<EconomyCurve | undefined> {
    // The player's latest recent verified match with a timeline.
    const latest = await this.db
      .selectFrom('match_results_view as r')
      .select('r.match_id')
      .where('r.account_id', '=', accountId)
      .where('r.kind', '=', 'human')
      .where('r.ended_at', '>', sql<Date>`now() - make_interval(days => ${RECENT_DAYS})`)
      .where((eb) =>
        eb.exists(
          eb
            .selectFrom('match_team_stats as s')
            .select('s.match_id')
            .whereRef('s.match_id', '=', 'r.match_id')
            .whereRef('s.team', '=', 'r.team')
            .where(
              sql<boolean>`jsonb_typeof(s.timeline) = 'array' AND jsonb_array_length(s.timeline) > 0`,
            ),
        ),
      )
      .orderBy('r.ended_at', 'desc')
      .limit(1)
      .executeTakeFirst();
    if (!latest) return undefined;
    return (await this.economyOf(latest.match_id, accountId))[0];
  }

  /**
   * Economy curves of a match's human players (or one of them), each next to
   * the player's own recent average. match_economy_curves (migration 0015)
   * reads only these players' recent matches, so a public match page costs
   * the same however much history the instance holds.
   */
  private async economyOf(matchId: string, accountId?: string): Promise<EconomyCurve[]> {
    const result = await sql<{
      account_id: string | null;
      seat: number;
      tick: number | null;
      units: number | null;
      buildings: number | null;
      prestige: number | null;
      average_units: number | null;
      average_buildings: number | null;
      average_prestige: number | null;
      games_at_tick: number | null;
    }>`SELECT * FROM match_economy_curves(${matchId}::uuid, ${accountId ?? null}::uuid)`.execute(
      this.db,
    );
    const rows = result.rows;
    const curves = new Map<string, EconomyCurve>();
    for (const row of rows) {
      if (row.account_id === null || row.tick === null) continue;
      let curve = curves.get(row.account_id);
      if (!curve) {
        curve = { matchId, accountId: row.account_id, seat: row.seat, points: [] };
        curves.set(row.account_id, curve);
      }
      curve.points.push({
        tick: row.tick,
        units: row.units ?? 0,
        buildings: row.buildings ?? 0,
        prestige: row.prestige ?? 0,
        averageUnits: row.average_units ?? 0,
        averageBuildings: row.average_buildings ?? 0,
        averagePrestige: row.average_prestige ?? 0,
        gamesAtTick: row.games_at_tick ?? 0,
      });
    }
    return [...curves.values()];
  }

  // ------------------------------------------------------------ matches

  private matchesQuery() {
    return this.db.selectFrom('matches as m').selectAll('m');
  }

  private async page(
    query: ReturnType<HistoryService['matchesQuery']>,
    cursor: string | undefined,
    limit: number,
  ): Promise<MatchList> {
    const after = decodeCursor(cursor);
    let q = query;
    if (after) q = q.where(sql<boolean>`(${MATCH_TIME}, m.id) < (${after.at}, ${after.id})`);
    const rows = (await q
      .orderBy(MATCH_TIME, 'desc')
      .orderBy('m.id', 'desc')
      .limit(limit + 1)
      .execute()) as MatchRow[];
    const page = rows.slice(0, limit);
    const last = page.at(-1);
    return {
      items: await summarize(this.db, page),
      ...(rows.length > limit && last
        ? {
            nextCursor: encodeCursor({
              at: last.ended_at ?? last.started_at ?? last.created_at,
              id: last.id,
            }),
          }
        : {}),
    };
  }

  async playerMatches(
    accountId: string,
    viewer: Viewer | undefined,
    options: { cursor?: string; limit: number; queue?: string },
  ): Promise<MatchList> {
    await this.visibleAccount(accountId, viewer);
    let query = this.matchesQuery()
      .where('m.status', '!=', 'cancelled')
      .where((eb) =>
        eb.exists(
          eb
            .selectFrom('match_participants as p')
            .select('p.seat')
            .whereRef('p.match_id', '=', 'm.id')
            .where('p.account_id', '=', accountId),
        ),
      );
    if (options.queue) query = this.byQueue(query, options.queue);
    return this.page(query, options.cursor, options.limit);
  }

  private byQueue(query: ReturnType<HistoryService['matchesQuery']>, queue: string) {
    return queue === 'room'
      ? query.where('m.origin', '=', 'room')
      : query.where('m.queue_id', '=', queue);
  }

  /** Recent ended matches anyone may browse: queue matches and public rooms. */
  async recentMatches(options: {
    cursor?: string;
    limit: number;
    queue?: string;
  }): Promise<MatchList> {
    let query = this.matchesQuery()
      .leftJoin('rooms as r', 'r.id', 'm.room_id')
      .where('m.status', '=', 'ended')
      .where((eb) => eb.or([eb('m.origin', '=', 'queue'), eb('r.visibility', '=', 'public')]));
    if (options.queue) query = this.byQueue(query, options.queue);
    return this.page(query as never, options.cursor, options.limit);
  }

  /** Moderator match lookup by match id, account id, player name or relay id. */
  async adminMatches(options: {
    q?: string;
    status?: string;
    verification?: string;
    cursor?: string;
    limit: number;
  }): Promise<MatchList> {
    let query = this.matchesQuery();
    const q = options.q?.trim().slice(0, 200);
    if (q) {
      if (UUID.test(q.toLowerCase())) {
        const id = q.toLowerCase();
        query = query.where((eb) =>
          eb.or([
            eb('m.id', '=', id),
            eb('m.room_id', '=', id),
            eb.exists(
              eb
                .selectFrom('match_participants as p')
                .select('p.seat')
                .whereRef('p.match_id', '=', 'm.id')
                .where('p.account_id', '=', id),
            ),
          ]),
        );
      } else {
        const pattern = `%${q.toLowerCase().replace(/[\\%_]/g, (c) => `\\${c}`)}%`;
        query = query.where((eb) =>
          eb.or([
            eb('m.relay_id', '=', q),
            eb.exists(
              eb
                .selectFrom('match_participants as p')
                .select('p.seat')
                .whereRef('p.match_id', '=', 'm.id')
                .where(sql<string>`lower(p.display_name)`, 'like', pattern),
            ),
          ]),
        );
      }
    }
    if (options.status) {
      const statuses = ['starting', 'running', 'ended', 'cancelled'] as const;
      const status = statuses.find((s) => s === options.status);
      if (!status) throw apiError('bad_request', 'Unknown status.');
      query = query.where('m.status', '=', status);
    }
    if (options.verification) {
      const states = [
        'pending',
        'verified',
        'diverged',
        'unverifiable',
        'not_applicable',
        'failed',
      ] as const;
      const state = states.find((s) => s === options.verification);
      if (!state) throw apiError('bad_request', 'Unknown verification state.');
      query = query.where('m.verification', '=', state);
    }
    return this.page(query, options.cursor, options.limit);
  }

  private async match(id: string): Promise<MatchRow> {
    if (!UUID.test(id)) throw apiError('not_found', 'No such match.');
    const match = await this.db
      .selectFrom('matches')
      .selectAll()
      .where('id', '=', id)
      .executeTakeFirst();
    if (!match) throw apiError('not_found', 'No such match.');
    return match;
  }

  private async mayDownload(
    match: MatchRow,
    kind: ArtifactKind,
    viewer: Viewer | undefined,
  ): Promise<boolean> {
    if (kind !== 'record') return true;
    if (!viewer) return false;
    if (isModerator(viewer)) return true;
    const seat = await this.db
      .selectFrom('match_participants')
      .select('seat')
      .where('match_id', '=', match.id)
      .where('account_id', '=', viewer.account.id)
      .executeTakeFirst();
    return seat !== undefined;
  }

  artifactUrl(matchId: string, kind: ArtifactKind): string {
    return `${this.origin}/api/v1/matches/${matchId}/artifacts/${kind}`;
  }

  async matchDetail(id: string, viewer: Viewer | undefined): Promise<MatchDetail> {
    const match = await this.match(id);
    const [summary] = await summarize(this.db, [match]);
    if (!summary) throw apiError('not_found', 'No such match.');
    const [teams, artifacts, job, economy, titles, networkRows] = await Promise.all([
      this.db
        .selectFrom('match_team_stats')
        .selectAll()
        .where('match_id', '=', id)
        .orderBy('team')
        .execute(),
      this.db
        .selectFrom('match_artifacts as a')
        .innerJoin('blobs as b', 'b.sha256', 'a.blob_sha256')
        .select(['a.kind', 'b.sha256', 'b.size'])
        .where('a.match_id', '=', id)
        .orderBy('a.kind')
        .execute(),
      this.db
        .selectFrom('engine_jobs')
        .select(['result'])
        .where('match_id', '=', id)
        .where('kind', '=', 'verify-match')
        .where('status', '=', 'succeeded')
        .orderBy('completed_at', 'desc')
        .limit(1)
        .executeTakeFirst(),
      this.economyOf(id),
      catalogTitles(this.db, [match.map_hash]),
      this.db
        .selectFrom('match_participants')
        .select(['seat', 'network'])
        .where('match_id', '=', id)
        .where('kind', '=', 'human')
        .where('network', 'is not', null)
        .orderBy('seat')
        .execute(),
    ]);
    const tickRate = reportTickRate(match.end_report);
    const network = networkRows.flatMap((row) => {
      const condensed = participantNetwork(row.seat, row.network, tickRate);
      return condensed ? [condensed] : [];
    });
    const visibleArtifacts: MatchArtifactInfo[] = [];
    for (const artifact of artifacts) {
      if (!(await this.mayDownload(match, artifact.kind, viewer))) continue;
      visibleArtifacts.push({
        kind: artifact.kind,
        url: this.artifactUrl(id, artifact.kind),
        size: Number(artifact.size),
        sha256: artifact.sha256,
      });
    }
    const verdict = (job?.result ?? undefined) as Record<string, unknown> | undefined;
    const diverged = Array.isArray(verdict?.['clients'])
      ? (verdict['clients'] as unknown[]).filter(
          (s): s is number => typeof s === 'number' && s >= 0 && s <= 11,
        )
      : undefined;
    const rejections = orderRejections(verdict?.['orderRejections']);
    const verificationDetail: VerificationDetail = {
      ...(diverged && diverged.length > 0 ? { divergedSeats: diverged } : {}),
      ...(typeof verdict?.['reason'] === 'string'
        ? { reason: (verdict['reason'] as string).slice(0, 2000) }
        : {}),
      ...(rejections ? { orderRejections: rejections } : {}),
      ...(match.rating_note ? { ratingNote: match.rating_note.slice(0, 500) } : {}),
    };
    const setup = match.setup as unknown as MatchSetup;
    const generator = generatorOf(setup);
    const catalog = titles.get(match.map_hash);
    const generated = generator
      ? await this.db
          .selectFrom('generated_maps')
          .select(['width', 'height'])
          .where('map_hash', '=', match.map_hash)
          .limit(1)
          .executeTakeFirst()
      : undefined;
    const width = catalog?.width ?? generated?.width ?? undefined;
    const height = catalog?.height ?? generated?.height ?? undefined;
    const map = {
      ...(summary?.mapTitle ? { title: summary.mapTitle } : {}),
      ...(catalog ? { mapId: catalog.mapId } : {}),
      ...(generator ? { generatorId: generator } : {}),
      ...(width ? { width } : {}),
      ...(height ? { height } : {}),
    };
    return {
      match: summary,
      setup,
      teams: teams.map((t): MatchTeamStats => ({
        team: t.team,
        outcome: t.outcome,
        prestige: t.prestige,
        ...(t.eliminated_tick !== null ? { eliminatedTick: t.eliminated_tick } : {}),
        statistics: numberRecord(t.statistics),
        timeline: timelinePoints(t.timeline),
      })),
      artifacts: visibleArtifacts,
      map,
      verificationDetail,
      economy,
      ...(network.length > 0 ? { network } : {}),
    };
  }

  /** The blob behind a match artifact the viewer may download. */
  async artifact(
    id: string,
    kind: string,
    viewer: Viewer | undefined,
  ): Promise<{ storageKey: string; sha256: string; size: number; kind: ArtifactKind }> {
    const artifactKind = ARTIFACT_KINDS.find((k) => k === kind);
    if (!artifactKind) throw apiError('not_found', 'No such artifact.');
    const match = await this.match(id);
    if (!(await this.mayDownload(match, artifactKind, viewer))) {
      throw viewer
        ? apiError('not_found', 'No such artifact.')
        : apiError('unauthenticated', 'Sign in to download the match record.');
    }
    const row = await this.db
      .selectFrom('match_artifacts as a')
      .innerJoin('blobs as b', 'b.sha256', 'a.blob_sha256')
      .select(['b.storage_key', 'b.sha256', 'b.size'])
      .where('a.match_id', '=', id)
      .where('a.kind', '=', artifactKind)
      .executeTakeFirst();
    if (!row) throw apiError('not_found', 'No such artifact.');
    return {
      storageKey: row.storage_key,
      sha256: row.sha256,
      size: Number(row.size),
      kind: artifactKind,
    };
  }
}
