// Account REST: the caller's own account, renames, and public profiles.
import type { FastifyInstance } from 'fastify';
import { UpdateAccountRequest, type PublicAccount, type SelfAccount } from '@glob2/protocol';
import { apiError } from '../errors.ts';
import { body } from '../http/validate.ts';
import { requireAccount, type Identity } from '../identity.ts';

export async function accountRoutes(app: FastifyInstance, identity: Identity): Promise<void> {
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

  app.get<{ Params: { id: string } }>(
    '/api/v1/accounts/:id',
    async (request): Promise<PublicAccount> => {
      const account = await identity.accounts.get(request.params.id);
      if (!account || account.status === 'deleted') throw apiError('not_found', 'No such account.');
      return identity.accounts.publicView(account);
    },
  );
}
