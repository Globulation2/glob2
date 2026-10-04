import type { FastifyInstance } from 'fastify';
import type { Transaction } from 'kysely';
import { sql } from 'kysely';
import type { Database } from '@glob2/db';
import { ModerateSkinRequest, ResolveSkinReportRequest, SkinReportRequest } from '@glob2/protocol';
import { requireAccount, requireRole, type Identity } from '../identity.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { apiError } from '../errors.ts';

const uuid = (value: string) => {
  if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(value))
    throw apiError('bad_request', 'Invalid identifier.');
  return value;
};
const reasonOf = (value: string) => {
  if (!value.trim()) throw apiError('bad_request', 'Give a reason.');
  return value.trim();
};

export async function skinModerationRoutes(app: FastifyInstance, identity: Identity) {
  const { db } = app.services;
  const reports = new SharedLimit(db, 'skin-report', 30, 3600000);
  const setDisabled = async (
    trx: Transaction<Database>,
    id: string,
    disabled: boolean,
    actorId: string,
    reason: string,
  ) => {
    const skin = await trx
      .selectFrom('colony_skins')
      .select('id')
      .where('id', '=', id)
      .forUpdate()
      .executeTakeFirst();
    if (!skin) throw apiError('not_found', 'Skin not found.');
    await trx
      .updateTable('colony_skins')
      .set({ disabled_at: disabled ? new Date() : null })
      .where('id', '=', id)
      .execute();
    await trx
      .insertInto('admin_audit_log')
      .values({
        actor_account_id: actorId,
        action: disabled ? 'skin.disable' : 'skin.enable',
        target_type: 'skin',
        target_id: id,
        details: JSON.stringify({ reason }),
      })
      .execute();
  };
  app.post<{ Params: { id: string } }>(
    '/api/v1/skins/versions/:id/reports',
    async (request, reply) => {
      const { account } = await requireAccount(identity, request);
      reply.header('Cache-Control', 'private, no-store');
      const versionId = uuid(request.params.id);
      const input = body(SkinReportRequest, request.body);
      const reason = reasonOf(input.reason);
      const existing = await db
        .selectFrom('colony_skin_reports')
        .select(['id', 'resolution'])
        .where('version_id', '=', versionId)
        .where('reporter_account_id', '=', account.id)
        .executeTakeFirst();
      if (existing) return existing;
      const version = await db
        .selectFrom('colony_skin_versions')
        .select('id')
        .where('id', '=', versionId)
        .executeTakeFirst();
      if (!version) throw apiError('not_found', 'Skin version not found.');
      await enforce(reports, account.id, undefined, 'Too many skin reports.');
      await db
        .insertInto('colony_skin_reports')
        .values({ version_id: versionId, reporter_account_id: account.id, reason })
        .onConflict((oc) => oc.columns(['version_id', 'reporter_account_id']).doNothing())
        .execute();
      reply.header('Cache-Control', 'private, no-store');
      return db
        .selectFrom('colony_skin_reports')
        .select(['id', 'resolution'])
        .where('version_id', '=', versionId)
        .where('reporter_account_id', '=', account.id)
        .executeTakeFirstOrThrow();
    },
  );
  app.get<{ Querystring: { status?: string; cursor?: string } }>(
    '/api/v1/admin/skin-reports',
    async (request, reply) => {
      await requireRole(identity, request, 'moderator');
      const status = request.query.status ?? 'open';
      if (status !== 'open' && status !== 'closed')
        throw apiError('bad_request', 'Invalid report status.');
      let query = db
        .selectFrom('colony_skin_reports as r')
        .innerJoin('colony_skin_versions as v', 'v.id', 'r.version_id')
        .innerJoin('colony_skins as s', 's.id', 'v.skin_id')
        .innerJoin('accounts as a', 'a.id', 'r.reporter_account_id')
        .select([
          'r.id',
          'r.version_id as versionId',
          's.id as skinId',
          's.name',
          'a.display_name as reporterName',
          'r.reason',
          'r.created_at as createdAt',
          'r.resolution',
          'r.resolution_reason as resolutionReason',
          's.disabled_at as disabledAt',
        ])
        .where('r.resolution', status === 'open' ? 'is' : 'is not', null);
      if (request.query.cursor) {
        const cursor = await db
          .selectFrom('colony_skin_reports')
          .select(['id', 'created_at'])
          .where('id', '=', uuid(request.query.cursor))
          .executeTakeFirst();
        if (!cursor) throw apiError('bad_request', 'Invalid report cursor.');
        query = query.where(
          sql<boolean>`(r.created_at, r.id) < (${cursor.created_at}, ${cursor.id}::uuid)`,
        );
      }
      const rows = await query
        .orderBy('r.created_at', 'desc')
        .orderBy('r.id', 'desc')
        .limit(51)
        .execute();
      reply.header('Cache-Control', 'private, no-store');
      return {
        items: rows.slice(0, 50),
        ...(rows.length > 50 ? { nextCursor: rows[49]?.id } : {}),
      };
    },
  );
  app.post<{ Params: { id: string } }>(
    '/api/v1/admin/skin-reports/:id/resolve',
    async (request) => {
      const { account } = await requireRole(identity, request, 'moderator');
      const input = body(ResolveSkinReportRequest, request.body);
      const reason = reasonOf(input.reason);
      return db.transaction().execute(async (trx) => {
        const report = await trx
          .selectFrom('colony_skin_reports')
          .selectAll()
          .where('id', '=', uuid(request.params.id))
          .forUpdate()
          .executeTakeFirst();
        if (!report) throw apiError('not_found', 'Report not found.');
        if (report.resolution) {
          if (report.resolution !== input.resolution)
            throw apiError('conflict', 'Report already resolved.');
          return { id: report.id, resolution: report.resolution };
        }
        const version = await trx
          .selectFrom('colony_skin_versions')
          .select('skin_id')
          .where('id', '=', report.version_id)
          .executeTakeFirstOrThrow();
        if (input.resolution === 'disabled')
          await setDisabled(trx, version.skin_id, true, account.id, reason);
        await trx
          .updateTable('colony_skin_reports')
          .set({
            resolution: input.resolution,
            resolution_reason: reason,
            resolved_at: new Date(),
            resolved_by_account_id: account.id,
          })
          .where('id', '=', report.id)
          .execute();
        await trx
          .insertInto('admin_audit_log')
          .values({
            actor_account_id: account.id,
            action: 'skin.report.resolve',
            target_type: 'skin_report',
            target_id: report.id,
            details: JSON.stringify({ resolution: input.resolution, reason }),
          })
          .execute();
        return { id: report.id, resolution: input.resolution };
      });
    },
  );
  // Moderators can inspect disabled skins' colour atlas and material map too.
  for (const [route, column, missing] of [
    ['texture', 'v.texture_sha256', 'Texture not found.'],
    ['material', 'v.material_sha256', 'Material map not found.'],
  ] as const)
    app.get<{ Params: { id: string } }>(
      `/api/v1/admin/skins/versions/:id/${route}`,
      async (request, reply) => {
        await requireRole(identity, request, 'moderator');
        const row = await db
          .selectFrom('colony_skin_versions as v')
          .innerJoin('blobs as b', 'b.sha256', column)
          .select('b.storage_key')
          .where('v.id', '=', uuid(request.params.id))
          .executeTakeFirst();
        if (!row) throw apiError('not_found', 'Skin version not found.');
        const stream = await app.services.blobs.get(row.storage_key);
        if (!stream) throw apiError('not_found', missing);
        return reply
          .type('image/png')
          .header('Cache-Control', 'private, no-store')
          .header('X-Content-Type-Options', 'nosniff')
          .send(stream);
      },
    );
  app.post<{ Params: { id: string } }>('/api/v1/admin/skins/:id/moderation', async (request) => {
    const { account } = await requireRole(identity, request, 'moderator');
    const input = body(ModerateSkinRequest, request.body);
    const reason = reasonOf(input.reason);
    await db
      .transaction()
      .execute((trx) =>
        setDisabled(trx, uuid(request.params.id), input.disabled, account.id, reason),
      );
    return { disabled: input.disabled };
  });
}
