// Release gates for crash reports (docs/multiplayer/crash-reports.md#release-gates).
//
// POST /api/v1/crash-sessions takes the session counts of installs whose
// player chose "Always send", which crash-free rates are computed over. It
// takes no cookies or tokens and stores no address; installs are limited like
// crash reports.
//
// GET /api/v1/admin/crash-releases (administrators) shows, per release,
// crash-free sessions and installs, its crash reports and the groups first
// seen in it. GET /api/v1/crash-releases/:version/gate answers whether a
// release may be promoted, for the release runbook and workflows
// (tools/release/crash_gate.py, CRASH_SYMBOL_UPLOAD_TOKEN). Administrators
// set a group's triage state with POST /api/v1/admin/crash-groups/:id/state.
import type { FastifyInstance } from 'fastify';
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import {
  CrashGroupStateRequest,
  CrashSessionPing,
  type AdminCrashRelease,
  type AdminCrashReleaseGroup,
  type AdminCrashReleaseList,
  type CrashFreeStats,
  type CrashGroupState,
  type CrashGroupStateReceipt,
  type CrashReportKind,
  type CrashReleaseGate,
} from '@glob2/protocol';
import { requireRole, type Identity } from '../identity.ts';
import { apiError } from '../errors.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { pageSize } from '../admin/cursor.ts';
import { MemoryLimit } from './routes.ts';
import { authorized, CRASH_SYMBOL_TOKEN_ENV } from './symbols.ts';

/** Session pings per install and day, across replicas: one an hour is plenty. */
export const CRASH_SESSIONS_PER_INSTALL_DAY = 48;
/** Session pings per client address and hour, per replica (memory only). */
export const CRASH_SESSIONS_PER_ADDRESS_HOUR = 240;
/** Session pings the whole instance accepts per hour. */
export const CRASH_SESSIONS_PER_HOUR = 60_000;
/** Days back a ping may name: an install offline for a few days still counts. */
export const CRASH_SESSION_MAX_AGE_DAYS = 6;

/** Fewest opted-in sessions a release needs before the gate compares it. */
export const GATE_MINIMUM_SESSIONS = 1000;
/** How far crash-free sessions may fall below the previous release (0.5 points). */
export const GATE_TOLERANCE = 0.005;
/** The gate fails while any new group among this many of the release's top groups is untriaged. */
export const GATE_TOP_GROUPS = 5;
/** Groups listed per release as new in it. */
const NEW_GROUPS_SHOWN = 10;

/**
 * Report kinds that mean the game crashed: what crash-free rates count. Report
 * a Problem bundles and any non-fatal kind are left out.
 */
export const CRASH_KINDS: readonly CrashReportKind[] = ['crash', 'metrickit', 'web', 'java'];

const RELEASE = /^[0-9]+(\.[0-9]+){2,3}$/;
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const numeric = (column: string) => sql.raw(`string_to_array(${column}, '.')::int[]`);

const DAY_MS = 86_400_000;
const utcDay = (ms: number) => new Date(ms).toISOString().slice(0, 10);

/** Whether `day` is a real UTC day from CRASH_SESSION_MAX_AGE_DAYS ago up to tomorrow (clock skew). */
export function acceptedDay(day: string, now = Date.now()): boolean {
  const ms = Date.parse(`${day}T00:00:00Z`);
  if (!Number.isFinite(ms) || utcDay(ms) !== day) return false;
  return day >= utcDay(now - CRASH_SESSION_MAX_AGE_DAYS * DAY_MS) && day <= utcDay(now + DAY_MS);
}

function stats(
  sessions: number,
  installs: number,
  crashedSessions: number,
  crashedInstalls: number,
): CrashFreeStats {
  const share = (bad: number, total: number) => Math.min(1, Math.max(0, 1 - bad / total));
  return {
    sessions,
    installs,
    crashedSessions,
    crashedInstalls,
    ...(sessions > 0 ? { crashFreeSessions: share(crashedSessions, sessions) } : {}),
    ...(installs > 0 ? { crashFreeInstalls: share(crashedInstalls, installs) } : {}),
  };
}

interface ReleaseStats {
  overall: CrashFreeStats;
  breakdown: AdminCrashRelease['breakdown'];
}

