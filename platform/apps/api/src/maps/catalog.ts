// Map catalog (plan I): catalog maps and their versions, who may see them,
// listing filters and the views the REST routes return.
//
// Visibility:
// - public: listed in the catalog and shown to anyone, signed in or not;
// - unlisted (the default): shown to anyone with its id or link, never listed;
// - private: only its owner.
// A map hidden by a moderator is shown only to its owner and to moderators,
// and rooms cannot choose it. Moderators and administrators see every map.
// Invisible maps answer 404, never 403, so their existence does not leak.
import { sql, type Kysely } from 'kysely';
import type { Account, Database } from '@glob2/db';
import type { MapInfo, MapVersionInfo, PublicAccount } from '@glob2/protocol';
import { STORED_GENERATOR, readStored, storedSimVersion } from '@glob2/play';
import { hasRole } from '../auth/admin.ts';
import { apiError } from '../errors.ts';

type Db = Kysely<Database>;
export type MapVisibility = 'public' | 'unlisted' | 'private';

/** Catalog rules; provisional product decisions live here so they are easy to change. */
export const CATALOG_RULES = {
  /** Visibility of a map created without one (provisional decision: unlisted). */
  defaultVisibility: 'unlisted' as MapVisibility,
  /** Guests may create unlisted and private maps but not publish them. */
  guestsMayPublish: false,
  /** Likes count only registered accounts. */
  guestsMayLike: false,
  /** New catalog maps per account per hour (per replica). */
  mapsPerHour: 20,
  /** Version uploads per account per hour (per replica). */
  versionsPerHour: 20,
  /** Reports per account per hour (per replica). */
  reportsPerHour: 10,
  /** Versions kept per map. */
  maxVersionsPerMap: 50,
  /** Side of the rendered preview, in pixels. */
  previewSizePx: 512,
  pageSize: 30,
  maxPageSize: 100,
} as const;

export interface Viewer {
  account: Account;
}

export function canModerate(viewer: Viewer | undefined): boolean {
  return viewer !== undefined && hasRole(viewer.account, 'moderator');
}

export type MapRow = {
  id: string;
  owner_account_id: string;
  title: string;
  description: string;
  visibility: MapVisibility;
  hidden: boolean;
  hidden_reason: string | null;
  play_count: number;
  download_count: number;
  like_count: number;
  made_with: 'hand' | 'generator';
  generator: unknown;
  latest_version_id: string | null;
  created_at: Date;
  updated_at: Date;
  owner_display_name: string;
  owner_kind: 'guest' | 'registered';
  owner_created_at: Date;
};

export type VersionRow = {
  id: string;
  map_id: string;
  hash: string;
  size: number | string;
  width: number | null;
  height: number | null;
  team_count: number | null;
  min_version_minor: number | null;
  preview_hash: string | null;
  validation: 'pending' | 'valid' | 'invalid';
  validation_error: string | null;
  sim_version: string | null;
  preview_status: 'pending' | 'ready' | 'failed';
  preview_width: number | null;
  preview_height: number | null;
  file_title: string | null;
  notes: string;
  validate_job_id: string | null;
  preview_job_id: string | null;
  created_at: Date;
};

/** True if the viewer may see the map at all. */
export function canView(
  map: Pick<MapRow, 'owner_account_id' | 'visibility' | 'hidden'>,
  viewer: Viewer | undefined,
): boolean {
  if (viewer && viewer.account.id === map.owner_account_id) return true;
  if (canModerate(viewer)) return true;
  if (map.hidden) return false;
  return map.visibility !== 'private';
}

export function isOwner(
  map: Pick<MapRow, 'owner_account_id'>,
  viewer: Viewer | undefined,
): boolean {
  return viewer !== undefined && viewer.account.id === map.owner_account_id;
}

const MAP_COLUMNS = [
  'm.id',
  'm.owner_account_id',
  'm.title',
  'm.description',
  'm.visibility',
  'm.hidden',
  'm.hidden_reason',
  'm.play_count',
  'm.download_count',
  'm.like_count',
  'm.made_with',
  'm.generator',
  'm.latest_version_id',
  'm.created_at',
  'm.updated_at',
  'a.display_name as owner_display_name',
  'a.kind as owner_kind',
  'a.created_at as owner_created_at',
] as const;

/** Maps with their owner's public fields. */
export function mapQuery(db: Db) {
  return db
    .selectFrom('maps as m')
    .innerJoin('accounts as a', 'a.id', 'm.owner_account_id')
    .select(MAP_COLUMNS);
}

export async function findMap(db: Db, id: string): Promise<MapRow | undefined> {
  return mapQuery(db).where('m.id', '=', id).executeTakeFirst() as Promise<MapRow | undefined>;
}

/** The map if the viewer may see it, else not_found. */
export async function visibleMap(db: Db, id: string, viewer: Viewer | undefined): Promise<MapRow> {
  const map = UUID.test(id) ? await findMap(db, id) : undefined;
  if (!map || !canView(map, viewer)) throw apiError('not_found', 'No such map.');
  return map;
}

