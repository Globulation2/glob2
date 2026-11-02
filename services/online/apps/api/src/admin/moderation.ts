import { sql, type Kysely, type Transaction } from 'kysely';
import type { Database } from '@glob2/db';
import type { AdminContent, AdminLibrary, AdminReport } from '@glob2/protocol';
import { apiError } from '../errors.ts';
import { cursorTimeSql } from '../http/cursorTime.ts';
import { decodeCursor, encodeCursor, pageSize } from './cursor.ts';

type Db = Kysely<Database> | Transaction<Database>;
export const LIBRARIES = [
  'maps',
  'ais',
  'generators',
  'buildings',
  'sets',
  'skins',
  'music',
] as const;
const specs = {
  generators: [
    'generators',
    'name',
    'owner_account_id',
    'generator_reports',
    'generator_id',
    'reporter_account_id',
  ],
  maps: ['maps', 'title', 'owner_account_id', 'map_reports', 'map_id', 'reporter_account_id'],
  ais: ['ais', 'name', 'owner_account_id', 'ai_reports', 'ai_id', 'reporter_account_id'],
  buildings: [
    'building_families',
    'name',
    'owner_account_id',
    'building_reports',
    'family_id',
    'reporter_account_id',
  ],
  sets: ['asset_sets', 'title', 'owner_account_id', 'set_reports', 'set_id', 'reporter_account_id'],
  skins: [
    'colony_skins',
    'name',
    'owner_account_id',
    'colony_skin_reports',
    'version_id',
    'reporter_account_id',
  ],
  music: ['music_releases', null, 'owner_id', 'music_reports', 'release_id', 'account_id'],
} as const;
export function libraryOf(value: string): AdminLibrary {
  if (!LIBRARIES.includes(value as AdminLibrary)) throw apiError('bad_request', 'Unknown library.');
  return value as AdminLibrary;
}
const hidden = (library: AdminLibrary, alias = 'c') =>
  library === 'skins'
    ? sql`${sql.ref(`${alias}.disabled_at`)} IS NOT NULL`
    : sql`${sql.ref(`${alias}.hidden`)}`;
const contentName = (library: AdminLibrary) =>
  library === 'music'
    ? sql`coalesce(c.metadata->>'title','Untitled music')`
    : sql`${sql.ref(`c.${specs[library][1]}`)}`;
const href = (library: AdminLibrary, id: string) =>
  library === 'skins' ? '/skins' : `/${library}/${id}`;