/**
 * Crash-free rates of `versions`, overall and per platform and channel. Only
 * opted-in installs count: their sessions, and the crash reports of installs
 * that reported sessions in that release.
 */
export async function releaseStats(
  db: Kysely<Database>,
  versions: string[],
): Promise<Map<string, ReleaseStats>> {
  type Row = {
    version: string;
    platform: string | null;
    distribution: string | null;
    a: string;
    b: string;
  };
  if (versions.length === 0) return new Map();
  const [sessions, installs, crashes] = await Promise.all([
    sql<Row>`SELECT version, platform, distribution, sum(sessions)::text AS a, '0' AS b
      FROM crash_session_days WHERE version = ANY(${versions}::text[])
      GROUP BY GROUPING SETS ((version), (version, platform, distribution))`.execute(db),
    sql<Row>`SELECT version, platform, distribution, count(DISTINCT install_id)::text AS a, '0' AS b
      FROM crash_session_installs WHERE version = ANY(${versions}::text[])
      GROUP BY GROUPING SETS ((version), (version, platform, distribution))`.execute(db),
    sql<Row>`SELECT r.version, r.platform, r.distribution,
        count(*)::text AS a, count(DISTINCT r.install_id)::text AS b
      FROM crash_reports r
      WHERE r.kind = ANY(${CRASH_KINDS}::text[]) AND r.version = ANY(${versions}::text[])
        AND EXISTS (SELECT 1 FROM crash_session_installs s
          WHERE s.version = r.version AND s.install_id = r.install_id)
      GROUP BY GROUPING SETS ((r.version), (r.version, r.platform, r.distribution))`.execute(db),
  ]);
  const key = (r: Row) =>
    r.platform === null ? r.version : `${r.version}\0${r.platform}\0${r.distribution}`;
  const index = (rows: Row[]) => new Map(rows.map((r) => [key(r), r]));
  const bySessions = index(sessions.rows);
  const byInstalls = index(installs.rows);
  const byCrashes = index(crashes.rows);
  const slice = (k: string) =>
    stats(
      Number(bySessions.get(k)?.a ?? 0),
      Number(byInstalls.get(k)?.a ?? 0),
      Number(byCrashes.get(k)?.a ?? 0),
      Number(byCrashes.get(k)?.b ?? 0),
    );
  const result = new Map<string, ReleaseStats>();
  for (const version of versions) {
    const pairs = new Map<string, { platform: string; distribution: string }>();
    for (const r of [...sessions.rows, ...crashes.rows])
      if (r.version === version && r.platform !== null && r.distribution !== null)
        pairs.set(key(r), { platform: r.platform, distribution: r.distribution });
    const breakdown = [...pairs]
      .map(([k, pair]) => ({ ...pair, stats: slice(k) }))
      .sort(
        (a, b) =>
          b.stats.sessions - a.stats.sessions ||
          a.platform.localeCompare(b.platform) ||
          a.distribution.localeCompare(b.distribution),
      );
    result.set(version, { overall: slice(version), breakdown });
  }
  return result;
}

type GroupRow = {
  id: string;
  signature: string;
  state: CrashGroupState;
  first_version: string;
  reports: string;
  installs: string;
};
const releaseGroup = (row: GroupRow): AdminCrashReleaseGroup => ({
  id: row.id,
  signature: row.signature,
  state: row.state,
  reports: Number(row.reports),
  installs: Number(row.installs),
});

/** The groups with the most affected installs in `version`, whatever release they started in. */
async function topGroups(db: Kysely<Database>, version: string, limit: number) {
  const { rows } = await sql<GroupRow>`
    SELECT g.id, g.signature, g.state, g.first_version,
      count(*)::text AS reports, count(DISTINCT r.install_id)::text AS installs
    FROM crash_reports r JOIN crash_groups g ON g.id = r.group_id
    WHERE r.version = ${version} AND r.kind = ANY(${CRASH_KINDS}::text[])
    GROUP BY g.id
    ORDER BY count(DISTINCT r.install_id) DESC, count(*) DESC, g.id
    LIMIT ${limit}`.execute(db);
  return rows;
}