/** The map if the viewer owns it (moderators may also act when `moderators` is set). */
export async function ownedMap(
  db: Db,
  id: string,
  viewer: Viewer,
  options: { moderators?: 'moderator' | 'admin' } = {},
): Promise<MapRow> {
  const map = await visibleMap(db, id, viewer);
  if (isOwner(map, viewer)) return map;
  if (options.moderators && hasRole(viewer.account, options.moderators)) return map;
  throw apiError('forbidden', 'Only the map owner can do that.');
}

export const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
export const SHA256 = /^[0-9a-f]{64}$/;

export function publicOwner(row: MapRow): PublicAccount {
  return {
    id: row.owner_account_id,
    displayName: row.owner_display_name,
    kind: row.owner_kind,
    createdAt: row.owner_created_at.toISOString(),
  };
}

export function versionUrls(origin: string, mapId: string, hash: string) {
  const base = `${origin}/api/v1/maps/${mapId}/versions/${hash}`;
  return { downloadUrl: `${base}/file`, previewUrl: `${base}/preview.png` };
}

export function versionView(origin: string, row: VersionRow): MapVersionInfo {
  const urls = versionUrls(origin, row.map_id, row.hash);
  const sim = row.sim_version ? storedSimVersion(row.sim_version) : undefined;
  return {
    hash: row.hash,
    size: Number(row.size),
    ...(row.width !== null ? { width: row.width } : {}),
    ...(row.height !== null ? { height: row.height } : {}),
    ...(row.team_count !== null ? { teamCount: row.team_count } : {}),
    ...(row.min_version_minor !== null ? { minVersionMinor: row.min_version_minor } : {}),
    ...(sim ? { simVersion: sim } : {}),
    validation: row.validation,
    ...(row.validation === 'invalid' && row.validation_error
      ? { reason: row.validation_error }
      : {}),
    ...(row.file_title ? { fileTitle: row.file_title } : {}),
    ...(row.notes ? { notes: row.notes } : {}),
    preview: row.preview_status,
    ...(row.preview_status === 'ready' ? { previewUrl: urls.previewUrl } : {}),
    downloadUrl: urls.downloadUrl,
    createdAt: row.created_at.toISOString(),
  };
}

export async function versionsOf(db: Db, mapIds: readonly string[]): Promise<VersionRow[]> {
  if (mapIds.length === 0) return [];
  return (await db
    .selectFrom('map_versions')
    .selectAll()
    .where('map_id', 'in', [...mapIds])
    .orderBy('created_at', 'desc')
    .orderBy('id', 'desc')
    .execute()) as VersionRow[];
}

/** MapInfo for a row; `latest` is its latest valid version, if any. */
export function mapView(
  origin: string,
  row: MapRow,
  latest: VersionRow | undefined,
  viewer: Viewer | undefined,
): MapInfo {
  const privileged = isOwner(row, viewer) || canModerate(viewer);
  return {
    id: row.id,
    owner: publicOwner(row),
    title: row.title,
    description: row.description,
    visibility: row.visibility,
    hidden: row.hidden,
    ...(row.hidden && privileged && row.hidden_reason ? { hiddenReason: row.hidden_reason } : {}),
    madeWith: row.made_with,
    ...(row.generator ? { generator: readStored(STORED_GENERATOR, row.generator) } : {}),
    ...(latest ? { latestVersion: versionView(origin, latest) } : {}),
    stats: { plays: row.play_count, downloads: row.download_count, likes: row.like_count },
    createdAt: row.created_at.toISOString(),
    updatedAt: row.updated_at.toISOString(),
  };
}

/** MapInfo views for rows, loading their latest versions in one query. */
export async function mapViews(
  db: Db,
  origin: string,
  rows: readonly MapRow[],
  viewer: Viewer | undefined,
): Promise<MapInfo[]> {
  const latestIds = rows.flatMap((r) => (r.latest_version_id ? [r.latest_version_id] : []));
  const latest = latestIds.length
    ? ((await db
        .selectFrom('map_versions')
        .selectAll()
        .where('id', 'in', latestIds)
        .execute()) as VersionRow[])
    : [];
  const byId = new Map(latest.map((v) => [v.id, v]));
  return rows.map((row) =>
    mapView(
      origin,
      row,
      row.latest_version_id ? byId.get(row.latest_version_id) : undefined,
      viewer,
    ),
  );
}

// ---------------------------------------------------------------- listing

export type MapSort = 'recent' | 'likes' | 'plays' | 'downloads';

export interface MapFilters {
  /** Exact team count of the latest version. */
  teams?: number;
  /** Bounds on the latest version's larger side, in tiles. */
  minSide?: number;
  maxSide?: number;
  madeWith?: 'hand' | 'generator';
  /** Case-insensitive title search. */
  q?: string;
}

export interface ListRequest extends MapFilters {
  /** Only this owner's maps (all of them for the owner and moderators, else public ones). */
  ownerId?: string;
  sort: MapSort;
  limit: number;
  cursor?: string;
}

const SORT_COLUMN = {
  recent: 'm.updated_at',
  likes: 'm.like_count',
  plays: 'm.play_count',
  downloads: 'm.download_count',
} as const;

