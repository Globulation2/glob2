import type { FastifyInstance } from 'fastify';
import { sql } from 'kysely';
import { AdminModerateContent, AdminResolveReport, type AdminAuditEntry } from '@glob2/protocol';
import { requireRole, type Identity } from '../identity.ts';
import { body } from '../http/validate.ts';
import { apiError } from '../errors.ts';
import {
  listContent,
  listReports,
  libraryOf,
  moderateContent,
  resolveReport,
} from './moderation.ts';
import { decodeCursor, encodeCursor, pageSize } from './cursor.ts';
import { cursorTimeSql } from '../http/cursorTime.ts';
import { MODERATION_AUDIT_ACTIONS, auditDate } from './audit.ts';

type Query = {
  library?: string;
  status?: string;
  hidden?: string;
  q?: string;
  cursor?: string;
  limit?: string;
  actor?: string;
  action?: string;
  target?: string;
  from?: string;
  to?: string;
};
export async function adminConsoleRoutes(app: FastifyInstance, identity: Identity) {
  const { db } = app.services;
  app.addHook('onSend', async (request, reply, payload) => {
    if (request.url.startsWith('/api/v1/admin/'))
      reply.header('cache-control', 'private, no-store');
    return payload;
  });
  app.get<{ Querystring: Query }>('/api/v1/admin/reports', async (r) => {
    await requireRole(identity, r, 'moderator');
    return listReports(db, r.query);
  });
  app.get<{ Querystring: Query }>('/api/v1/admin/content', async (r) => {
    await requireRole(identity, r, 'moderator');
    return listContent(db, r.query);
  });
  app.post<{ Params: { library: string; id: string } }>(
    '/api/v1/admin/reports/:library/:id/resolve',
    async (r, reply) => {
      const { account } = await requireRole(identity, r, 'moderator'),
        input = body(AdminResolveReport, r.body);
      if (!input.reason.trim() || input.reason.includes('\0'))
        throw apiError('bad_request', 'Give a moderation reason.');
      if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(r.params.id))
        throw apiError('bad_request', 'Invalid report identifier.');
      if (input.hide && input.resolution === 'dismissed')
        throw apiError('bad_request', 'A dismissed report cannot hide content.');
      await resolveReport(db, libraryOf(r.params.library), r.params.id, input, account.id);
      return reply.status(204).send();
    },
  );
  app.post<{ Params: { library: string; id: string } }>(
    '/api/v1/admin/content/:library/:id/moderation',
    async (r, reply) => {
      const { account } = await requireRole(identity, r, 'moderator'),
        input = body(AdminModerateContent, r.body);
      if (
        !input.reason.trim() ||
        input.reason.includes('\0') ||
        !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(r.params.id)
      )
        throw apiError('bad_request', 'Give a reason and a valid content identifier.');
      await db
        .transaction()
        .execute((trx) =>
          moderateContent(
            trx,
            libraryOf(r.params.library),
            r.params.id,
            input.hidden,
            input.reason,
            account.id,
          ),
        );
      return reply.status(204).send();
    },
  );
  app.get<{ Querystring: Query }>('/api/v1/admin/audit', async (r) => {
    const { account } = await requireRole(identity, r, 'moderator'),
      q = r.query,
      limit = pageSize(q.limit),
      before = decodeCursor(q.cursor);
    if (before && !/^[0-9]{1,18}$/.test(before.id))
      throw apiError('bad_request', 'Invalid audit cursor.');
    const from = auditDate(q.from),
      to = auditDate(q.to, true);
    const rows = (
      await sql<
        AdminAuditEntry & { cursorAt: string }
      >`SELECT l.id::text AS id,l.actor_account_id AS "actorId",coalesce(a.display_name,'System / deleted actor') AS "actorName",l.action,l.target_type AS "targetType",l.target_id AS "targetId",l.details,l.created_at AS "createdAt",${cursorTimeSql(sql<Date>`l.created_at`)} AS "cursorAt"
   FROM admin_audit_log l LEFT JOIN accounts a ON a.id=l.actor_account_id WHERE TRUE
   ${account.role === 'admin' ? sql`` : sql`AND l.action IN (${sql.join(MODERATION_AUDIT_ACTIONS)})`}
   ${q.actor ? sql`AND l.actor_account_id::text=${q.actor}` : sql``}
   ${q.action ? sql`AND l.action=${q.action.slice(0, 100)}` : sql``}
   ${q.target ? sql`AND (l.target_id=${q.target.slice(0, 200)} OR l.target_type=${q.target.slice(0, 100)})` : sql``}
   ${from ? sql`AND l.created_at>=${from}` : sql``} ${to ? sql`AND l.created_at<${to}` : sql``}
   ${before ? sql`AND (l.created_at,l.id)<(${before.exactAt},${before.id}::bigint)` : sql``}
   ORDER BY l.created_at DESC,l.id DESC LIMIT ${limit + 1}`.execute(db)
    ).rows;
    const allowed = [
      'reason',
      'note',
      'from',
      'to',
      'report',
      'contentId',
      'hidden',
      'resolution',
      'minutes',
      'displayName',
      'force',
      'previous',
      'jobId',
    ];
    const page = rows.slice(0, limit);
    const items = page.map(({ cursorAt, ...row }) => ({
      ...row,
      createdAt: new Date(cursorAt).toISOString(),
      details: Object.fromEntries(
        Object.entries(row.details).filter(([key]) => allowed.includes(key)),
      ),
    }));
    const last = page.at(-1);
    return {
      items,
      ...(rows.length > limit && last ? { nextCursor: encodeCursor(last.cursorAt, last.id) } : {}),
    };
  });
}
