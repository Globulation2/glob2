import { resolveReport } from '../admin/moderation.ts';
import { sql } from 'kysely';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import type { Account } from '@glob2/db';
import { putContent, ensureGeneratorValidation } from '@glob2/core';
import { insertBlob } from '@glob2/play';
import {
  GENERATOR_VALIDATION_SUITE,
  GeneratorSettings,
  PublishGeneratorRequest,
  UpdateGeneratorRequest,
  MapReportRequest,
  MapHideRequest,
  ResolveMapReportRequest,
  passedGeneratorReport,
  parseSimVersionKey,
  type GeneratorUpload,
  type GeneratorDetail,
  type GeneratorList,
} from '@glob2/protocol';
import { authenticate, requireAccount, requireRole, type Identity } from '../identity.ts';
import { apiError } from '../errors.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { CATALOG_RULES, UUID, canModerate } from '../maps/catalog.ts';

import { generatorCatalogViews } from './catalogViews.ts';
export async function generatorLibraryRoutes(app: FastifyInstance, identity: Identity) {
  const { db, blobs } = app.services;
  const uploads = new SharedLimit(db, 'generator-upload', CATALOG_RULES.versionsPerHour, 3600000);
  const publishes = new SharedLimit(db, 'generator-publish', CATALOG_RULES.mapsPerHour, 3600000);
  const reports = new SharedLimit(db, 'generator-report', CATALOG_RULES.reportsPerHour, 3600000);
  const caller = async (r: FastifyRequest) => (await authenticate(identity, r))?.account;
  const signed = async (r: FastifyRequest) => (await requireAccount(identity, r)).account;
  const moderator = (a?: Account) => canModerate(a ? { account: a } : undefined);
  const uuid = (id: string) => {
    if (!UUID.test(id)) throw apiError('not_found', 'Not found.');
    return id;
  };
  async function visible(id: string, a?: Account, own = false) {
    const row = await db
      .selectFrom('generators')
      .selectAll()
      .where('id', '=', uuid(id))
      .where('deleted_at', 'is', null)
      .executeTakeFirst();
    if (
      !row ||
      (row.owner_account_id !== a?.id &&
        (own || (!moderator(a) && (row.hidden || row.visibility === 'private'))))
    )
      throw apiError('not_found', 'No such generator.');
    return row;
  }
  const {
    info,
    infos,
    versions: versionViews,
  } = generatorCatalogViews(db, app.services.config.publicOrigin);
  app.get<{
    Querystring: {
      q?: string;
      tags?: string;
      sort?: string;
      owner?: string;
      favourites?: string;
      cursor?: string;
      limit?: string;
      editorOnly?: string;
      simVersion?: string;
    };
  }>('/api/v1/generators', async (r): Promise<GeneratorList> => {
    const a = await caller(r),
      q = r.query;
    if ((q.owner === 'me' || q.favourites === 'true') && !a)
      throw apiError('unauthenticated', 'Sign in to see your library.');
    const sort = q.sort ?? 'likes';
    if (!['likes', 'newest', 'updated', 'downloads'].includes(sort))
      throw apiError('bad_request', 'Unknown sort.');
    const tags = (q.tags ?? '').split(',').filter(Boolean);
    if (tags.length > 16 || tags.some((t) => t.length > 128 || !t.includes(':')))
      throw apiError('bad_request', 'Unknown generator tag.');
    const limit = Number(q.limit ?? 24);
    if (!Number.isInteger(limit) || limit < 1 || limit > 100)
      throw apiError('bad_request', 'Invalid page size.');
    const ranking =
      sort === 'likes'
        ? sql<string>`(SELECT count(*) FROM generator_likes l WHERE l.generator_id=a.id)`
        : sort === 'downloads'
          ? sql<string>`(SELECT count(*) FROM generator_downloads d JOIN generator_versions v ON v.id=d.version_id WHERE v.generator_id=a.id)`
          : sql<string>`extract(epoch from ${sql.ref(sort === 'newest' ? 'a.created_at' : 'a.updated_at')})`;
    let query = db
      .selectFrom('generators as a')
      .innerJoin('accounts as o', 'o.id', 'a.owner_account_id')
      .where('a.deleted_at', 'is', null)
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
            .selectFrom('generator_favourites as f')
            .select('f.generator_id')
            .whereRef('f.generator_id', '=', 'a.id')
            .where('f.account_id', '=', a.id),
        ),
      );
    if (q.q)
      query = query.where(
        sql<boolean>`concat_ws(' ',a.name,a.description,o.display_name) ILIKE ${'%' + q.q.slice(0, 128).replace(/[\\%_]/g, '\\$&') + '%'}`,
      );
    for (const tag of tags) query = query.where(sql<boolean>`${tag}=ANY(a.tags)`);
    if (q.editorOnly && !['true', 'false'].includes(q.editorOnly))
      throw apiError('bad_request', 'Invalid editor-only filter.');
    if (q.simVersion && !parseSimVersionKey(q.simVersion))
      throw apiError('bad_request', 'Invalid engine version.');
    if (q.editorOnly || q.simVersion)
      query = query.where(
        sql<boolean>`EXISTS (SELECT 1 FROM generator_versions v WHERE v.generator_id=a.id AND v.id=(SELECT id FROM generator_versions WHERE generator_id=a.id ORDER BY revision DESC LIMIT 1) ${q.editorOnly ? sql`AND (v.metadata->>'editorOnly')::boolean=${q.editorOnly === 'true'}` : sql``} ${q.simVersion ? sql`AND EXISTS (SELECT 1 FROM generator_validations c WHERE c.hash=v.source_hash AND c.example=v.example AND c.sim_version=${q.simVersion} AND c.status='valid' AND c.suite=${GENERATOR_VALIDATION_SUITE})` : sql``})`,
      );
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
      items: await infos(page, a),
      ...(rows.length > limit && last
        ? {
            nextCursor: Buffer.from(
              JSON.stringify({ sort, rank: String(last.rank), id: last.id }),
            ).toString('base64url'),
          }
        : {}),
    };
  });
  app.get<{ Params: { id: string } }>(
    '/api/v1/generators/:id',
    async (r): Promise<GeneratorDetail> => {
      const a = await caller(r),
        row = await visible(r.params.id, a);
      const versions = await db
        .selectFrom('generator_versions')
        .selectAll()
        .where('generator_id', '=', row.id)
        .orderBy('created_at', 'desc')
        .orderBy('id', 'desc')
        .execute();
      return {
        generator: await info(row, a),
        versions: await versionViews(versions),
        viewer: { owner: row.owner_account_id === a?.id, moderator: moderator(a) },
      };
    },
  );
  async function uploadView(id: string, a: Account): Promise<GeneratorUpload> {
    const row = await db
      .selectFrom('generator_uploads as u')
      .innerJoin('generator_validations as v', 'v.id', 'u.validation_id')
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
  app.post<{ Querystring: { example: string } }>(
    '/api/v1/generator-uploads',
    { bodyLimit: 4 * 1024 * 1024 },
    async (r, reply) => {
      const a = await signed(r);
      await enforce(uploads, a.id, reply, 'Too many uploads; wait a while.');
      if (!Buffer.isBuffer(r.body) || !r.body.length || r.body.includes(0))
        throw apiError(
          'bad_request',
          'Choose a nonempty portable generator package without NUL bytes.',
        );
      try {
        new TextDecoder('utf-8', { fatal: true }).decode(r.body);
      } catch {
        throw apiError('bad_request', 'The generator package must use UTF-8.');
      }
      const agents = await db
        .selectFrom('engine_agents')
        .select('sim_version')
        .where('last_seen_at', '>', new Date(Date.now() - 120000))
        .where(sql<boolean>`'validate-generator'=ANY(kinds)`)
        .orderBy('last_seen_at', 'desc')
        .execute();
      const sim = agents
        .map((x) => parseSimVersionKey(x.sim_version))
        .filter((x) => !!x)
        .sort((a, b) => b.versionMinor - a.versionMinor || b.netProtocol - a.netProtocol)[0];
      if (!sim)
        throw apiError(
          'unavailable',
          'No isolated generator validator is available. Please try again later.',
        );
      let example;
      try {
        example = body(GeneratorSettings, JSON.parse(r.query.example));
      } catch {
        throw apiError('bad_request', 'Supply valid example settings.');
      }
      const stored = await putContent(blobs, r.body);
      const id = await db.transaction().execute(async (trx) => {
        const owner = await trx
          .selectFrom('accounts')
          .select('status')
          .where('id', '=', a.id)
          .forUpdate()
          .executeTakeFirst();
        if (owner?.status !== 'active')
          throw apiError('forbidden', 'This account cannot upload generators.');
        await insertBlob(
          trx,
          stored.sha256,
          stored.size,
          'application/x-glob2-generator',
          'private',
          a.id,
        );
        const validation_id = await ensureGeneratorValidation(
          trx,
          stored.sha256,
          sim,
          example,
          true,
        );
        return (
          await trx
            .insertInto('generator_uploads')
            .values({ owner_account_id: a.id, validation_id })
            .returning('id')
            .executeTakeFirstOrThrow()
        ).id;
      });
      return reply.status(201).send(await uploadView(id, a));
    },
  );
  app.get<{ Params: { id: string } }>('/api/v1/generator-uploads/:id', async (r) =>
    uploadView(r.params.id, await signed(r)),
  );
  async function publish(r: FastifyRequest, generatorId?: string) {
    const a = await signed(r),
      input = body(PublishGeneratorRequest, r.body);
    if (a.kind === 'guest' && input.visibility === 'public')
      throw apiError('forbidden', 'Sign in with an account to publish publicly.');
    await enforce(publishes, a.id, undefined, 'Too many publications; wait a while.');
    const id = await db.transaction().execute(async (trx) => {
      const owner = await trx
        .selectFrom('accounts')
        .select('status')
        .where('id', '=', a.id)
        .forUpdate()
        .executeTakeFirst();
      if (owner?.status !== 'active')
        throw apiError('forbidden', 'This account cannot publish generators.');
      const u = await trx
        .selectFrom('generator_uploads')
        .selectAll()
        .where('id', '=', input.uploadId)
        .where('owner_account_id', '=', a.id)
        .forUpdate()
        .executeTakeFirst();
      if (!u || u.expires_at < new Date())
        throw apiError('not_found', 'Upload expired; validate again.');
      if (u.published_generator_id) {
        if (generatorId && generatorId !== u.published_generator_id)
          throw apiError('conflict', 'Upload already published elsewhere.');
        return u.published_generator_id;
      }
      const v = await trx
        .selectFrom('generator_validations')
        .selectAll()
        .where('id', '=', u.validation_id)
        .executeTakeFirstOrThrow();
      const fileHash = v.report.fileHash,
        packageHash = v.report.packageHash;
      if (
        !fileHash ||
        !packageHash ||
        v.status !== 'valid' ||
        !passedGeneratorReport(v.report) ||
        !v.report.metadata ||
        v.report.sourceHash !== v.hash ||
        v.report.simVersion !== v.sim_version ||
        v.suite !== GENERATOR_VALIDATION_SUITE
      )
        throw apiError('conflict', 'All compatibility checks must pass before publishing.');
      let id = generatorId;
      if (id) {
        const row = await trx
          .selectFrom('generators')
          .selectAll()
          .where('id', '=', uuid(id))
          .where('deleted_at', 'is', null)
          .forUpdate()
          .executeTakeFirst();
        if (!row || row.owner_account_id !== a.id)
          throw apiError('not_found', 'No such generator.');
        const claim = await trx
          .selectFrom('generator_ids')
          .select('manifest_id')
          .where('generator_id', '=', id)
          .executeTakeFirstOrThrow();
        if (claim.manifest_id !== v.report.metadata.id)
          throw apiError('conflict', 'A release must retain its package ID.');
        const latest = await trx
          .selectFrom('generator_versions')
          .select('revision')
          .where('generator_id', '=', id)
          .orderBy('revision', 'desc')
          .executeTakeFirst();
        if (latest && Number(latest.revision) >= v.report.metadata.revision)
          throw apiError('conflict', 'Increase the manifest revision for a new release.');
        const n = await trx
          .selectFrom('generator_versions')
          .select(sql<number>`count(*)::int`.as('n'))
          .where('generator_id', '=', id)
          .executeTakeFirstOrThrow();
        if (n.n >= CATALOG_RULES.maxVersionsPerMap)
          throw apiError('conflict', 'This generator has reached its version limit.');
      } else
        id = (
          await trx
            .insertInto('generators')
            .values({
              owner_account_id: a.id,
              name: input.name.trim(),
              description: input.description,
              tags: v.report.metadata.tags,
              visibility: input.visibility,
            })
            .returning('id')
            .executeTakeFirstOrThrow()
        ).id;
      if (!generatorId) {
        const claim = await trx
          .insertInto('generator_ids')
          .values({ manifest_id: v.report.metadata.id, generator_id: id })
          .onConflict((oc) => oc.doNothing())
          .returning('manifest_id')
          .executeTakeFirst();
        if (!claim)
          throw apiError('conflict', 'This package ID is already reserved. Forks need a new ID.');
      }
      const version = await trx
        .insertInto('generator_versions')
        .values({
          generator_id: id,
          hash: fileHash,
          label: input.version.trim(),
          notes: input.notes,
          profile: v.report.metadata.apiVersion,
          source_hash: v.hash,
          package_hash: packageHash,
          metadata: JSON.stringify(v.report.metadata),
          example: JSON.stringify(v.example),
          revision: v.report.metadata.revision,
        })
        .onConflict((oc) => oc.doNothing())
        .returning('id')
        .executeTakeFirst();
      if (!version)
        throw apiError(
          'conflict',
          'This source or version label has already been published for this generator.',
        );
      await trx
        .updateTable('generators')
        .set({ updated_at: new Date(), tags: v.report.metadata.tags })
        .where('id', '=', id)
        .execute();
      await trx
        .updateTable('generator_uploads')
        .set({ published_generator_id: id, published_version_id: version.id })
        .where('id', '=', u.id)
        .execute();
      return id;
    });
    return info(await visible(id, a), a);
  }
  app.post('/api/v1/generators', async (r) => publish(r));
  app.post<{ Params: { id: string } }>('/api/v1/generators/:id/versions', async (r) =>
    publish(r, r.params.id),
  );
  app.patch<{ Params: { id: string } }>('/api/v1/generators/:id', async (r) => {
    const a = await signed(r),
      row = await visible(r.params.id, a, true),
      input = body(UpdateGeneratorRequest, r.body);
    if (input.visibility === 'public' && a.kind === 'guest')
      throw apiError('forbidden', 'An account is required to publish publicly.');
    await db
      .updateTable('generators')
      .set({ ...input, updated_at: new Date() })
      .where('id', '=', row.id)
      .execute();
    return info(await visible(row.id, a), a);
  });
  app.delete<{ Params: { id: string } }>('/api/v1/generators/:id', async (r, reply) => {
    const a = await signed(r),
      row = await visible(r.params.id, a, true);
    await db
      .updateTable('generators')
      .set({ deleted_at: new Date(), visibility: 'private' })
      .where('id', '=', row.id)
      .execute();
    return reply.status(204).send();
  });
  app.get<{ Params: { id: string; version: string } }>(
    '/api/v1/generators/:id/versions/:version',
    async (r) => {
      const account = await caller(r),
        library = await visible(r.params.id, account);
      const release = await db
        .selectFrom('generator_versions')
        .selectAll()
        .where('generator_id', '=', library.id)
        .where('id', '=', uuid(r.params.version))
        .executeTakeFirst();
      if (!release) throw apiError('not_found', 'No such release.');
      return (await versionViews([release]))[0];
    },
  );
  app.get<{ Params: { id: string; version: string } }>(
    '/api/v1/generators/:id/versions/:version/file',
    async (r, reply) => {
      const a = await caller(r),
        row = await visible(r.params.id, a);
      const v = await db
        .selectFrom('generator_versions')
        .selectAll()
        .where('generator_id', '=', row.id)
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
          .insertInto('generator_downloads')
          .values({ version_id: v.id, downloader: a ? `a:${a.id}` : `ip:${r.ip}` })
          .onConflict((oc) => oc.doNothing())
          .execute();
      const name = (row.name + '-' + v.label).replace(/[^A-Za-z0-9._-]/g, '_').slice(0, 100);
      return reply
        .header('content-type', 'application/octet-stream')
        .header('content-disposition', `attachment; filename="${name}.json"`)
        .header('x-content-type-options', 'nosniff')
        .header('cache-control', 'private, no-store')
        .header('etag', `"${v.hash}"`)
        .send(stream);
    },
  );
  app.get<{ Params: { id: string; version: string } }>(
    '/api/v1/generators/:id/versions/:version/preview.png',
    async (r, reply) => {
      const a = await caller(r),
        row = await visible(r.params.id, a);
      const version = await db
        .selectFrom('generator_versions')
        .selectAll()
        .where('generator_id', '=', row.id)
        .where('id', '=', uuid(r.params.version))
        .executeTakeFirst();
      if (!version) throw apiError('not_found', 'No such release.');
      const validation = await db
        .selectFrom('generator_validations')
        .select('report')
        .where('hash', '=', version.source_hash)
        .where(sql<boolean>`example=${JSON.stringify(version.example)}::jsonb`)
        .where('status', '=', 'valid')
        .orderBy('created_at', 'desc')
        .executeTakeFirst();
      const hash = validation?.report.previewHash;
      if (!hash) throw apiError('not_found', 'Preview unavailable.');
      const blob = await db
        .selectFrom('blobs')
        .select('storage_key')
        .where('sha256', '=', hash)
        .executeTakeFirst();
      const stream = blob ? await blobs.get(blob.storage_key) : undefined;
      if (!stream) throw apiError('not_found', 'Preview unavailable.');
      return reply
        .header('content-type', 'image/png')
        .header('cache-control', 'private, no-store')
        .send(stream);
    },
  );
  for (const action of ['like', 'favourite'] as const)
    for (const method of ['PUT', 'DELETE'] as const)
      app.route<{ Params: { id: string } }>({
        method,
        url: `/api/v1/generators/:id/${action}`,
        config: { rateLimit: { max: 60, timeWindow: 60000 } },
        handler: async (r) => {
          const a = await signed(r);
          if (a.kind === 'guest')
            throw apiError(
              'forbidden',
              'Sign in with an account to save favourites and like generators.',
            );
          const row = await visible(r.params.id, a),
            table = action === 'like' ? 'generator_likes' : 'generator_favourites';
          if (method === 'PUT')
            await db
              .insertInto(table)
              .values({ generator_id: row.id, account_id: a.id })
              .onConflict((oc) => oc.doNothing())
              .execute();
          else
            await db
              .deleteFrom(table)
              .where('generator_id', '=', row.id)
              .where('account_id', '=', a.id)
              .execute();
          const count = await db
            .selectFrom('generator_likes')
            .select(sql<number>`count(*)::int`.as('n'))
            .where('generator_id', '=', row.id)
            .executeTakeFirstOrThrow();
          return { active: method === 'PUT', likes: count.n };
        },
      });
  app.post<{ Params: { id: string } }>('/api/v1/generators/:id/reports', async (r) => {
    const a = await signed(r),
      row = await visible(r.params.id, a),
      input = body(MapReportRequest, r.body);
    await enforce(reports, a.id, undefined, 'Too many reports; wait a while.');
    await db
      .insertInto('generator_reports')
      .values({
        generator_id: row.id,
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
        target_type: 'generator',
        target_id: id,
        details: JSON.stringify(details),
      })
      .execute();
  }
  for (const hidden of [true, false])
    app.post<{ Params: { id: string } }>(
      `/api/v1/admin/generators/:id/${hidden ? 'hide' : 'unhide'}`,
      async (r) => {
        const a = (await requireRole(identity, r, 'moderator')).account,
          row = await visible(r.params.id, a),
          reason = hidden ? body(MapHideRequest, r.body).reason : null;
        await db
          .updateTable('generators')
          .set({ hidden, hidden_reason: reason })
          .where('id', '=', row.id)
          .execute();
        await audit(a, hidden ? 'generator.hide' : 'generator.unhide', row.id, { reason });
        return info(await visible(row.id, a), a);
      },
    );
  app.get('/api/v1/admin/generator-reports', async (r) => {
    await requireRole(identity, r, 'moderator');
    return {
      items: await db
        .selectFrom('generator_reports')
        .selectAll()
        .where('status', '=', 'open')
        .orderBy('created_at', 'asc')
        .limit(100)
        .execute(),
    };
  });
  app.post<{ Params: { id: string } }>('/api/v1/admin/generator-reports/:id/resolve', async (r) => {
    const a = (await requireRole(identity, r, 'moderator')).account,
      input = body(ResolveMapReportRequest, r.body);
    await resolveReport(
      db,
      'generators',
      uuid(r.params.id),
      {
        resolution: input.status,
        reason: input.hideReason ?? input.note ?? 'Reviewed through generator moderation',
        hide: input.hideMap,
      },
      a.id,
      { action: 'generator.report.' + input.status, targetType: 'generator', reportTarget: false },
    );
    const row = await db
      .selectFrom('generator_reports')
      .selectAll()
      .where('id', '=', r.params.id)
      .executeTakeFirstOrThrow();
    return row;
  });
}
