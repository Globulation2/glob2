import { avatarRoutes } from '../avatars/routes.ts';
// Account REST: the caller's own account, renames, unlinking sign-in methods,
// downloading its data, self-service deletion, and public profiles.
import type { FastifyInstance } from 'fastify';
import type { Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import {
  DeleteAccountRequest,
  UpdateAccountRequest,
  type PublicAccount,
  type SelfAccount,
} from '@glob2/protocol';
import { exportAccount } from '../auth/accountExport.ts';
import { apiError } from '../errors.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { body } from '../http/validate.ts';
import { clearSessionCookie, requireAccount, type Identity } from '../identity.ts';

/** Data exports per account and hour: each reads every table about the account. */
const EXPORTS_PER_HOUR = 10;

export async function accountRoutes(
  app: FastifyInstance,
  identity: Identity,
  db: Kysely<Database>,
): Promise<void> {
  await avatarRoutes(app, identity);
  const exports = new SharedLimit(db, 'account:export', EXPORTS_PER_HOUR, 3_600_000);

  app.get('/api/v1/accounts/me', async (request): Promise<SelfAccount> => {
    const { account } = await requireAccount(identity, request);
    return identity.accounts.selfView(account);
  });

  app.patch('/api/v1/accounts/me', async (request): Promise<SelfAccount> => {
    const { account } = await requireAccount(identity, request);
    const { displayName } = body(UpdateAccountRequest, request.body);
    const renamed = await identity.accounts.rename(account, displayName);
    return identity.accounts.selfView(renamed);
  });

  // "Download my data" (privacy policy; GDPR/PIPEDA access requests): every
  // stored row about the caller, as a JSON file. The web account page links
  // here; the session cookie or a bearer token authenticates it.
  app.get('/api/v1/accounts/me/export', async (request, reply) => {
    const { account } = await requireAccount(identity, request);
    await enforce(
      exports,
      account.id,
      reply,
      'You downloaded your data recently; try again later.',
    );
    const document = await exportAccount(db, account, identity.origin);
    const day = document.exportedAt.slice(0, 10);
    void reply
      .header('content-type', 'application/json; charset=utf-8')
      .header(
        'content-disposition',
        `attachment; filename="glob2-account-${account.id.slice(0, 8)}-${day}.json"`,
      )
      .header('cache-control', 'no-store')
      .header('x-content-type-options', 'nosniff');
    return JSON.stringify(document, null, 2);
  });

  // Deletes the caller's account (app stores and GDPR require it be
  // self-service). The web account page and the game's settings lead here.
  app.delete('/api/v1/accounts/me', async (request, reply): Promise<void> => {
    const { account } = await requireAccount(identity, request);
    const { confirmDisplayName } = body(DeleteAccountRequest, request.body);
    if (confirmDisplayName.trim() !== account.display_name) {
      throw apiError('bad_request', 'Type your display name exactly to confirm.', {
        reason: 'confirmation_mismatch',
      });
    }
    await identity.admin.deleteAccount(account, account, undefined, { self: true });
    request.log.info({ account: account.id }, 'account deleted by its owner');
    clearSessionCookie(identity, reply);
    reply.code(204);
  });

  app.delete<{ Params: { provider: string } }>(
    '/api/v1/accounts/me/identities/:provider',
    async (request, reply): Promise<void> => {
      const { account } = await requireAccount(identity, request);
      const provider = request.params.provider;
      if (!/^[a-z0-9][a-z0-9_-]{0,63}$/.test(provider)) {
        throw apiError('bad_request', 'Invalid provider.');
      }
      await identity.accounts.unlinkProvider(account, provider);
      reply.code(204);
    },
  );

  app.get<{ Params: { id: string } }>(
    '/api/v1/accounts/:id',
    async (request): Promise<PublicAccount> => {
      const account = await identity.accounts.get(request.params.id);
      if (!account || account.status === 'deleted') throw apiError('not_found', 'No such account.');
      return identity.accounts.publicView(account);
    },
  );
}
