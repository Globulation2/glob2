// Problem and crash reports from game clients (docs/multiplayer/problem-reports.md).
//
// POST /api/v1/crash-reports takes the bundle the game writes (a zip whose
// context.json is a CrashReportContext) without signing in, and answers with a
// short ID the player can quote. Admins list reports and download bundles or
// single attachments under /api/v1/admin/crash-reports.
//
// Crash bundles (docs/multiplayer/crash-reports.md) carry their crash in one
// more entry and are accepted only from releases whose symbols were uploaded;
// each queues a CRASH_PROCESS_TASK job for apps/crash-worker in the same
// transaction as its row.
//
// Privacy: nothing ties a report to an account, and the client address is used
// only by an in-memory limiter, never stored (the shared Postgres limiter keys
// on the random install ID instead).
import { randomInt, randomUUID } from 'node:crypto';
import type { FastifyInstance } from 'fastify';
import { sql, type Selectable } from 'kysely';
import type { CrashReportsTable } from '@glob2/db';
import {
  CRASH_PAYLOAD_ENTRIES,
  CrashReportContext,
  type AdminCrashGroup,
  type AdminCrashGroupList,
  type AdminCrashReport,
  type AdminCrashReportDetail,
  type AdminCrashReportList,
  type CrashReportReceipt,
} from '@glob2/protocol';
import { requireRole, type Identity } from '../identity.ts';
import { apiError } from '../errors.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { decodeCursor, encodeCursor, pageSize } from '../admin/cursor.ts';
import { cursorTimeSql } from '../http/cursorTime.ts';
import { CRASH_PROCESS_TASK, listEntries, readEntry, ZipError } from '@glob2/core';

/** Largest bundle accepted, packed and unpacked. */
export const CRASH_REPORT_MAX_BYTES = 10 * 1024 * 1024;
/** Reports per install and day, across replicas. */
export const CRASH_REPORTS_PER_INSTALL_DAY = 20;
/** Reports per client address and hour, per replica (memory only). */
export const CRASH_REPORTS_PER_ADDRESS_HOUR = 60;
/**
 * Reports the whole instance accepts per hour, across replicas. Install IDs
 * and forwarded addresses are client-chosen, so this bounds what a flood can
 * store (at most this many 10 MB bundles an hour).
 */
export const CRASH_REPORTS_PER_HOUR = 600;
const CONTEXT_MAX_BYTES = 16 * 1024;

const ID_ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
const ID = /^[A-HJ-NP-Z2-9]{8}$/;

export function newCrashReportId(): string {
  let id = '';
  for (let i = 0; i < 8; i++) id += ID_ALPHABET[randomInt(ID_ALPHABET.length)];
  return id;
}

/**
 * A per-replica fixed-window counter whose keys never leave memory. At most
 * `capacity` keys are kept: a new key evicts the oldest (Map insertion order),
 * so a flood of distinct addresses costs O(1) per request and bounded memory.
 */
export class MemoryLimit {
  private readonly windows = new Map<string, { start: number; count: number }>();
  readonly max: number;
  readonly windowMs: number;
  readonly capacity: number;
  private readonly now: () => number;
  constructor(max: number, windowMs: number, now: () => number = Date.now, capacity = 10_000) {
    this.max = max;
    this.windowMs = windowMs;
    this.now = now;
    this.capacity = capacity;
  }
  get size(): number {
    return this.windows.size;
  }
  take(key: string): boolean {
    const now = this.now();
    const window = this.windows.get(key);
    if (!window || now - window.start >= this.windowMs) {
      // Re-inserting moves the key to the newest end.
      this.windows.delete(key);
      while (this.windows.size >= this.capacity) {
        const oldest = this.windows.keys().next();
        if (oldest.done) break;
        this.windows.delete(oldest.value);
      }
      this.windows.set(key, { start: now, count: 1 });
      return true;
    }
    if (window.count >= this.max) return false;
    window.count++;
    return true;
  }
}

const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

function summary(row: Selectable<CrashReportsTable>): AdminCrashReport {
  return {
    id: row.id,
    kind: row.kind,
    status: row.status,
    ...(row.group_id ? { groupId: row.group_id } : {}),
    ...(row.reason ? { reason: row.reason } : {}),
    receivedAt: row.received_at.toISOString(),
    version: row.version,
    versionMinor: row.version_minor,
    simRevision: row.sim_revision,
    distribution: row.distribution,
    platform: row.platform,
    os: row.os,
    deviceClass: row.device_class,
    deviceTier: row.device_tier,
    description: row.description,
    installId: row.install_id,
    size: row.size,
    attachments: row.attachments,
    bundleAvailable: row.bundle_key !== null,
  };
}

