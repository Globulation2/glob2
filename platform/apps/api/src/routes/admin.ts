// Minimal moderation REST (/api/v1/admin). Moderators: search, rename, mute.
// Administrators: also ban and change roles. Web admin pages come in M8.
import type { FastifyInstance, FastifyRequest } from 'fastify';
import {
  AdminBanRequest,
  AdminMuteRequest,
  AdminRenameRequest,
  AdminRoleRequest,
  type AdminAccount,
  type AdminAccountList,
} from '@glob2/protocol';
import { apiError } from '../errors.ts';
import { body } from '../http/validate.ts';
import { requireRole, type Identity } from '../identity.ts';

const PAGE_SIZE = 50;

export async function adminRoutes(app: FastifyInstance, identity: Identity): Promise<void> {
  const target = async (request: FastifyRequest<{ Params: { id: string } }>) => {
    const account = await identity.accounts.get(request.params.id);
    if (!account || account.status === 'deleted') throw apiError('not_found', 'No such account.');
    return account;
  };

  app.get<{ Querystring: { q?: string; cursor?: string } }>(
    '/api/v1/admin/accounts',
    async (request): Promise<AdminAccountList> => {
      await requireRole(identity, request, 'moderator');
      const { q = '', cursor } = request.query;
      let before: Date | undefined;
      if (cursor) {
        before = new Date(Buffer.from(cursor, 'base64url').toString('utf8'));
        if (Number.isNaN(before.getTime())) throw apiError('bad_request', 'Invalid cursor.');
      }
      const rows = await identity.admin.search(String(q).slice(0, 200), PAGE_SIZE + 1, before);
      const page = rows.slice(0, PAGE_SIZE);
      const items = await Promise.all(page.map((row) => identity.admin.view(row)));
      const last = page.at(-1);
      return {
        items,
        ...(rows.length > PAGE_SIZE && last
          ? { nextCursor: Buffer.from(last.created_at.toISOString()).toString('base64url') }
          : {}),
      };
    },
  );

  app.get<{ Params: { id: string } }>(
    '/api/v1/admin/accounts/:id',
    async (request): Promise<AdminAccount> => {
      await requireRole(identity, request, 'moderator');
      return identity.admin.view(await target(request));
    },
  );

  app.post<{ Params: { id: string } }>(
    '/api/v1/admin/accounts/:id/rename',
    async (request): Promise<AdminAccount> => {
      const { account: actor } = await requireRole(identity, request, 'moderator');
      const input = body(AdminRenameRequest, request.body);
      const updated = await identity.admin.rename(
        actor,
        await target(request),
        input.displayName,
        input.reason,
      );
      return identity.admin.view(updated);
    },
  );

  app.post<{ Params: { id: string } }>(
    '/api/v1/admin/accounts/:id/mute',
    async (request): Promise<AdminAccount> => {
      const { account: actor } = await requireRole(identity, request, 'moderator');
      const input = body(AdminMuteRequest, request.body);
      const updated = await identity.admin.mute(
        actor,
        await target(request),
        input.minutes,
        input.reason,
      );
      return identity.admin.view(updated);
    },
  );

  app.post<{ Params: { id: string } }>(
    '/api/v1/admin/accounts/:id/ban',
    async (request): Promise<AdminAccount> => {
      const { account: actor } = await requireRole(identity, request, 'admin');
      const input = body(AdminBanRequest, request.body);
      const updated = await identity.admin.setBanned(
        actor,
        await target(request),
        input.banned,
        input.reason,
      );
      return identity.admin.view(updated);
    },
  );

  app.post<{ Params: { id: string } }>(
    '/api/v1/admin/accounts/:id/role',
    async (request): Promise<AdminAccount> => {
      const { account: actor } = await requireRole(identity, request, 'admin');
      const input = body(AdminRoleRequest, request.body);
      const updated = await identity.admin.setRole(actor, await target(request), input.role);
      return identity.admin.view(updated);
    },
  );
}
