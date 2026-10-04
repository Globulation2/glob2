import { skinModerationRoutes } from './moderation.ts';
import { skinDraftRoutes } from './drafts.ts';
import { seedSkinPresets } from './presets.ts';
import { matchColonySkins } from './matches.ts';
import type { FastifyInstance } from 'fastify';
import { sql } from 'kysely';
import { putContent } from '@glob2/core';
import { EquipSkinRequest, PublishSkinRequest, type ColonySkinVersion } from '@glob2/protocol';
import { requireAccount, type Identity } from '../identity.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { apiError } from '../errors.ts';
import { equipSkin } from './equipment.ts';
import { canonicalSkinImage } from './images.ts';
import { skinManifestSha256, type SkinContent } from './manifest.ts';

export async function skinRoutes(app: FastifyInstance, identity: Identity) {
  const { db, blobs } = app.services;
  await seedSkinPresets(app.services);
  await skinDraftRoutes(app, identity);
  await skinModerationRoutes(app, identity);
  app.get<{ Params: { id: string } }>('/api/v1/matches/:id/skins', async (request, reply) => {
    reply.header('Cache-Control', 'no-store');
    if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(request.params.id))
      throw apiError('not_found', 'Match not found.');
    const match = await db
      .selectFrom('matches')
      .select('id')
      .where('id', '=', request.params.id)
      .executeTakeFirst();
    if (!match) throw apiError('not_found', 'Match not found.');
    return {
      colonySkins: await matchColonySkins(
        db,
        identity.keys,
        app.services.config.publicOrigin,
        match.id,
        false,
      ),
    };
  });
  const uploads = new SharedLimit(db, 'skin-publish', 30, 3600000);
  app.put('/api/v1/skins/equipped', async (request) => {
    const { account } = await requireAccount(identity, request);
    const input = body(EquipSkinRequest, request.body);
    await equipSkin(db, account.id, input.versionId, input.buildingColor);
    return { versionId: input.versionId };
  });
  app.get('/api/v1/skins', async (request) => {
    const { account } = await requireAccount(identity, request);
    const items = await db
      .selectFrom('colony_skins as s')
      .innerJoin('colony_skin_versions as v', 'v.skin_id', 's.id')
      .select([
        's.id as skinId',
        's.name',
        's.kind',
        's.entitlement',
        'v.id as id',
        'v.texture_sha256 as textureSha256',
        'v.manifest_sha256 as manifestSha256',
        'v.layout',
        'v.building_color as buildingColor',
        'v.swarm_mesh as swarmMesh',
      ])
      .where('s.disabled_at', 'is', null)
      .where((eb) =>
        eb.or([eb('s.kind', '=', 'preset'), eb('s.owner_account_id', '=', account.id)]),
      )
      .orderBy('v.created_at', 'desc')
      .limit(200)
      .execute();
    const equipment = await db
      .selectFrom('colony_skin_equipment')
      .select(['version_id', 'building_color'])
      .where('account_id', '=', account.id)
      .executeTakeFirst();
    return {
      items,
      equippedVersionId: equipment?.version_id ?? null,
      equippedBuildingColor: equipment?.building_color ?? null,
    };
  });
  app.post(
    '/api/v1/skins/publish',
    { bodyLimit: 360000 },
    async (request): Promise<ColonySkinVersion> => {
      const { account } = await requireAccount(identity, request);
      if (account.kind !== 'registered' || account.status !== 'active')
        throw apiError('forbidden', 'Link a recoverable account first.');
      const input = body(PublishSkinRequest, request.body);
      if (!input.name.trim()) throw apiError('bad_request', 'Choose a skin name.');
      await enforce(uploads, account.id, undefined, 'Too many skin uploads.');
      // Entitlement check precedes decoding and is repeated while committing.
      const checkGrant = async (query = db) => {
        const grant = await query
          .selectFrom('entitlements')
          .select('id')
          .where('account_id', '=', account.id)
          .where('entitlement', '=', 'skins:designer')
          .where('revoked_at', 'is', null)
          .where((eb) =>
            eb.or([eb('expires_at', 'is', null), eb('expires_at', '>', sql<Date>`now()`)]),
          )
          .executeTakeFirst();
        if (!grant) throw apiError('forbidden', 'The skin designer unlock is required to publish.');
      };
      await checkGrant();
      const image = await canonicalSkinImage(input.imageBase64);
      const stored = await putContent(blobs, image);
      return db.transaction().execute(async (trx) => {
        const current = await trx
          .selectFrom('accounts')
          .select(['id', 'status', 'kind'])
          .where('id', '=', account.id)
          .forUpdate()
          .executeTakeFirstOrThrow();
        if (current.status !== 'active' || current.kind !== 'registered')
          throw apiError('forbidden', 'Link an active recoverable account first.');
        await checkGrant(trx);
        let skin;
        if (input.skinId) {
          skin = await trx
            .selectFrom('colony_skins')
            .selectAll()
            .where('id', '=', input.skinId)
            .where('owner_account_id', '=', account.id)
            .where('disabled_at', 'is', null)
            .forUpdate()
            .executeTakeFirst();
          if (!skin) throw apiError('not_found', 'Skin not found.');
          // The display name belongs to the design; immutable paint remains versioned.
          await trx
            .updateTable('colony_skins')
            .set({ name: input.name.trim() })
            .where('id', '=', skin.id)
            .execute();
        } else {
          skin = await trx
            .insertInto('colony_skins')
            .values({
              owner_account_id: account.id,
              kind: 'custom',
              name: input.name.trim(),
              entitlement: 'skins:designer',
            })
            .returningAll()
            .executeTakeFirstOrThrow();
        }
        await trx
          .insertInto('blobs')
          .values({
            sha256: stored.sha256,
            size: stored.size,
            storage_key: stored.key,
            content_type: 'image/png',
            visibility: 'private',
            owner_account_id: account.id,
          })
          .onConflict((oc) => oc.column('sha256').doNothing())
          .execute();
        const content: SkinContent = {
          skinId: skin.id,
          textureSha256: stored.sha256,
          layout: 'colony-v1',
          buildingColor: input.buildingColor,
          swarmMesh: input.swarmMesh ?? 'classic',
        };
        const digest = skinManifestSha256(content);
        let version = await trx
          .selectFrom('colony_skin_versions')
          .select('id')
          .where('manifest_sha256', '=', digest)
          .executeTakeFirst();
        if (!version)
          version = await trx
            .insertInto('colony_skin_versions')
            .values({
              skin_id: skin.id,
              texture_sha256: stored.sha256,
              layout: 'colony-v1',
              building_color: input.buildingColor,
              swarm_mesh: content.swarmMesh,
              manifest_sha256: digest,
            })
            .returning('id')
            .executeTakeFirstOrThrow();
        return { id: version.id, ...content, manifestSha256: digest };
      });
    },
  );
  app.get<{ Params: { id: string } }>(
    '/api/v1/skins/versions/:id/texture',
    async (request, reply) => {
      if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(request.params.id))
        throw apiError('not_found', 'Skin not found.');
      const row = await db
        .selectFrom('colony_skin_versions as v')
        .innerJoin('colony_skins as s', 's.id', 'v.skin_id')
        .innerJoin('blobs as b', 'b.sha256', 'v.texture_sha256')
        .select(['b.storage_key', 'b.sha256'])
        .where('v.id', '=', request.params.id)
        .where('s.disabled_at', 'is', null)
        .executeTakeFirst();
      if (!row) throw apiError('not_found', 'Skin not found.');
      const stream = await blobs.get(row.storage_key);
      if (!stream) throw apiError('not_found', 'Texture not found.');
      return reply
        .type('image/png')
        .header('X-Content-Type-Options', 'nosniff')
        .header('Cache-Control', 'public, max-age=300')
        .header('ETag', `"${row.sha256}"`)
        .send(stream);
    },
  );
}
