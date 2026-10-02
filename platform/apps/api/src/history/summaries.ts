// Match summaries for history lists: a page of matches, their participants,
// rating changes and map titles in four queries, and keyset cursors over
// "newest first" (ended, else started, else created).
import { sql, type Kysely, type Selectable } from 'kysely';
import type { Database } from '@glob2/db';
import type { AiId, MatchSetup, MatchSummary } from '@glob2/protocol';
import { PROVISIONAL_SIGMA } from '@glob2/worker';
import { apiError } from '../errors.ts';

type Db = Kysely<Database>;
export type MatchRow = Selectable<Database['matches']>;

/** The time a match sorts by in history lists. */
export const MATCH_TIME = sql<Date>`COALESCE(m.ended_at, m.started_at, m.created_at)`;

export interface MatchCursor {
  at: Date;
  id: string;
}

export function encodeCursor(cursor: MatchCursor): string {
  return Buffer.from(`${cursor.at.toISOString()}|${cursor.id}`).toString('base64url');
}

export function decodeCursor(value: string | undefined): MatchCursor | undefined {
  if (!value) return undefined;
  const [at, id] = Buffer.from(value, 'base64url').toString('utf8').split('|');
  const date = new Date(at ?? '');
  if (!id || !UUID.test(id) || Number.isNaN(date.getTime())) {
    throw apiError('bad_request', 'Invalid cursor.');
  }
  return { at: date, id };
}

export const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

export function pageLimit(value: string | undefined, fallback: number, max: number): number {
  if (value === undefined || value === '') return fallback;
  const n = Number(value);
  if (!Number.isInteger(n) || n < 1 || n > max) {
    throw apiError('bad_request', `limit must be an integer from 1 to ${max}.`);
  }
  return n;
}

/** Map titles of catalog maps anyone may see (public or unlisted, not hidden), by version hash. */
export async function catalogTitles(
  db: Db,
  hashes: readonly string[],
): Promise<Map<string, { title: string; mapId: string; width?: number; height?: number }>> {
  const unique = [...new Set(hashes)];
  if (unique.length === 0) return new Map();
  const rows = await db
    .selectFrom('map_versions as v')
    .innerJoin('maps as mp', 'mp.id', 'v.map_id')
    .select(['v.hash', 'mp.id', 'mp.title', 'v.width', 'v.height', 'mp.updated_at'])
    .where('v.hash', 'in', unique)
    .where('mp.visibility', 'in', ['public', 'unlisted'])
    .where('mp.hidden', '=', false)
    .orderBy('mp.updated_at', 'desc')
    .execute();
  const titles = new Map<
    string,
    { title: string; mapId: string; width?: number; height?: number }
  >();
  for (const row of rows) {
    if (titles.has(row.hash)) continue;
    titles.set(row.hash, {
      title: row.title,
      mapId: row.id,
      ...(row.width !== null ? { width: row.width } : {}),
      ...(row.height !== null ? { height: row.height } : {}),
    });
  }
  return titles;
}

/** A readable name for a generator id ("even-ground" → "Even Ground"). */
export function generatorLabel(id: string): string {
  return id
    .split(/[-_.]+/)
    .filter(Boolean)
    .map((word) => word.charAt(0).toUpperCase() + word.slice(1))
    .join(' ');
}

export function generatorOf(setup: unknown): string | undefined {
  const map = (setup as MatchSetup | undefined)?.map;
  return map && map.kind === 'generated' ? map.generator.generatorId : undefined;
}

/** Builds summaries for matches, keeping the order of `rows`. */
export async function summarize(db: Db, rows: readonly MatchRow[]): Promise<MatchSummary[]> {
  if (rows.length === 0) return [];
  const ids = rows.map((row) => row.id);
  const [participants, history, titles] = await Promise.all([
    db
      .selectFrom('match_participants')
      .selectAll()
      .where('match_id', 'in', ids)
      .orderBy('match_id')
      .orderBy('seat')
      .execute(),
    db
      .selectFrom('rating_history as h')
      .innerJoin('match_participants as p', (join) =>
        join.onRef('p.rating_entity_id', '=', 'h.entity_id').onRef('p.match_id', '=', 'h.match_id'),
      )
      .select([
        'h.match_id',
        'p.seat',
        'h.ladder',
        'h.display_before',
        'h.display_after',
        'h.sigma_after',
      ])
      .where('h.match_id', 'in', ids)
      .execute(),
    catalogTitles(
      db,
      rows.map((row) => row.map_hash),
    ),
  ]);
  const seatsOf = new Map<string, typeof participants>();
  for (const p of participants) {
    const list = seatsOf.get(p.match_id) ?? [];
    list.push(p);
    seatsOf.set(p.match_id, list);
  }
  const ratingOf = new Map(history.map((h) => [`${h.match_id}:${h.seat}`, h]));
  return rows.map((match) => {
    const setup = match.setup as unknown as MatchSetup;
    const generator = generatorOf(setup);
    const title =
      titles.get(match.map_hash)?.title ?? (generator ? generatorLabel(generator) : undefined);
    return {
      id: match.id,
      simVersion: setup.simVersion,
      origin: match.origin,
      ...(match.queue_id ? { queueId: match.queue_id } : {}),
      rated: match.rated,
      status: match.status,
      ...(match.end_reason ? { endReason: match.end_reason } : {}),
      verification: match.verification,
      mapHash: match.map_hash,
      ...(title ? { mapTitle: title.slice(0, 128) } : {}),
      ...(match.started_at ? { startedAt: match.started_at.toISOString() } : {}),
      ...(match.ended_at ? { endedAt: match.ended_at.toISOString() } : {}),
      ...(match.final_tick !== null ? { durationTicks: match.final_tick } : {}),
      participants: (seatsOf.get(match.id) ?? []).map((p) => {
        const rating = ratingOf.get(`${match.id}:${p.seat}`);
        return {
          seat: p.seat,
          team: p.team,
          kind: p.kind,
          displayName: p.display_name,
          ...(p.account_id ? { accountId: p.account_id } : {}),
          ...(p.ai_id ? { ai: p.ai_id as AiId } : {}),
          ...(p.outcome ? { outcome: p.outcome } : {}),
          disconnects: p.disconnects,
          ...(rating
            ? {
                rating: {
                  ladder: rating.ladder,
                  before: rating.display_before,
                  after: rating.display_after,
                  provisional: rating.sigma_after > PROVISIONAL_SIGMA,
                },
              }
            : {}),
        };
      }),
    };
  });
}