/**
 * Whether `version` may be promoted: crash-free sessions no more than
 * GATE_TOLERANCE below the newest earlier release with enough sessions, and no
 * untriaged group new in this release among its GATE_TOP_GROUPS top groups.
 * Too few sessions answers 'insufficient_data' unless a check already failed.
 */
export async function releaseGate(
  db: Kysely<Database>,
  version: string,
): Promise<CrashReleaseGate | undefined> {
  const release = await db
    .selectFrom('crash_releases')
    .select('version')
    .where('version', '=', version)
    .executeTakeFirst();
  if (!release) return undefined;
  const current = (await releaseStats(db, [version])).get(version)?.overall ?? stats(0, 0, 0, 0);
  const earlier = await sql<{ version: string; sessions: string }>`
    SELECT d.version, sum(d.sessions)::text AS sessions
    FROM crash_session_days d
    WHERE ${numeric('d.version')} < string_to_array(${version}::text, '.')::int[]
    GROUP BY d.version
    HAVING sum(d.sessions) >= ${GATE_MINIMUM_SESSIONS}
    ORDER BY ${numeric('d.version')} DESC
    LIMIT 1`.execute(db);
  const previousVersion = earlier.rows[0]?.version;
  const previous = previousVersion
    ? (await releaseStats(db, [previousVersion])).get(previousVersion)?.overall
    : undefined;
  const top = await topGroups(db, version, GATE_TOP_GROUPS);
  const untriaged = top.filter((g) => g.first_version === version && g.state === 'new');

  const percent = (value: number) => `${(value * 100).toFixed(2)}%`;
  const failures: string[] = [];
  const undecided: string[] = [];
  const passed: string[] = [];
  for (const group of untriaged)
    failures.push(
      `Untriaged new crash group among the top ${GATE_TOP_GROUPS}: ${group.signature} (${group.installs} installs).`,
    );
  if (untriaged.length === 0)
    passed.push(`No untriaged new crash group among the top ${GATE_TOP_GROUPS}.`);
  const rate = current.crashFreeSessions;
  if (current.sessions < GATE_MINIMUM_SESSIONS || rate === undefined)
    undecided.push(
      `${version} has ${current.sessions} opted-in sessions; the comparison needs ${GATE_MINIMUM_SESSIONS}.`,
    );
  else if (!previousVersion || previous?.crashFreeSessions === undefined)
    passed.push(
      `Crash-free sessions ${percent(rate)}; no earlier release has ${GATE_MINIMUM_SESSIONS} sessions to compare with.`,
    );
  else if (rate < previous.crashFreeSessions - GATE_TOLERANCE)
    failures.push(
      `Crash-free sessions ${percent(rate)} are more than ${percent(GATE_TOLERANCE)} below ${previousVersion} (${percent(previous.crashFreeSessions)}).`,
    );
  else
    passed.push(
      `Crash-free sessions ${percent(rate)} against ${percent(previous.crashFreeSessions)} in ${previousVersion}.`,
    );
  const verdict = failures.length ? 'fail' : undecided.length ? 'insufficient_data' : 'pass';
  return {
    version,
    verdict,
    reasons: [...failures, ...undecided, ...(verdict === 'pass' ? passed : [])],
    ...(previousVersion ? { previousVersion } : {}),
    sessions: current.sessions,
    ...(rate !== undefined ? { crashFreeSessions: rate } : {}),
    ...(previous ? { previousSessions: previous.sessions } : {}),
    ...(previous?.crashFreeSessions !== undefined
      ? { previousCrashFreeSessions: previous.crashFreeSessions }
      : {}),
    tolerance: GATE_TOLERANCE,
    minimumSessions: GATE_MINIMUM_SESSIONS,
    untriagedGroups: untriaged.map(releaseGroup),
  };
}

