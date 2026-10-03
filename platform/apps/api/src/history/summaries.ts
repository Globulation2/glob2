// Match summaries for history lists: a page of matches, their participants,
// rating changes and map titles in four queries, and keyset cursors over
// "newest first" (ended, else started, else created).
import { sql, type Kysely, type Selectable } from 'kysely';
import type { Database } from '@glob2/db';
import type { AiId, MatchSetup, MatchSummary } from '@glob2/protocol';
import {
  PROVISIONAL_SIGMA,
  STORED_MATCH_SETUP,
  storedSimVersion,
  tryReadStored,
} from '@glob2/play';
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

/**
 * The name of an uploaded map (a premade map a room host sent with POST
 * /api/v1/uploads): the title the engine read from the file, else the file name
 * without its extension. `ownerId` limits it to one account's uploads.
 */
export async function uploadTitle(
  db: Db,
  hash: string,
  ownerId?: string,
): Promise<string | undefined> {
  let query = db
    .selectFrom('map_uploads')
    .select(['title', 'file_name'])
    .where('blob_sha256', '=', hash)
    .orderBy('created_at', 'desc');
  if (ownerId) query = query.where('owner_account_id', '=', ownerId);
  const row = await query.executeTakeFirst();
  return row ? uploadName(row.title, row.file_name) : undefined;
}

/** The name an upload gives a map: its embedded title, else the file name without extension. */
function uploadName(title: string | null, fileName: string | null): string | undefined {
  const name = title?.trim() || fileName?.replace(/\.(map|game)(\.gz)?$/i, '').trim();
  return name ? name.slice(0, 128) : undefined;
}

/**
 * Names of uploaded maps by hash and uploader, for many matches in one query. A
 * match only shows the name an account that played in it gave the map (the room
 * host's premade map), never another account's private upload.
 */
export async function uploadTitles(
  db: Db,
  hashes: readonly string[],
): Promise<Map<string, Map<string, { title: string; width?: number; height?: number }>>> {
  const unique = [...new Set(hashes)];
  const titles = new Map<string, Map<string, { title: string; width?: number; height?: number }>>();
  if (unique.length === 0) return titles;
  const rows = await db
    .selectFrom('map_uploads')
    .select(['blob_sha256', 'owner_account_id', 'title', 'file_name', 'width', 'height'])
    .where('blob_sha256', 'in', unique)
    .where('format', '=', 'map')
    .orderBy('created_at', 'desc')
    .execute();
  for (const row of rows) {
    const title = uploadName(row.title, row.file_name);
    if (!title) continue;
    const byOwner = titles.get(row.blob_sha256) ?? new Map();
    if (!byOwner.has(row.owner_account_id)) {
      byOwner.set(row.owner_account_id, {
        title,
        ...(row.width !== null ? { width: row.width } : {}),
        ...(row.height !== null ? { height: row.height } : {}),
      });
    }
    titles.set(row.blob_sha256, byOwner);
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

export function generatorOf(setup: MatchSetup | undefined): string | undefined {
  const map = setup?.map;
  return map && map.kind === 'generated' ? map.generator.generatorId : undefined;
}

/**
 * Builds summaries for matches, keeping the order of `rows`. `queueNames` maps
 * queue ids to the names the instance config gives them ("Casual 1v1").
 */
export async function summarize(
  db: Db,
  rows: readonly MatchRow[],
  queueNames: ReadonlyMap<string, string> = new Map(),
): Promise<MatchSummary[]> {
  if (rows.length === 0) return [];
  const ids = rows.map((row) => row.id);
  const [participants, history, titles, uploads] = await Promise.all([
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
    uploadTitles(
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
    // A list page degrades for a row whose setup no longer decodes: the sim
    // version comes from its column and the map title from the catalog only.
    const decoded = tryReadStored(STORED_MATCH_SETUP, match.setup);
    const setup = decoded.ok ? decoded.value : undefined;
    const generator = generatorOf(setup);
    const players = (seatsOf.get(match.id) ?? []).flatMap((p) =>
      p.account_id ? [p.account_id] : [],
    );
    const title =
      titles.get(match.map_hash)?.title ??
      (generator ? generatorLabel(generator) : undefined) ??
      players.map((id) => uploads.get(match.map_hash)?.get(id)?.title).find(Boolean);
    const queueName = match.queue_id ? queueNames.get(match.queue_id) : undefined;
    return {
      id: match.id,
      simVersion: setup?.simVersion ?? storedSimVersion(match.sim_version),
      origin: match.origin,
      ...(match.queue_id ? { queueId: match.queue_id } : {}),
      ...(queueName ? { queueName: queueName.slice(0, 64) } : {}),
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
