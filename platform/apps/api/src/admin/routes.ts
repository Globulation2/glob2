import type { FastifyInstance } from 'fastify';
import { AdminModerateContent, AdminResolveReport } from '@glob2/protocol';
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
}
