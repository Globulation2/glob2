import { randomUUID } from 'node:crypto';
import type { FastifyInstance } from 'fastify';
import { SaveSkinDraftRequest, type SkinDraft } from '@glob2/protocol';
import { requireAccount, type Identity } from '../identity.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { apiError } from '../errors.ts';
import { canonicalSkinImage } from './images.ts';

export async function skinDraftRoutes(app: FastifyInstance, identity: Identity) {
  const { db } = app.services;
  const saves = new SharedLimit(db, 'skin-draft', 60, 3600000);
  app.get('/api/v1/skins/draft', async (request, reply): Promise<{ draft: SkinDraft | null }> => {
    const { account } = await requireAccount(identity, request);
    if (account.kind !== 'registered' || account.status !== 'active')
      throw apiError('forbidden', 'Link an active recoverable account to sync drafts.');
    reply.header('Cache-Control', 'private, no-store');
    const row = await db
      .selectFrom('colony_skin_drafts')
      .selectAll()
      .where('account_id', '=', account.id)
      .executeTakeFirst();
    return {
      draft: row
        ? {
            ...(row.skin_id ? { skinId: row.skin_id } : {}),
            revision: row.revision,
            name: row.name,
            buildingColor: row.building_color,
            imageBase64: row.image.toString('base64'),
          }
        : null,
    };
  });
  app.put('/api/v1/skins/draft', { bodyLimit: 360000 }, async (request, reply) => {
    const { account } = await requireAccount(identity, request);
    if (account.kind !== 'registered' || account.status !== 'active')
      throw apiError('forbidden', 'Link an active recoverable account to sync drafts.');
    const input = body(SaveSkinDraftRequest, request.body);
    if (!input.name.trim()) throw apiError('bad_request', 'Choose a skin name.');
    await enforce(saves, account.id, undefined, 'Too many draft saves.');
    const image = await canonicalSkinImage(input.imageBase64);
    reply.header('Cache-Control', 'private, no-store');
    return db.transaction().execute(async (trx) => {
      // Serialize first saves too, and recheck account eligibility after decoding.
      const current = await trx
        .selectFrom('accounts')
        .select(['status', 'kind'])
        .where('id', '=', account.id)
        .forUpdate()
        .executeTakeFirstOrThrow();
      if (current.kind !== 'registered' || current.status !== 'active')
        throw apiError('forbidden', 'Link an active recoverable account to sync drafts.');
      const existing = await trx
        .selectFrom('colony_skin_drafts')
        .select('revision')
        .where('account_id', '=', account.id)
        .executeTakeFirst();
      if ((existing?.revision ?? null) !== input.revision)
        throw apiError(
          'conflict',
          'Your account draft changed. Save your work on this device, then restore the account draft before saving again.',
        );
      if (input.skinId) {
        const skin = await trx
          .selectFrom('colony_skins')
          .select('id')
          .where('id', '=', input.skinId)
          .where('owner_account_id', '=', account.id)
          .where('disabled_at', 'is', null)
          .executeTakeFirst();
        if (!skin) throw apiError('not_found', 'Skin not found.');
      }
      const revision = randomUUID();
      const values = {
        revision,
        skin_id: input.skinId ?? null,
        name: input.name.trim(),
        building_color: input.buildingColor,
        image,
        updated_at: new Date(),
      };
      await trx
        .insertInto('colony_skin_drafts')
        .values({ account_id: account.id, ...values })
        .onConflict((oc) => oc.column('account_id').doUpdateSet(values))
        .execute();
      return { revision };
    });
  });
}