function encodeCursor(value: string | number, id: string): string {
  return Buffer.from(JSON.stringify([value, id])).toString('base64url');
}

function decodeCursor(sort: MapSort, text: string): { value: Date | number; id: string } {
  try {
    const [value, id] = JSON.parse(Buffer.from(text, 'base64url').toString()) as [unknown, unknown];
    if (typeof id !== 'string' || !UUID.test(id)) throw new Error('bad id');
    if (sort === 'recent') {
      const at = new Date(String(value));
      if (Number.isNaN(at.getTime())) throw new Error('bad time');
      return { value: at, id };
    }
    if (typeof value !== 'number' || !Number.isInteger(value)) throw new Error('bad count');
    return { value, id };
  } catch {
    throw apiError('bad_request', 'Invalid cursor.');
  }
}

export async function listMaps(
  db: Db,
  request: ListRequest,
  viewer: Viewer | undefined,
): Promise<{ rows: MapRow[]; nextCursor?: string }> {
  // The latest valid version carries the facts listings filter on.
  let query = db
    .selectFrom('maps as m')
    .innerJoin('accounts as a', 'a.id', 'm.owner_account_id')
    .leftJoin('map_versions as lv', 'lv.id', 'm.latest_version_id')
    .select(MAP_COLUMNS);
  const seesAll =
    request.ownerId !== undefined &&
    ((viewer !== undefined && viewer.account.id === request.ownerId) || canModerate(viewer));
  if (request.ownerId !== undefined)
    query = query.where('m.owner_account_id', '=', request.ownerId);
  if (!seesAll) {
    query = query
      .where('m.visibility', '=', 'public')
      .where('m.hidden', '=', false)
      .where('m.latest_version_id', 'is not', null);
  }
  if (request.teams !== undefined) query = query.where('lv.team_count', '=', request.teams);
  const side = sql<number>`greatest(lv.width, lv.height)`;
  if (request.minSide !== undefined) query = query.where(side, '>=', request.minSide);
  if (request.maxSide !== undefined) query = query.where(side, '<=', request.maxSide);
  if (request.madeWith) query = query.where('m.made_with', '=', request.madeWith);
  if (request.q) {
    const pattern = `%${request.q.replace(/[\\%_]/g, (c) => `\\${c}`)}%`;
    query = query.where('m.title', 'ilike', pattern);
  }
  const column = SORT_COLUMN[request.sort];
  if (request.cursor) {
    const cursor = decodeCursor(request.sort, request.cursor);
    query = query.where((eb) =>
      eb.or([
        eb(sql.ref(column), '<', cursor.value),
        eb.and([eb(sql.ref(column), '=', cursor.value), eb('m.id', '<', cursor.id)]),
      ]),
    );
  }
  const rows = (await query
    .orderBy(sql.ref(column), 'desc')
    .orderBy('m.id', 'desc')
    .limit(request.limit + 1)
    .execute()) as MapRow[];
  const page = rows.slice(0, request.limit);
  const last = page.at(-1);
  if (rows.length <= request.limit || !last) return { rows: page };
  const value =
    request.sort === 'recent'
      ? last.updated_at.toISOString()
      : request.sort === 'likes'
        ? last.like_count
        : request.sort === 'plays'
          ? last.play_count
          : last.download_count;
  return { rows: page, nextCursor: encodeCursor(value, last.id) };
}

// ------------------------------------------------------------------ blobs

/**
 * True if catalog rules let the caller download these map bytes: a valid
 * version of a map the caller may see. Used by GET /api/v1/blobs/maps/{hash}
 * next to its upload, room and match rules.
 */
export async function catalogAllowsMapBlob(
  db: Db,
  hash: string,
  viewer: Viewer | undefined,
): Promise<boolean> {
  let query = db
    .selectFrom('map_versions as v')
    .innerJoin('maps as m', 'm.id', 'v.map_id')
    .select('v.id')
    .where('v.hash', '=', hash);
  if (!canModerate(viewer)) {
    query = query.where((eb) => {
      const visible = eb.and([
        eb('v.validation', '=', 'valid'),
        eb('m.hidden', '=', false),
        eb('m.visibility', '!=', 'private'),
      ]);
      return viewer ? eb.or([visible, eb('m.owner_account_id', '=', viewer.account.id)]) : visible;
    });
  }
  return (await query.executeTakeFirst()) !== undefined;
}

/** Records a download of a map; counted once per downloader and day. */
export async function countDownload(db: Db, mapId: string, downloader: string): Promise<void> {
  await db.transaction().execute(async (trx) => {
    const inserted = await trx
      .insertInto('map_downloads')
      .values({ map_id: mapId, downloader: downloader.slice(0, 128) })
      .onConflict((oc) => oc.columns(['map_id', 'downloader', 'day']).doNothing())
      .executeTakeFirst();
    if ((inserted.numInsertedOrUpdatedRows ?? 0n) > 0n) {
      await trx
        .updateTable('maps')
        .set((eb) => ({ download_count: eb('download_count', '+', 1) }))
        .where('id', '=', mapId)
        .execute();
    }
  });
}