function contentQuery(library: AdminLibrary) {
  const [table, , owner] = specs[library];
  const downloads =
    library === 'maps' || library === 'buildings'
      ? sql`c.download_count`
      : library === 'music'
        ? sql`c.downloads`
        : library === 'sets'
          ? sql`(SELECT count(*)::int FROM set_downloads d JOIN set_versions v ON v.id=d.version_id WHERE v.set_id=c.id)`
          : library === 'ais'
            ? sql`(SELECT count(*)::int FROM ai_downloads d JOIN ai_versions v ON v.id=d.version_id WHERE v.ai_id=c.id)`
            : sql`NULL::int`;
  return sql`SELECT c.id, ${library}::text AS library, ${contentName(library)} AS name,
 ${sql.ref(`c.${owner}`)} AS "ownerId", ${hidden(library)} AS hidden,
 ${library === 'skins' || library === 'music' ? sql`NULL::text` : sql`c.hidden_reason`} AS reason,
 c.created_at AS "createdAt", ${cursorTimeSql(sql<Date>`c.created_at`)} AS "cursorAt", ${library === 'skins' ? sql`(SELECT '/api/v1/admin/skins/versions/' || v.id::text || '/texture' FROM colony_skin_versions v WHERE v.skin_id=c.id ORDER BY v.created_at DESC,v.id DESC LIMIT 1)` : sql`NULL::text`} AS "previewHref", ${downloads} AS downloads FROM ${sql.table(table)} c`;
}
function reportQuery(library: AdminLibrary) {
  const [table, , , reports, fk, reporter] = specs[library];
  const status =
    library === 'maps' || library === 'ais' || library === 'generators'
      ? sql`r.status`
      : library === 'skins'
        ? sql`CASE WHEN r.resolution IS NULL THEN 'open' WHEN r.resolution='dismissed' THEN 'dismissed' ELSE 'resolved' END`
        : sql`CASE WHEN r.resolved THEN coalesce(m.resolution,'resolved') ELSE 'open' END`;
  const resolution =
    library === 'maps' || library === 'ais' || library === 'generators'
      ? sql`r.resolution_note`
      : library === 'skins'
        ? sql`r.resolution_reason`
        : library === 'sets'
          ? sql`coalesce(m.reason,r.resolution)`
          : sql`m.reason`;
  return sql`SELECT r.id, ${library}::text AS library, c.id AS "contentId", ${contentName(library)} AS name,
 ${hidden(library)} AS hidden, ${sql.ref(`r.${reporter}`)} AS "reporterId", coalesce(a.display_name,'Deleted player') AS "reporterName",
 ${library === 'skins' ? sql`'/api/v1/admin/skins/versions/' || r.version_id::text || '/texture'` : sql`NULL::text`} AS "previewHref", r.reason, ${library === 'maps' || library === 'ais' || library === 'generators' || library === 'sets' ? sql`r.details` : sql`''::text`} AS details,
 r.created_at AS "createdAt", ${cursorTimeSql(sql<Date>`r.created_at`)} AS "cursorAt", ${status} AS status, ${resolution} AS resolution,
 ${library === 'maps' || library === 'skins' ? sql`r.resolved_at` : sql`m.resolved_at`} AS "resolvedAt"
 FROM ${sql.table(reports)} r
 ${library === 'skins' ? sql`JOIN colony_skin_versions v ON v.id=r.version_id JOIN colony_skins c ON c.id=v.skin_id` : sql`JOIN ${sql.table(table)} c ON c.id=${sql.ref(`r.${fk}`)}`}
 LEFT JOIN accounts a ON a.id=${sql.ref(`r.${reporter}`)}
 LEFT JOIN admin_report_resolutions m ON m.library=${library} AND m.report_id=r.id`;
}
function pageClause(cursor?: string) {
  const before = decodeCursor(cursor);
  return before
    ? sql`AND ("createdAt",library || ':' || id::text) < (${before.exactAt},${before.id})`
    : sql``;
}
export async function listContent(
  db: Db,
  query: {
    library?: string;
    q?: string;
    hidden?: string;
    sort?: string;
    cursor?: string;
    limit?: string;
  },
) {
  const libraries = query.library ? [libraryOf(query.library)] : LIBRARIES;
  if (query.hidden && !['true', 'false', 'all'].includes(query.hidden))
    throw apiError('bad_request', 'Invalid visibility.');
  const limit = pageSize(query.limit);
  const rows = (
    await sql<
      Omit<AdminContent, 'href'> & { cursorAt: string }
    >`WITH contents AS (${sql.join(libraries.map(contentQuery), sql` UNION ALL `)}) SELECT * FROM contents WHERE TRUE
 ${query.hidden === 'true' ? sql`AND hidden` : query.hidden === 'false' ? sql`AND NOT hidden` : sql``}
 ${query.q ? sql`AND (name ILIKE ${'%' + query.q.slice(0, 200).replace(/[\\%_]/g, '\\$&') + '%'} OR id::text=${query.q})` : sql``}
 ${pageClause(query.cursor)} ORDER BY ${query.sort === 'downloads' ? sql`downloads DESC NULLS LAST,` : sql``} "createdAt" DESC,library || ':' || id::text DESC LIMIT ${limit + 1}`.execute(
      db,
    )
  ).rows;
  const page = rows.slice(0, limit);
  const items = page.map(({ cursorAt, ...r }) => ({
    ...r,
    createdAt: new Date(cursorAt).toISOString(),
    href: href(r.library, r.id),
  }));
  const last = page.at(-1);
  return {
    items,
    ...(rows.length > limit && last
      ? { nextCursor: encodeCursor(last.cursorAt, `${last.library}:${last.id}`) }
      : {}),
  };
}
export async function listReports(
  db: Db,
  query: { library?: string; status?: string; cursor?: string; limit?: string },
) {
  const libraries = query.library ? [libraryOf(query.library)] : LIBRARIES;
  const status = query.status ?? 'open';
  if (!['open', 'resolved', 'dismissed', 'all'].includes(status))
    throw apiError('bad_request', 'Invalid report status.');
  const union = sql.join(libraries.map(reportQuery), sql` UNION ALL `),
    limit = pageSize(query.limit);
  const rows = (
    await sql<
      Omit<AdminReport, 'href'> & { cursorAt: string }
    >`WITH reports AS (${union}) SELECT * FROM reports WHERE TRUE ${status === 'all' ? sql`` : sql`AND status=${status}`} ${pageClause(query.cursor)} ORDER BY "createdAt" DESC,library || ':' || id::text DESC LIMIT ${limit + 1}`.execute(
      db,
    )
  ).rows;
  const counts = (
    await sql<{
      library: string;
      count: number;
    }>`WITH reports AS (${sql.join(LIBRARIES.map(reportQuery), sql` UNION ALL `)}) SELECT library,count(*)::int AS count FROM reports WHERE status='open' GROUP BY library`.execute(
      db,
    )
  ).rows;
  const page = rows.slice(0, limit);
  const items = page.map(({ cursorAt, ...r }) => ({
    ...r,
    createdAt: new Date(cursorAt).toISOString(),
    resolvedAt: r.resolvedAt ? new Date(r.resolvedAt).toISOString() : null,
    href: href(r.library, r.contentId),
  }));
  const last = page.at(-1);
  return {
    items,
    counts: Object.fromEntries(
      LIBRARIES.map((l) => [l, counts.find((c) => c.library === l)?.count ?? 0]),
    ),
    ...(rows.length > limit && last
      ? { nextCursor: encodeCursor(last.cursorAt, `${last.library}:${last.id}`) }
      : {}),
  };
}

