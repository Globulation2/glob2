import { randomUUID } from 'node:crypto';
import { canonicalAvatar, MAX_BYTES } from './image.ts';
import { GravatarPhotos } from './gravatar.ts';
import { sql } from 'kysely';
import type { FastifyInstance } from 'fastify';
import type { Identity } from '../identity.ts';
import { requireAccount } from '../identity.ts';
import { apiError } from '../errors.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { UUID } from '../history/summaries.ts';

export async function avatarRoutes(app: FastifyInstance, identity: Identity) {
  const { db, blobs } = app.services;
  const writes = new SharedLimit(db, 'account:avatar', 30, 3_600_000);
  const remove = async (keys: (string | null)[]) => {
    for (const key of new Set(keys))
      if (key)
        await blobs
          .delete(key)
          .catch((error) => app.log.warn({ error, key }, 'avatar cleanup failed'));
  };
  const gravatars = new GravatarPhotos(db, blobs, remove);
  const change = async (
    id: string,
    source: 'uploaded' | 'automatic' | 'initials',
    bytes?: Buffer,
  ) => {
    const key = bytes ? `avatars/${id}/${randomUUID()}.webp` : null;
    if (key && bytes) await blobs.put(key, bytes);
    try {
      const previous = await db.transaction().execute(async (tx) => {
        const account = await tx
          .selectFrom('accounts')
          .selectAll()
          .where('id', '=', id)
          .forUpdate()
          .executeTakeFirstOrThrow();
        if (account.status !== 'active' || account.kind !== 'registered')
          throw apiError('forbidden', 'A registered account is required.');
        await tx
          .updateTable('accounts')
          .set({
            avatar_source: source,
            avatar_key: key,
            avatar_revision: sql<number>`avatar_revision + 1`,
            gravatar_key: null,
            gravatar_fingerprint: null,
            gravatar_checked_at: null,
          })
          .where('id', '=', id)
          .execute();
        return [account.avatar_key, account.gravatar_key];
      });
      await remove(previous);
    } catch (error) {
      if (key) await remove([key]);
      throw error;
    }
    return identity.accounts.selfView(await identity.accounts.requireUsable(id));
  };
  app.put('/api/v1/accounts/me/avatar', { bodyLimit: MAX_BYTES }, async (request, reply) => {
    const { account } = await requireAccount(identity, request);
    await enforce(writes, account.id, reply, 'You changed your photo recently; try again later.');
    if (account.kind !== 'registered') throw apiError('forbidden', 'Sign in to upload a photo.');
    if (!Buffer.isBuffer(request.body)) throw apiError('bad_request', 'Upload image bytes.');
    return change(account.id, 'uploaded', await canonicalAvatar(request.body));
  });
  app.patch('/api/v1/accounts/me/avatar', async (request, reply) => {
    const { account } = await requireAccount(identity, request);
    await enforce(writes, account.id, reply, 'You changed your photo recently; try again later.');
    const source = (request.body as { source?: unknown } | null)?.source;
    if (source !== 'automatic' && source !== 'initials')
      throw apiError('bad_request', 'Choose automatic or initials.');
    return change(account.id, source);
  });
  app.delete('/api/v1/accounts/me/avatar', async (request, reply) => {
    const { account } = await requireAccount(identity, request);
    await enforce(writes, account.id, reply, 'You changed your photo recently; try again later.');
    return change(account.id, 'automatic');
  });
  app.get<{ Params: { id: string } }>('/api/v1/accounts/:id/avatar', async (request, reply) => {
    const { id } = request.params;
    if (!UUID.test(id)) throw apiError('not_found', 'No photo.');
    // Do not cache a public URL beyond account deletion or a preference change.
    reply.header('cache-control', 'no-store').header('x-content-type-options', 'nosniff');
    const account = await db
      .selectFrom('accounts')
      .selectAll()
      .where('id', '=', id)
      .executeTakeFirst();
    if (
      !account ||
      account.status !== 'active' ||
      account.kind !== 'registered' ||
      account.avatar_source === 'initials'
    )
      throw apiError('not_found', 'No photo.');
    const key =
      account.avatar_source === 'automatic' ? await gravatars.keyFor(account) : account.avatar_key;
    const stream = key ? await blobs.get(key) : undefined;
    if (!stream) throw apiError('not_found', 'No photo.');
    return reply.header('content-type', 'image/webp').send(stream);
  });
}