/** Records one ping: the install's day total only grows, and the day aggregate by the same step. */
export async function recordSessions(db: Kysely<Database>, ping: CrashSessionPing) {
  const where = {
    install_id: ping.installId,
    day: ping.day,
    version: ping.version,
    platform: ping.platform,
    distribution: ping.distribution,
  };
  await db.transaction().execute(async (trx) => {
    const created = await trx
      .insertInto('crash_session_installs')
      .values({ ...where, sessions: 0 })
      .onConflict((oc) => oc.doNothing())
      .returning('install_id')
      .executeTakeFirst();
    let query = trx.selectFrom('crash_session_installs').select('sessions');
    for (const [column, value] of Object.entries(where))
      query = query.where(column as keyof typeof where, '=', value);
    const row = await query.forUpdate().executeTakeFirstOrThrow();
    const step = Math.max(0, ping.sessions - row.sessions);
    if (step === 0 && !created) return;
    let update = trx.updateTable('crash_session_installs').set({ sessions: row.sessions + step });
    for (const [column, value] of Object.entries(where))
      update = update.where(column as keyof typeof where, '=', value);
    await update.execute();
    await trx
      .insertInto('crash_session_days')
      .values({
        day: ping.day,
        version: ping.version,
        platform: ping.platform,
        distribution: ping.distribution,
        sessions: step,
        installs: created ? 1 : 0,
      })
      .onConflict((oc) =>
        oc.columns(['day', 'version', 'platform', 'distribution']).doUpdateSet({
          sessions: sql`crash_session_days.sessions + excluded.sessions`,
          installs: sql`crash_session_days.installs + excluded.installs`,
        }),
      )
      .execute();
  });
}

function groupOffset(cursor: string | undefined): number {
  if (cursor === undefined) return 0;
  const offset = /^o([0-9]{1,7})$/.exec(cursor);
  if (!offset?.[1]) throw apiError('bad_request', 'Invalid cursor.');
  return Number(offset[1]);
}