export async function moderateContent(
  db: Db,
  library: AdminLibrary,
  id: string,
  value: boolean,
  reason: string,
  actor: string,
) {
  if (!reason.trim() || reason.length > 2000 || reason.includes('\0'))
    throw apiError('bad_request', 'Give a bounded moderation reason without NUL characters.');
  const [table] = specs[library];
  const before = (
    await sql<{
      hidden: boolean;
    }>`SELECT ${hidden(library)} AS hidden FROM ${sql.table(table)} c WHERE id=${id}::uuid FOR UPDATE`.execute(
      db,
    )
  ).rows[0];
  if (!before) throw apiError('not_found', 'Content not found.');
  await sql`UPDATE ${sql.table(table)} SET ${library === 'skins' ? sql`disabled_at=${value ? new Date() : null}` : library === 'music' ? sql`hidden=${value}` : sql`hidden=${value}, hidden_reason=${value ? reason : null}`} WHERE id=${id}::uuid`.execute(
    db,
  );
  await sql`INSERT INTO admin_audit_log(actor_account_id,action,target_type,target_id,details) VALUES(${actor},${'content.' + (value ? 'hide' : 'restore')},${library},${id},${JSON.stringify({ reason, from: before.hidden, to: value })}::jsonb)`.execute(
    db,
  );
}
export async function resolveReport(
  db: Kysely<Database>,
  library: AdminLibrary,
  id: string,
  input: { resolution: 'resolved' | 'dismissed'; reason: string; hide?: boolean },
  actor: string,
  legacy?: { action: string; targetType: string; reportTarget?: boolean },
) {
  if (
    library === 'skins' &&
    (input.reason.length > 1000 || (input.resolution === 'resolved' && !input.hide))
  )
    throw apiError(
      'bad_request',
      'Skin report resolutions need at most 1000 characters and disabling for a resolved report.',
    );
  return db.transaction().execute(async (trx) => {
    const [, , , table, fk] = specs[library];
    const row = (
      await sql<
        Record<string, unknown>
      >`SELECT * FROM ${sql.table(table)} WHERE id=${id}::uuid FOR UPDATE`.execute(trx)
    ).rows[0];
    if (!row) throw apiError('not_found', 'Report not found.');
    const open =
      library === 'maps' || library === 'ais' || library === 'generators'
        ? row['status'] === 'open'
        : library === 'skins'
          ? row['resolution'] === null
          : !row['resolved'];
    if (!open)
      throw apiError(
        'conflict',
        'Report has already been resolved. Refresh to review its resolution.',
      );
    let contentId = String(row[fk]);
    if (library === 'skins')
      contentId = (
        await trx
          .selectFrom('colony_skin_versions')
          .select('skin_id')
          .where('id', '=', contentId)
          .executeTakeFirstOrThrow()
      ).skin_id;
    if (input.hide) await moderateContent(trx, library, contentId, true, input.reason, actor);
    const update =
      library === 'maps'
        ? sql`status=${input.resolution},resolution_note=${input.reason},resolved_at=now(),resolved_by_account_id=${actor}::uuid`
        : library === 'ais' || library === 'generators'
          ? sql`status=${input.resolution},resolution_note=${input.reason}`
          : library === 'skins'
            ? sql`resolution=${input.hide ? 'disabled' : 'dismissed'},resolution_reason=${input.reason},resolved_at=now(),resolved_by_account_id=${actor}::uuid`
            : library === 'sets'
              ? sql`resolved=true,resolution=${input.reason}`
              : sql`resolved=true`;
    await sql`UPDATE ${sql.table(table)} SET ${update} WHERE id=${id}::uuid`.execute(trx);
    await sql`INSERT INTO admin_report_resolutions(library,report_id,resolution,reason,actor_id) VALUES(${library},${id}::uuid,${input.resolution},${input.reason},${actor}::uuid) ON CONFLICT(library,report_id) DO UPDATE SET resolution=excluded.resolution,reason=excluded.reason,actor_id=excluded.actor_id,resolved_at=now()`.execute(
      trx,
    );
    await sql`INSERT INTO admin_audit_log(actor_account_id,action,target_type,target_id,details) VALUES(${actor},${legacy?.action ?? 'report.resolve'},${legacy?.targetType ?? library},${legacy ? (legacy.reportTarget ? id : contentId) : id},${JSON.stringify({ reason: input.reason, from: 'open', to: input.resolution, contentId, report: id, hidden: !!input.hide })}::jsonb)`.execute(
      trx,
    );
  });
}
