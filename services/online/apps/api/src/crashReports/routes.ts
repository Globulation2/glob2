// Problem and crash reports from game clients (docs/multiplayer/problem-reports.md).
//
// POST /api/v1/crash-reports takes the bundle the game writes (a zip whose
// context.json is a CrashReportContext) without signing in, and answers with a
// short ID the player can quote. Admins list reports and download bundles or
// single attachments under /api/v1/admin/crash-reports.
//
// Privacy: nothing ties a report to an account, and the client address is used
// only by an in-memory limiter, never stored (the shared Postgres limiter keys
// on the random install ID instead).
import { randomInt, randomUUID } from 'node:crypto';
import type { FastifyInstance } from 'fastify';
import { sql } from 'kysely';
import {
  CrashReportContext,
  type AdminCrashReport,
  type AdminCrashReportList,
  type CrashReportReceipt,
} from '@glob2/protocol';
import { requireRole, type Identity } from '../identity.ts';
import { apiError } from '../errors.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { decodeCursor, encodeCursor, pageSize } from '../admin/cursor.ts';
import { cursorTimeSql } from '../http/cursorTime.ts';
import { listEntries, readEntry, ZipError } from './zip.ts';

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
            const inserted = await db
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
              })
              .onConflict((oc) => oc.doNothing())
              .returning('id')
              .executeTakeFirst();
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

  app.get<{ Querystring: { cursor?: string; limit?: string } }>(
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
      const rows = await query.execute();
      const items: AdminCrashReport[] = rows.slice(0, limit).map((row) => ({
        id: row.id,
        kind: row.kind,
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
      }));
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
