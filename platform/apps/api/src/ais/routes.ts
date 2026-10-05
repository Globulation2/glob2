import { sql, type Selectable } from 'kysely';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import type { Account, AisTable, AiVersionsTable } from '@glob2/db';
import { putContent, ensureAiValidation } from '@glob2/core';
import { insertBlob } from '@glob2/play';
import {
  AI_TAGS,
  AI_VALIDATION_SUITE,
  PublishAiRequest,
  UpdateAiRequest,
  MapReportRequest,
  MapHideRequest,
  ResolveMapReportRequest,
  passedAiReport,
  parseSimVersionKey,
  type AiInfo,
  type AiVersion,
  type AiUpload,
  type AiDetail,
  type AiList,
} from '@glob2/protocol';
import { authenticate, requireAccount, requireRole, type Identity } from '../identity.ts';
import { apiError } from '../errors.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { CATALOG_RULES, UUID, canModerate } from '../maps/catalog.ts';

type AiRow = Selectable<AisTable>;
export async function aiLibraryRoutes(app: FastifyInstance, identity: Identity) {
  const { db, blobs } = app.services;
  const uploads = new SharedLimit(db, 'ai-upload', CATALOG_RULES.versionsPerHour, 3600000);
  const publishes = new SharedLimit(db, 'ai-publish', CATALOG_RULES.mapsPerHour, 3600000);
  const reports = new SharedLimit(db, 'ai-report', CATALOG_RULES.reportsPerHour, 3600000);
  const caller = async (r: FastifyRequest) => (await authenticate(identity, r))?.account;
  const signed = async (r: FastifyRequest) => (await requireAccount(identity, r)).account;
  const moderator = (a?: Account) => canModerate(a ? { account: a } : undefined);
  const uuid = (id: string) => {
    if (!UUID.test(id)) throw apiError('not_found', 'Not found.');
    return id;
  };
  async function visible(id: string, a?: Account, own = false) {
    const row = await db
      .selectFrom('ais')
      .selectAll()
      .where('id', '=', uuid(id))
      .executeTakeFirst();
    if (
      !row ||
      (row.owner_account_id !== a?.id &&
        (own || (!moderator(a) && (row.hidden || row.visibility === 'private'))))
    )
      throw apiError('not_found', 'No such AI.');
    return row;
  }
  async function versionView(v: Selectable<AiVersionsTable>): Promise<AiVersion> {
    const validations = await db
      .selectFrom('ai_validations')
      .select('report')
      .where('hash', '=', v.hash)
      .where('status', 'in', ['valid', 'invalid'])
      .orderBy('created_at', 'desc')
      .execute();
    const downloads = await db
      .selectFrom('ai_downloads')
      .select(sql<number>`count(*)::int`.as('n'))
      .where('version_id', '=', v.id)
      .executeTakeFirstOrThrow();
    return {
      id: v.id,
      hash: v.hash,
      label: v.label,
      notes: v.notes,
      profile: v.profile,
      createdAt: v.created_at.toISOString(),
      downloads: downloads.n,
      downloadUrl: `${app.services.config.publicOrigin}/api/v1/ais/${v.ai_id}/versions/${v.id}/file`,
      validations: validations.map((r) => r.report),
    };
  }
  async function info(row: AiRow, a?: Account): Promise<AiInfo> {
    const owner = await db
      .selectFrom('accounts')
      .select(['id', 'display_name'])
      .where('id', '=', row.owner_account_id)
      .executeTakeFirstOrThrow();
    const latest = await db
      .selectFrom('ai_versions')
      .selectAll()
      .where('ai_id', '=', row.id)
      .orderBy('created_at', 'desc')
      .orderBy('id', 'desc')
      .executeTakeFirstOrThrow();
    const likes = await db
      .selectFrom('ai_likes')
      .select(sql<number>`count(*)::int`.as('n'))
      .where('ai_id', '=', row.id)
      .executeTakeFirstOrThrow();
    const downloads = await db
      .selectFrom('ai_downloads as d')
      .innerJoin('ai_versions as v', 'v.id', 'd.version_id')
      .select(sql<number>`count(*)::int`.as('n'))
      .where('v.ai_id', '=', row.id)
      .executeTakeFirstOrThrow();
    const liked =
      !!a &&
      !!(await db
        .selectFrom('ai_likes')
        .select('ai_id')
        .where('ai_id', '=', row.id)
        .where('account_id', '=', a.id)
        .executeTakeFirst());
    const favourited =
      !!a &&
      !!(await db
        .selectFrom('ai_favourites')
        .select('ai_id')
        .where('ai_id', '=', row.id)
        .where('account_id', '=', a.id)
        .executeTakeFirst());
    return {
      id: row.id,
      name: row.name,
      description: row.description,
      tags: row.tags as AiInfo['tags'],
      visibility: row.visibility,
      hidden: row.hidden,
      ...(row.hidden_reason ? { hiddenReason: row.hidden_reason } : {}),
      owner: { id: owner.id, displayName: owner.display_name },
      createdAt: row.created_at.toISOString(),
      updatedAt: row.updated_at.toISOString(),
      likes: likes.n,
      downloads: downloads.n,
      latestVersion: await versionView(latest),
      liked,
      favourited,
    };
  }
  app.get<{
    Querystring: {
      q?: string;
      tags?: string;
      sort?: string;
      owner?: string;
      favourites?: string;
      cursor?: string;
      limit?: string;
    };
  }>('/api/v1/ais', async (r): Promise<AiList> => {
    const a = await caller(r),
      q = r.query;
    if ((q.owner === 'me' || q.favourites === 'true') && !a)
      throw apiError('unauthenticated', 'Sign in to see your library.');
    const sort = q.sort ?? 'likes';
    if (!['likes', 'newest', 'updated', 'downloads'].includes(sort))
      throw apiError('bad_request', 'Unknown sort.');
    const tags = (q.tags ?? '').split(',').filter(Boolean);
    if (tags.length > 5 || tags.some((t) => !(AI_TAGS as readonly string[]).includes(t)))
      throw apiError('bad_request', 'Unknown AI tag.');
    const limit = Number(q.limit ?? 24);
    if (!Number.isInteger(limit) || limit < 1 || limit > 100)
      throw apiError('bad_request', 'Invalid page size.');
    const ranking =
      sort === 'likes'
        ? sql<string>`(SELECT count(*) FROM ai_likes l WHERE l.ai_id=a.id)`
        : sort === 'downloads'
          ? sql<string>`(SELECT count(*) FROM ai_downloads d JOIN ai_versions v ON v.id=d.version_id WHERE v.ai_id=a.id)`
          : sql<string>`extract(epoch from ${sql.ref(sort === 'newest' ? 'a.created_at' : 'a.updated_at')})`;
    let query = db
      .selectFrom('ais as a')
      .innerJoin('accounts as o', 'o.id', 'a.owner_account_id')
      .selectAll('a')
      .select(ranking.as('rank'));
    if (q.owner === 'me' && a) query = query.where('a.owner_account_id', '=', a.id);
    else if (q.favourites === 'true' && a) {
      if (!moderator(a))
        query = query.where(
          sql<boolean>`(a.owner_account_id=${a.id} OR (NOT a.hidden AND a.visibility <> 'private'))`,
        );
    } else query = query.where('a.visibility', '=', 'public').where('a.hidden', '=', false);
    if (q.favourites === 'true' && a)
      query = query.where((eb) =>
        eb.exists(
          eb
            .selectFrom('ai_favourites as f')
            .select('f.ai_id')
            .whereRef('f.ai_id', '=', 'a.id')
            .where('f.account_id', '=', a.id),
        ),
      );
    if (q.q)
      query = query.where(
        sql<boolean>`concat_ws(' ',a.name,a.description,o.display_name) ILIKE ${'%' + q.q.slice(0, 128).replace(/[\\%_]/g, '\\$&') + '%'}`,
      );
    for (const tag of tags) query = query.where(sql<boolean>`${tag}=ANY(a.tags)`);
    if (q.cursor) {
      try {
        const c = JSON.parse(Buffer.from(q.cursor, 'base64url').toString()) as {
          sort: string;
          rank: string;
          id: string;
        };
        if (c.sort !== sort || !UUID.test(c.id) || !/^\d+(\.\d+)?$/.test(c.rank)) throw Error();
        query = query.where(sql<boolean>`(${ranking},a.id)<(${c.rank}::numeric,${c.id}::uuid)`);
      } catch {
        throw apiError('bad_request', 'Invalid cursor.');
      }
    }
    const rows = await query
      .orderBy('rank', 'desc')
      .orderBy('a.id', 'desc')
      .limit(limit + 1)
      .execute();
    const page = rows.slice(0, limit),
      last = page.at(-1);
    return {
      items: await Promise.all(page.map((row) => info(row, a))),
      ...(rows.length > limit && last
        ? {
            nextCursor: Buffer.from(
              JSON.stringify({ sort, rank: String(last.rank), id: last.id }),
            ).toString('base64url'),
          }
        : {}),
    };
  });
  app.get<{ Params: { id: string } }>('/api/v1/ais/:id', async (r): Promise<AiDetail> => {
    const a = await caller(r),
      row = await visible(r.params.id, a);
    const versions = await db
      .selectFrom('ai_versions')
      .selectAll()
      .where('ai_id', '=', row.id)
      .orderBy('created_at', 'desc')
      .orderBy('id', 'desc')
      .execute();
    return {
      ai: await info(row, a),
      versions: await Promise.all(versions.map(versionView)),
      viewer: { owner: row.owner_account_id === a?.id, moderator: moderator(a) },
    };
  });
  async function uploadView(id: string, a: Account): Promise<AiUpload> {
    const row = await db
      .selectFrom('ai_uploads as u')
      .innerJoin('ai_validations as v', 'v.id', 'u.validation_id')
      .select(['u.id', 'u.expires_at', 'v.hash', 'v.status', 'v.report', 'v.error'])
      .where('u.id', '=', uuid(id))
      .where('u.owner_account_id', '=', a.id)
      .executeTakeFirst();
    if (!row || row.expires_at < new Date())
      throw apiError('not_found', 'Upload expired; choose the file again.');
    return {
      id: row.id,
      sourceHash: row.hash,
      status: row.status,
      report: row.report,
      expiresAt: row.expires_at.toISOString(),
      ...(row.error ? { error: row.error } : {}),
    };
  }
  app.post('/api/v1/ai-uploads', { bodyLimit: 128 * 1024 }, async (r, reply) => {
    const a = await signed(r);
    await enforce(uploads, a.id, reply, 'Too many uploads; wait a while.');
    if (!Buffer.isBuffer(r.body) || !r.body.length || r.body.includes(0))
      throw apiError('bad_request', 'Choose a nonempty JavaScript file without NUL bytes.');
    try {
      new TextDecoder('utf-8', { fatal: true }).decode(r.body);
    } catch {
      throw apiError('bad_request', 'The JavaScript file must use UTF-8.');
    }
    const agents = await db
      .selectFrom('engine_agents')
      .select('sim_version')
      .where('last_seen_at', '>', new Date(Date.now() - 120000))
      .where(sql<boolean>`'validate-ai'=ANY(kinds)`)
      .orderBy('last_seen_at', 'desc')
      .execute();
    const sim = agents
      .map((x) => parseSimVersionKey(x.sim_version))
      .filter((x) => !!x)
      .sort((a, b) => b.versionMinor - a.versionMinor || b.netProtocol - a.netProtocol)[0];
    if (!sim)
      throw apiError(
        'unavailable',
        'No isolated AI validator is available. Please try again later.',
      );
    const stored = await putContent(blobs, r.body);
    const id = await db.transaction().execute(async (trx) => {
      await insertBlob(trx, stored.sha256, stored.size, 'application/javascript', 'private', a.id);
      const validation_id = await ensureAiValidation(trx, stored.sha256, sim, true);
      return (
        await trx
          .insertInto('ai_uploads')
          .values({ owner_account_id: a.id, validation_id })
          .returning('id')
          .executeTakeFirstOrThrow()
      ).id;
    });
    return reply.status(201).send(await uploadView(id, a));
  });
  app.get<{ Params: { id: string } }>('/api/v1/ai-uploads/:id', async (r) =>
    uploadView(r.params.id, await signed(r)),
  );
  async function publish(r: FastifyRequest, aiId?: string) {
    const a = await signed(r),
      input = body(PublishAiRequest, r.body);
    if (a.kind === 'guest' && input.visibility === 'public')
      throw apiError('forbidden', 'Sign in with an account to publish publicly.');
    await enforce(publishes, a.id, undefined, 'Too many publications; wait a while.');
    const id = await db.transaction().execute(async (trx) => {
      const u = await trx
        .selectFrom('ai_uploads')
        .selectAll()
        .where('id', '=', input.uploadId)
        .where('owner_account_id', '=', a.id)
        .forUpdate()
        .executeTakeFirst();
      if (!u || u.expires_at < new Date())
        throw apiError('not_found', 'Upload expired; validate again.');
      if (u.published_ai_id) {
        if (aiId && aiId !== u.published_ai_id)
          throw apiError('conflict', 'Upload already published elsewhere.');
        return u.published_ai_id;
      }
      const v = await trx
        .selectFrom('ai_validations')
        .selectAll()
        .where('id', '=', u.validation_id)
        .executeTakeFirstOrThrow();
      if (
        v.status !== 'valid' ||
        !passedAiReport(v.report) ||
        !v.report.metadata ||
        v.report.sourceHash !== v.hash ||
        v.report.simVersion !== v.sim_version ||
        v.suite !== AI_VALIDATION_SUITE
      )
        throw apiError('conflict', 'All compatibility checks must pass before publishing.');
      let id = aiId;
      if (id) {
        const row = await trx
          .selectFrom('ais')
          .selectAll()
          .where('id', '=', uuid(id))
          .forUpdate()
          .executeTakeFirst();
        if (!row || row.owner_account_id !== a.id) throw apiError('not_found', 'No such AI.');
        const n = await trx
          .selectFrom('ai_versions')
          .select(sql<number>`count(*)::int`.as('n'))
          .where('ai_id', '=', id)
          .executeTakeFirstOrThrow();
        if (n.n >= CATALOG_RULES.maxVersionsPerMap)
          throw apiError('conflict', 'This AI has reached its version limit.');
      } else
        id = (
          await trx
            .insertInto('ais')
            .values({
              owner_account_id: a.id,
              name: input.name.trim(),
              description: input.description,
              tags: input.tags,
              visibility: input.visibility,
            })
            .returning('id')
            .executeTakeFirstOrThrow()
        ).id;
      const version = await trx
        .insertInto('ai_versions')
        .values({
          ai_id: id,
          hash: v.hash,
          label: input.version.trim(),
          notes: input.notes,
          profile: v.report.metadata.apiVersion,
        })
        .onConflict((oc) => oc.doNothing())
        .returning('id')
        .executeTakeFirst();
      if (!version)
        throw apiError(
          'conflict',
          'This source or version label has already been published for this AI.',
        );
      await trx.updateTable('ais').set({ updated_at: new Date() }).where('id', '=', id).execute();
      await trx
        .updateTable('ai_uploads')
        .set({ published_ai_id: id, published_version_id: version.id })
        .where('id', '=', u.id)
        .execute();
      return id;
    });
    return info(await visible(id, a), a);
  }
  app.post('/api/v1/ais', async (r) => publish(r));
  app.post<{ Params: { id: string } }>('/api/v1/ais/:id/versions', async (r) =>
    publish(r, r.params.id),
  );
  app.patch<{ Params: { id: string } }>('/api/v1/ais/:id', async (r) => {
    const a = await signed(r),
      row = await visible(r.params.id, a, true),
      input = body(UpdateAiRequest, r.body);
    if (input.visibility === 'public' && a.kind === 'guest')
      throw apiError('forbidden', 'An account is required to publish publicly.');
    await db
      .updateTable('ais')
      .set({ ...input, updated_at: new Date() })
      .where('id', '=', row.id)
      .execute();
    return info(await visible(row.id, a), a);
  });
  app.delete<{ Params: { id: string } }>('/api/v1/ais/:id', async (r, reply) => {
    const a = await signed(r),
      row = await visible(r.params.id, a, true);
    await db.deleteFrom('ais').where('id', '=', row.id).execute();
    return reply.status(204).send();
  });
  app.get<{ Params: { id: string; version: string } }>(
    '/api/v1/ais/:id/versions/:version/file',
    async (r, reply) => {
      const a = await caller(r),
        row = await visible(r.params.id, a);
      const v = await db
        .selectFrom('ai_versions')
        .selectAll()
        .where('ai_id', '=', row.id)
        .where('id', '=', uuid(r.params.version))
        .executeTakeFirst();
      if (!v) throw apiError('not_found', 'No such version.');
      const blob = await db
          .selectFrom('blobs')
          .select('storage_key')
          .where('sha256', '=', v.hash)
          .executeTakeFirstOrThrow(),
        stream = await blobs.get(blob.storage_key);
      if (!stream) throw apiError('not_found', 'The file is missing.');
      if (a?.id !== row.owner_account_id)
        await db
          .insertInto('ai_downloads')
          .values({ version_id: v.id, downloader: a ? `a:${a.id}` : `ip:${r.ip}` })
          .onConflict((oc) => oc.doNothing())
          .execute();
      const name = (row.name + '-' + v.label).replace(/[^A-Za-z0-9._-]/g, '_').slice(0, 100);
      return reply
        .header('content-type', 'application/octet-stream')
        .header('content-disposition', `attachment; filename="${name}.js"`)
        .header('x-content-type-options', 'nosniff')
        .header('cache-control', 'private, no-store')
        .header('etag', `"${v.hash}"`)
        .send(stream);
    },
  );
  for (const action of ['like', 'favourite'] as const)
    for (const method of ['PUT', 'DELETE'] as const)
      app.route<{ Params: { id: string } }>({
        method,
        url: `/api/v1/ais/:id/${action}`,
        config: { rateLimit: { max: 60, timeWindow: 60000 } },
        handler: async (r) => {
          const a = await signed(r);
          if (a.kind === 'guest')
            throw apiError('forbidden', 'Sign in with an account to save favourites and like AIs.');
          const row = await visible(r.params.id, a),
            table = action === 'like' ? 'ai_likes' : 'ai_favourites';
          if (method === 'PUT')
            await db
              .insertInto(table)
              .values({ ai_id: row.id, account_id: a.id })
              .onConflict((oc) => oc.doNothing())
              .execute();
          else
            await db
              .deleteFrom(table)
              .where('ai_id', '=', row.id)
              .where('account_id', '=', a.id)
              .execute();
          const count = await db
            .selectFrom('ai_likes')
            .select(sql<number>`count(*)::int`.as('n'))
            .where('ai_id', '=', row.id)
            .executeTakeFirstOrThrow();
          return { active: method === 'PUT', likes: count.n };
        },
      });
  app.post<{ Params: { id: string } }>('/api/v1/ais/:id/reports', async (r) => {
    const a = await signed(r),
      row = await visible(r.params.id, a),
      input = body(MapReportRequest, r.body);
    await enforce(reports, a.id, undefined, 'Too many reports; wait a while.');
    await db
      .insertInto('ai_reports')
      .values({
        ai_id: row.id,
        reporter_account_id: a.id,
        reason: input.reason,
        details: input.details,
      })
      .onConflict((oc) => oc.doNothing())
      .execute();
    return { status: 'open' };
  });
  async function audit(a: Account, action: string, id: string, details: object) {
    await db
      .insertInto('admin_audit_log')
      .values({
        actor_account_id: a.id,
        action,
        target_type: 'ai',
        target_id: id,
        details: JSON.stringify(details),
      })
      .execute();
  }
  for (const hidden of [true, false])
    app.post<{ Params: { id: string } }>(
      `/api/v1/admin/ais/:id/${hidden ? 'hide' : 'unhide'}`,
      async (r) => {
        const a = (await requireRole(identity, r, 'moderator')).account,
          row = await visible(r.params.id, a),
          reason = hidden ? body(MapHideRequest, r.body).reason : null;
        await db
          .updateTable('ais')
          .set({ hidden, hidden_reason: reason })
          .where('id', '=', row.id)
          .execute();
        await audit(a, hidden ? 'ai.hide' : 'ai.unhide', row.id, { reason });
        return info(await visible(row.id, a), a);
      },
    );
  app.get('/api/v1/admin/ai-reports', async (r) => {
    await requireRole(identity, r, 'moderator');
    return {
      items: await db
        .selectFrom('ai_reports')
        .selectAll()
        .where('status', '=', 'open')
        .orderBy('created_at', 'asc')
        .limit(100)
        .execute(),
    };
  });
  app.post<{ Params: { id: string } }>('/api/v1/admin/ai-reports/:id/resolve', async (r) => {
    const a = (await requireRole(identity, r, 'moderator')).account,
      input = body(ResolveMapReportRequest, r.body);
    const row = await db
      .updateTable('ai_reports')
      .set({ status: input.status, resolution_note: input.note ?? null })
      .where('id', '=', uuid(r.params.id))
      .returningAll()
      .executeTakeFirst();
    if (!row) throw apiError('not_found', 'No such report.');
    if (input.hideMap)
      await db
        .updateTable('ais')
        .set({ hidden: true, hidden_reason: input.hideReason ?? input.note ?? row.reason })
        .where('id', '=', row.ai_id)
        .execute();
    await audit(a, 'ai.report.' + input.status, row.ai_id, { report: row.id });
    return row;
  });
}
