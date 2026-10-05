import { skinModerationRoutes } from './moderation.ts';
import { skinDraftRoutes } from './drafts.ts';
import { seedSkinPresets } from './presets.ts';
import { matchColonySkins } from './matches.ts';
import type { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';
import { sql } from 'kysely';
import { enqueueSkinSprites, putContent } from '@glob2/core';
import { EquipSkinRequest, PublishSkinRequest, type ColonySkinVersion } from '@glob2/protocol';
import { requireAccount, type Identity } from '../identity.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { apiError } from '../errors.ts';
import { equipSkin } from './equipment.ts';
import { canonicalMaterialMap, canonicalSkinImage } from './images.ts';
import { knownSwarmMesh, skinManifestSha256, type SkinContent } from './manifest.ts';

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
    const versions = await db
      .selectFrom('colony_skins as s')
      .innerJoin('colony_skin_versions as v', 'v.skin_id', 's.id')
      .select([
        's.id as skinId',
        's.name',
        's.kind',
        's.entitlement',
        'v.id as id',
        'v.texture_sha256 as textureSha256',
        'v.material_sha256 as materialSha256',
        'v.manifest_sha256 as manifestSha256',
        'v.layout',
        'v.building_color as buildingColor',
        'v.swarm_mesh as swarmMesh',
        'v.swarm_view_angle as swarmViewAngle',
      ])
      .where('s.disabled_at', 'is', null)
      .where((eb) =>
        eb.or([eb('s.kind', '=', 'preset'), eb('s.owner_account_id', '=', account.id)]),
      )
      .orderBy('v.created_at', 'desc')
      .limit(200)
      .execute();
    // Like match appearances, omit versions for a swarm mesh this release does not know.
    const statuses = await db
      .selectFrom('colony_skin_sprites')
      .select(['version_id', 'status'])
      .where(
        'version_id',
        'in',
        versions.map((v) => v.id),
      )
      .where(
        'render_revision',
        '=',
        db
          .selectFrom('skin_render_revisions')
          .select('revision')
          .orderBy(sql<boolean>`last_seen_at > now() - interval '90 seconds'`, 'desc')
          .orderBy('created_at', 'desc')
          .orderBy('revision')
          .limit(1),
      )
      .execute();
    const status = new Map(statuses.map((row) => [row.version_id, row.status]));
    const items = versions
      .filter((version) => knownSwarmMesh(version.swarmMesh))
      .map((version) => ({ ...version, softwareStatus: status.get(version.id) ?? 'pending' }));
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
    { bodyLimit: 1600000 },
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
      const material = await canonicalMaterialMap(input.materialBase64);
      const stored = await putContent(blobs, image);
      const storedMaterial = await putContent(blobs, material);
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
        for (const blob of [stored, storedMaterial])
          await trx
            .insertInto('blobs')
            .values({
              sha256: blob.sha256,
              size: blob.size,
              storage_key: blob.key,
              content_type: 'image/png',
              visibility: 'private',
              owner_account_id: account.id,
            })
            .onConflict((oc) => oc.column('sha256').doNothing())
            .execute();
        const content: SkinContent = {
          skinId: skin.id,
          textureSha256: stored.sha256,
          materialSha256: storedMaterial.sha256,
          layout: 'colony-v2',
          buildingColor: input.buildingColor,
          swarmMesh: input.swarmMesh ?? 'classic',
          swarmViewAngle: input.swarmViewAngle ?? 0,
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
              material_sha256: storedMaterial.sha256,
              layout: 'colony-v2',
              building_color: input.buildingColor,
              swarm_mesh: content.swarmMesh,
              swarm_view_angle: content.swarmViewAngle ?? 0,
              manifest_sha256: digest,
            })
            .returning('id')
            .executeTakeFirstOrThrow();
        await enqueueSkinSprites(trx, version.id);
        const derivative = await trx
          .selectFrom('colony_skin_sprites')
          .select('status')
          .where('version_id', '=', version.id)
          .where(
            'render_revision',
            '=',
            trx
              .selectFrom('skin_render_revisions')
              .select('revision')
              .orderBy(sql<boolean>`last_seen_at > now() - interval '90 seconds'`, 'desc')
              .orderBy('created_at', 'desc')
              .orderBy('revision')
              .limit(1),
          )
          .executeTakeFirst();
        return {
          softwareStatus: derivative?.status ?? 'pending',
          id: version.id,
          ...content,
          manifestSha256: digest,
        };
      });
    },
  );
  app.get<{ Params: { id: string; bundle: string; hash?: string } }>(
    '/api/v1/skins/versions/:id/sprites/:bundle/manifest',
    spriteAsset(false),
  );
  app.get<{ Params: { id: string; bundle: string; hash?: string } }>(
    '/api/v1/skins/versions/:id/sprites/:bundle/pages/:hash',
    spriteAsset(true),
  );
  function spriteAsset(page: boolean) {
    return async (
      request: FastifyRequest<{
        Params: { id: string; bundle: string; hash?: string };
      }>,
      reply: FastifyReply,
    ) => {
      const { id, bundle, hash } = request.params;
      if (
        !/^[0-9a-f-]{36}$/.test(id) ||
        !/^[0-9a-f]{64}$/.test(bundle) ||
        (page && !/^[0-9a-f]{64}$/.test(hash ?? ''))
      )
        throw apiError('not_found', 'Skin not found.');
      const derivative = await db
        .selectFrom('colony_skin_sprites as d')
        .innerJoin('colony_skin_versions as v', 'v.id', 'd.version_id')
        .innerJoin('colony_skins as s', 's.id', 'v.skin_id')
        .select('d.id')
        .where('v.id', '=', id)
        .where('d.manifest_sha256', '=', bundle)
        .where('d.status', '=', 'ready')
        .where('s.disabled_at', 'is', null)
        .executeTakeFirst();
      if (!derivative) throw apiError('not_found', 'Skin artwork not found.');
      if (
        page &&
        !(await db
          .selectFrom('colony_skin_sprite_pages')
          .select('sha256')
          .where('sprites_id', '=', derivative.id)
          .where('sha256', '=', hash ?? '')
          .executeTakeFirst())
      )
        throw apiError('not_found', 'Skin page not found.');
      const blob = await db
        .selectFrom('blobs')
        .select('storage_key')
        .where('sha256', '=', page ? (hash ?? '') : bundle)
        .executeTakeFirst();
      const stream = blob ? await blobs.get(blob.storage_key) : undefined;
      if (!stream) throw apiError('not_found', 'Skin artwork not found.');
      return reply
        .type(page ? 'image/webp' : 'application/json')
        .header('X-Content-Type-Options', 'nosniff')
        .header('Cache-Control', 'public, max-age=300')
        .header('ETag', `"${page ? hash : bundle}"`)
        .send(stream);
    };
  }
  // Public, cacheable content of enabled skins: the colour atlas and the material map.
  for (const [route, column, missing] of [
    ['texture', 'v.texture_sha256', 'Texture not found.'],
    ['material', 'v.material_sha256', 'Material map not found.'],
  ] as const)
    app.get<{ Params: { id: string } }>(
      `/api/v1/skins/versions/:id/${route}`,
      async (request, reply) => {
        if (
          !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(request.params.id)
        )
          throw apiError('not_found', 'Skin not found.');
        const row = await db
          .selectFrom('colony_skin_versions as v')
          .innerJoin('colony_skins as s', 's.id', 'v.skin_id')
          .innerJoin('blobs as b', 'b.sha256', column)
          .select(['b.storage_key', 'b.sha256'])
          .where('v.id', '=', request.params.id)
          .where('s.disabled_at', 'is', null)
          .executeTakeFirst();
        if (!row) throw apiError('not_found', 'Skin not found.');
        const stream = await blobs.get(row.storage_key);
        if (!stream) throw apiError('not_found', missing);
        return reply
          .type('image/png')
          .header('X-Content-Type-Options', 'nosniff')
          .header('Cache-Control', 'public, max-age=300')
          .header('ETag', `"${row.sha256}"`)
          .send(stream);
      },
    );
}