/** Group list paging: an opaque offset, since groups sort by a computed count. */
function groupOffset(cursor: string | undefined): number {
  if (cursor === undefined) return 0;
  const offset = /^o([0-9]{1,7})$/.exec(cursor);
  if (!offset?.[1]) throw apiError('bad_request', 'Invalid cursor.');
  return Number(offset[1]);
}

function contentType(name: string): string {
  return /\.(txt|log|json)$/.test(name) ? 'text/plain; charset=utf-8' : 'application/octet-stream';
}

export async function crashReportRoutes(app: FastifyInstance, identity: Identity) {
  const { db, blobs } = app.services;
  const perInstall = new SharedLimit(
    db,
    'crash-report:install',
    CRASH_REPORTS_PER_INSTALL_DAY,
    86_400_000,
  );
  const perAddress = new MemoryLimit(CRASH_REPORTS_PER_ADDRESS_HOUR, 3_600_000);
  const overall = new SharedLimit(db, 'crash-report:all', CRASH_REPORTS_PER_HOUR, 3_600_000);

  await app.register(async (scope) => {
    // Browser builds on game portals post from another origin. The route takes
    // no cookies or tokens, so any origin may call it (requests carry no
    // credentials, which '*' requires).
    scope.addHook('onRequest', async (_request, reply) => {
      void reply.header('access-control-allow-origin', '*');
    });
    scope.options('/api/v1/crash-reports', async (_request, reply) =>
      reply
        .header('access-control-allow-methods', 'POST')
        .header('access-control-allow-headers', 'content-type')
        .header('access-control-max-age', '86400')
        .code(204)
        .send(),
    );
    scope.addContentTypeParser(
      'application/zip',
      { parseAs: 'buffer', bodyLimit: CRASH_REPORT_MAX_BYTES },
      (_request, raw, done) => done(null, raw),
    );
    scope.post(
      '/api/v1/crash-reports',
      { bodyLimit: CRASH_REPORT_MAX_BYTES },
      async (request, reply): Promise<CrashReportReceipt> => {
        if (!perAddress.take(request.ip)) {
          void reply.header('retry-after', '3600');
          throw apiError('rate_limited', 'Too many reports; try again later.');
        }
        if (!Buffer.isBuffer(request.body))
          throw apiError('bad_request', 'Send the report bundle as application/zip.');
        const bundle = request.body;
        let entries;
        let context: CrashReportContext;
        try {
          entries = listEntries(bundle, CRASH_REPORT_MAX_BYTES);
          const entry = entries.find((e) => e.name === 'context.json');
          if (!entry || entry.size > CONTEXT_MAX_BYTES) throw new ZipError('missing context.json');
          const parsed: unknown = JSON.parse(readEntry(bundle, entry).toString('utf8'));
          context = body(CrashReportContext, parsed);
        } catch (error) {
          if (error instanceof ZipError || error instanceof SyntaxError)
            throw apiError('bad_request', 'Invalid report bundle.');
          throw error;
        }
        // A retried upload (the answer was lost, or the player left the
        // screen mid-send) returns the report it already created.
        const existing = await db
          .selectFrom('crash_reports')
          .select('id')
          .where('submission_id', '=', context.submissionId)
          .executeTakeFirst();
        if (existing) return { id: existing.id };
        const crash = context.kind === 'problem' ? undefined : CRASH_PAYLOAD_ENTRIES[context.kind];
        if (crash) {
          const payload = entries.find((e) => e.name === crash);
          if (!payload || payload.size === 0)
            throw apiError('bad_request', 'Invalid report bundle.');
          // Only releases with uploaded symbols can be read; a report from any
          // other build would never get a stack.
          const released = await db
            .selectFrom('crash_releases')
            .select('version')
            .where('version', '=', context.version)
            .executeTakeFirst();
          if (!released)
            throw apiError('forbidden', 'Crash reports are accepted only from released versions.');
        }
        await enforce(perInstall, context.installId, reply, 'Too many reports; try again later.');
        await enforce(overall, 'all', reply, 'Too many reports; try again later.');
        const attachments = entries
          .filter((e) => e.name !== 'context.json')
          .map((e) => ({ name: e.name, size: e.size }));
        for (let attempt = 0; ; attempt++) {
          const id = newCrashReportId();
          const key = `crash-reports/${id.toLowerCase()}-${randomUUID()}.zip`;
          await blobs.put(key, bundle);
          try {
            const inserted = await db.transaction().execute(async (trx) => {
              const row = await trx
                .insertInto('crash_reports')
                .values({
                  id,
                  submission_id: context.submissionId,
                  kind: context.kind,
                  install_id: context.installId,
                  version: context.version,
                  version_minor: context.versionMinor,
                  sim_revision: context.simRevision,
                  distribution: context.distribution,
                  platform: context.platform,
                  os: context.os,
                  device_class: context.deviceClass,
                  device_tier: context.deviceTier,
                  description: context.description,
                  size: bundle.length,
                  attachments: JSON.stringify(attachments),
                  bundle_key: key,
                  status: crash ? 'pending' : 'none',
                  match_tick: context.match?.tick ?? null,
                  match_seed: context.match?.seed ?? null,
                })
                .onConflict((oc) => oc.doNothing())
                .returning('id')
                .executeTakeFirst();
              if (row && crash)
                await sql`SELECT graphile_worker.add_job(
                  identifier => ${CRASH_PROCESS_TASK}::text,
                  payload => ${JSON.stringify({ id })}::json,
                  job_key => ${`crash-${id}`}::text,
                  max_attempts => 3)`.execute(trx);
              return row;
            });
            if (inserted) {
              void reply.code(201);
              return { id };
            }
            // Either the short ID collided (try another) or the same
            // submission arrived concurrently (answer with its report).
            const raced = await db
              .selectFrom('crash_reports')
              .select('id')
              .where('submission_id', '=', context.submissionId)
              .executeTakeFirst();
            if (raced) {
              await blobs.delete(key).catch(() => undefined);
              return { id: raced.id };
            }
          } catch (error) {
            await blobs.delete(key).catch(() => undefined);
            throw error;
          }
          await blobs.delete(key).catch(() => undefined);
          if (attempt >= 4) throw apiError('internal', 'Could not store the report.');
        }
      },
    );
  });

  // Bundles hold a player's log, save and replay: every download is audited.
  const audit = (actor: string, id: string, file: string) =>
    db
      .insertInto('admin_audit_log')
      .values({
        actor_account_id: actor,
        action: 'crash_report.download',
        target_type: 'crash_report',
        target_id: id,
        details: JSON.stringify({ file }),
      })
      .execute();
  const load = async (id: string) => {
    if (!ID.test(id)) throw apiError('not_found', 'No such report.');
    const row = await db
      .selectFrom('crash_reports')
      .select(['id', 'bundle_key'])
      .where('id', '=', id)
      .executeTakeFirst();
    const stream = row?.bundle_key ? await blobs.get(row.bundle_key) : undefined;
    if (!row || !stream)
      throw apiError('not_found', 'The bundle of this report is no longer kept.');
    return { id: row.id, stream };
  };
  const readAll = async (stream: NodeJS.ReadableStream): Promise<Buffer> => {
    const chunks: Buffer[] = [];
    for await (const chunk of stream) chunks.push(Buffer.from(chunk as Buffer));
    return Buffer.concat(chunks);
  };

  app.get<{ Querystring: { cursor?: string; limit?: string; group?: string; kind?: string } }>(
    '/api/v1/admin/crash-reports',
    async (r): Promise<AdminCrashReportList> => {
      await requireRole(identity, r, 'admin');
      const limit = pageSize(r.query.limit);
      const cursor = decodeCursor(r.query.cursor);
      let query = db
        .selectFrom('crash_reports')
        .selectAll()
        .select(cursorTimeSql(sql<Date>`received_at`).as('cursor_at'))
        .orderBy('received_at', 'desc')
        .orderBy('id', 'desc')
        .limit(limit + 1);
      if (cursor)
        query = query.where(
          sql<boolean>`(received_at, id) < (${cursor.exactAt}::timestamptz, ${cursor.id})`,
        );
      if (r.query.kind !== undefined) {
        const kind = r.query.kind;
        if (kind !== 'problem' && !Object.hasOwn(CRASH_PAYLOAD_ENTRIES, kind))
          throw apiError('bad_request', 'Unknown report kind.');
        query = query.where('kind', '=', kind as AdminCrashReport['kind']);
      }
      if (r.query.group !== undefined) {
        if (!UUID.test(r.query.group)) throw apiError('bad_request', 'Invalid crash group.');
        query = query.where('group_id', '=', r.query.group);
      }
      const rows = await query.execute();
      const items: AdminCrashReport[] = rows.slice(0, limit).map(summary);
      const last = rows[limit - 1];
      return {
        items,
        ...(rows.length > limit && last
          ? { nextCursor: encodeCursor(last.cursor_at, last.id) }
          : {}),
      };
    },
  );

  app.get<{ Params: { id: string } }>(
    '/api/v1/admin/crash-reports/:id',
    async (r): Promise<AdminCrashReportDetail> => {
      await requireRole(identity, r, 'admin');
      const row = ID.test(r.params.id)
        ? await db
            .selectFrom('crash_reports')
            .selectAll()
            .where('id', '=', r.params.id)
            .executeTakeFirst()
        : undefined;
      if (!row) throw apiError('not_found', 'No such report.');
      return { report: summary(row), frames: row.frames ?? [] };
    },
  );

  // Groups sorted by affected installs, counted over the reports that match
  // the filters (a release, platform or channel).
  app.get<{
    Querystring: {
      cursor?: string;
      limit?: string;
      version?: string;
      platform?: string;
      distribution?: string;
      state?: string;
    };
  }>('/api/v1/admin/crash-groups', async (r): Promise<AdminCrashGroupList> => {
    await requireRole(identity, r, 'admin');
    const limit = pageSize(r.query.limit);
    const offset = groupOffset(r.query.cursor);
    const { version, platform, distribution, state } = r.query;
    if (state !== undefined && !['new', 'triaged', 'resolved'].includes(state))
      throw apiError('bad_request', 'Unknown crash group state.');
    let counts = db
      .selectFrom('crash_reports')
      .select((eb) => [
        'group_id',
        eb.fn.countAll<string>().as('reports'),
        sql<string>`count(DISTINCT install_id)`.as('installs'),
        sql<string[]>`array_agg(DISTINCT platform ORDER BY platform)`.as('platforms'),
        sql<string[]>`array_agg(DISTINCT distribution ORDER BY distribution)`.as('distributions'),
      ])
      .where('group_id', 'is not', null)
      .groupBy('group_id');
    if (version) counts = counts.where('version', '=', version);
    if (platform) counts = counts.where('platform', '=', platform);
    if (distribution) counts = counts.where('distribution', '=', distribution);
    let grouped = db
      .with('counts', () => counts)
      .selectFrom('counts')
      .innerJoin('crash_groups as g', 'g.id', 'counts.group_id');
    if (state) grouped = grouped.where('g.state', '=', state as AdminCrashGroup['state']);
    const rows = await grouped
      .select([
        'g.id',
        'g.signature',
        'g.first_version',
        'g.last_version',
        'g.first_seen_at',
        'g.last_seen_at',
        'g.state',
        'g.fixed_in',
        'g.github_issue',
        'g.github_issue_url',
        'counts.reports',
        'counts.installs',
        'counts.platforms',
        'counts.distributions',
      ])
      .orderBy(sql`counts.installs::bigint`, 'desc')
      .orderBy(sql`counts.reports::bigint`, 'desc')
      .orderBy('g.last_seen_at', 'desc')
      .orderBy('g.id')
      .limit(limit + 1)
      .offset(offset)
      .execute();
    const items: AdminCrashGroup[] = rows.slice(0, limit).map((row) => ({
      id: row.id,
      signature: row.signature,
      firstVersion: row.first_version,
      lastVersion: row.last_version,
      firstSeenAt: row.first_seen_at.toISOString(),
      lastSeenAt: row.last_seen_at.toISOString(),
      reports: Number(row.reports),
      installs: Number(row.installs),
      platforms: row.platforms,
      distributions: row.distributions,
      state: row.state,
      ...(row.fixed_in ? { fixedIn: row.fixed_in } : {}),
      ...(row.github_issue && row.github_issue_url
        ? { githubIssue: { number: row.github_issue, url: row.github_issue_url } }
        : {}),
    }));
    return { items, ...(rows.length > limit ? { nextCursor: `o${offset + limit}` } : {}) };
  });

  app.get<{ Params: { id: string } }>(
    '/api/v1/admin/crash-reports/:id/bundle',
    async (r, reply) => {
      const { account } = await requireRole(identity, r, 'admin');
      const { id, stream } = await load(r.params.id);
      await audit(account.id, id, 'bundle');
      return reply
        .header('content-type', 'application/zip')
        .header('content-disposition', `attachment; filename="glob2-report-${id}.zip"`)
        .header('x-content-type-options', 'nosniff')
        .send(stream);
    },
  );

  app.get<{ Params: { id: string; name: string } }>(
    '/api/v1/admin/crash-reports/:id/attachments/:name',
    async (r, reply) => {
      const { account } = await requireRole(identity, r, 'admin');
      const { id, stream } = await load(r.params.id);
      const bundle = await readAll(stream);
      let data: Buffer | undefined;
      try {
        const entry = listEntries(bundle, CRASH_REPORT_MAX_BYTES).find(
          (e) => e.name === r.params.name,
        );
        data = entry ? readEntry(bundle, entry) : undefined;
      } catch (error) {
        if (!(error instanceof ZipError)) throw error;
      }
      if (!data) throw apiError('not_found', 'No such attachment.');
      await audit(account.id, id, r.params.name);
      return reply
        .header('content-type', contentType(r.params.name))
        .header('content-disposition', `attachment; filename="${id}-${r.params.name}"`)
        .header('x-content-type-options', 'nosniff')
        .send(data);
    },
  );
}