export async function crashReleaseRoutes(app: FastifyInstance, identity: Identity) {
  const { db, config } = app.services;
  const token = (config.secrets ?? process.env)[CRASH_SYMBOL_TOKEN_ENV];
  const perInstall = new SharedLimit(
    db,
    'crash-session:install',
    CRASH_SESSIONS_PER_INSTALL_DAY,
    DAY_MS,
  );
  const perAddress = new MemoryLimit(CRASH_SESSIONS_PER_ADDRESS_HOUR, 3_600_000);
  const overall = new SharedLimit(db, 'crash-session:all', CRASH_SESSIONS_PER_HOUR, 3_600_000);

  await app.register(async (scope) => {
    // Browser builds on game portals post from another origin; the route
    // takes no credentials, so any origin may call it.
    scope.addHook('onRequest', async (_request, reply) => {
      void reply.header('access-control-allow-origin', '*');
    });
    scope.options('/api/v1/crash-sessions', async (_request, reply) =>
      reply
        .header('access-control-allow-methods', 'POST')
        .header('access-control-allow-headers', 'content-type')
        .header('access-control-max-age', '86400')
        .code(204)
        .send(),
    );
    scope.post('/api/v1/crash-sessions', { bodyLimit: 4096 }, async (request, reply) => {
      if (!perAddress.take(request.ip)) {
        void reply.header('retry-after', '3600');
        throw apiError('rate_limited', 'Too many session reports; try again later.');
      }
      const ping = body(CrashSessionPing, request.body);
      if (!acceptedDay(ping.day))
        throw apiError('bad_request', 'Sessions are accepted for the last seven days only.');
      const released = await db
        .selectFrom('crash_releases')
        .select('version')
        .where('version', '=', ping.version)
        .executeTakeFirst();
      if (!released)
        throw apiError('forbidden', 'Sessions are counted only for released versions.');
      await enforce(perInstall, ping.installId, reply, 'Too many session reports; try later.');
      await enforce(overall, 'all', reply, 'Too many session reports; try later.');
      await recordSessions(db, ping);
      return reply.code(204).send();
    });
  });

  app.get<{ Params: { version: string } }>(
    '/api/v1/crash-releases/:version/gate',
    async (r): Promise<CrashReleaseGate> => {
      if (!token) throw apiError('unavailable', 'Release gates are not configured.');
      if (!authorized(r.headers.authorization, token))
        throw apiError('unauthenticated', 'A release workflow token is required.');
      if (!RELEASE.test(r.params.version)) throw apiError('bad_request', 'Not a release version.');
      const gate = await releaseGate(db, r.params.version);
      if (!gate) throw apiError('not_found', 'No such release.');
      return gate;
    },
  );

  app.get<{ Querystring: { cursor?: string; limit?: string } }>(
    '/api/v1/admin/crash-releases',
    async (r): Promise<AdminCrashReleaseList> => {
      await requireRole(identity, r, 'admin');
      const limit = Math.min(pageSize(r.query.limit), 20);
      const offset = groupOffset(r.query.cursor);
      const releases = await db
        .selectFrom('crash_releases')
        .select(['version', 'first_upload_at'])
        .orderBy(numeric('version'), 'desc')
        .limit(limit + 1)
        .offset(offset)
        .execute();
      const versions = releases.slice(0, limit).map((row) => row.version);
      if (versions.length === 0) return { items: [] };
      const [byVersion, totals, groups] = await Promise.all([
        releaseStats(db, versions),
        sql<{ version: string; reports: string; installs: string }>`
          SELECT version, count(*)::text AS reports, count(DISTINCT install_id)::text AS installs
          FROM crash_reports WHERE kind = ANY(${CRASH_KINDS}::text[]) AND version = ANY(${versions}::text[])
          GROUP BY version`.execute(db),
        sql<GroupRow>`SELECT id, signature, state, first_version, reports, installs FROM (
            SELECT g.id, g.signature, g.state, g.first_version,
              count(r.id)::text AS reports, count(DISTINCT r.install_id)::text AS installs,
              row_number() OVER (PARTITION BY g.first_version
                ORDER BY count(DISTINCT r.install_id) DESC, count(r.id) DESC, g.id) AS rank
            FROM crash_groups g
            LEFT JOIN crash_reports r ON r.group_id = g.id AND r.version = g.first_version
            WHERE g.first_version = ANY(${versions}::text[])
            GROUP BY g.id) ranked
          WHERE rank <= ${NEW_GROUPS_SHOWN} ORDER BY first_version, rank`.execute(db),
      ]);
      const total = new Map(totals.rows.map((row) => [row.version, row]));
      const items: AdminCrashRelease[] = releases.slice(0, limit).map((row) => {
        const releaseStats = byVersion.get(row.version);
        return {
          version: row.version,
          registeredAt: row.first_upload_at.toISOString(),
          overall: releaseStats?.overall ?? stats(0, 0, 0, 0),
          breakdown: releaseStats?.breakdown ?? [],
          reports: Number(total.get(row.version)?.reports ?? 0),
          affectedInstalls: Number(total.get(row.version)?.installs ?? 0),
          newGroups: groups.rows.filter((g) => g.first_version === row.version).map(releaseGroup),
        };
      });
      return { items, ...(releases.length > limit ? { nextCursor: `o${offset + limit}` } : {}) };
    },
  );

  app.post<{ Params: { id: string } }>(
    '/api/v1/admin/crash-groups/:id/state',
    async (r): Promise<CrashGroupStateReceipt> => {
      const { account } = await requireRole(identity, r, 'admin');
      const input = body(CrashGroupStateRequest, r.body);
      if (input.state === 'resolved' && !input.fixedIn)
        throw apiError('bad_request', 'Name the release the crash is fixed in.');
      if (input.state !== 'resolved' && input.fixedIn)
        throw apiError('bad_request', 'Only a resolved crash names a fixed-in release.');
      if (!UUID.test(r.params.id)) throw apiError('not_found', 'No such crash group.');
      return db.transaction().execute(async (trx) => {
        const before = await trx
          .selectFrom('crash_groups')
          .select(['id', 'state', 'fixed_in'])
          .where('id', '=', r.params.id)
          .forUpdate()
          .executeTakeFirst();
        if (!before) throw apiError('not_found', 'No such crash group.');
        const fixedIn = input.fixedIn ?? before.fixed_in;
        await trx
          .updateTable('crash_groups')
          .set({ state: input.state, fixed_in: fixedIn, state_changed_at: new Date() })
          .where('id', '=', before.id)
          .execute();
        await trx
          .insertInto('admin_audit_log')
          .values({
            actor_account_id: account.id,
            action: 'crash_group.state',
            target_type: 'crash_group',
            target_id: before.id,
            details: JSON.stringify({
              from: before.state,
              to: input.state,
              ...(input.fixedIn ? { fixedIn: input.fixedIn } : {}),
            }),
          })
          .execute();
        return { id: before.id, state: input.state, ...(fixedIn ? { fixedIn } : {}) };
      });
    },
  );
}
