import { randomUUID } from 'node:crypto';
import sharp from 'sharp';
import { sql, type Transaction } from 'kysely';
import type { Database } from '@glob2/db';
import type { FastifyInstance } from 'fastify';
import {
  ApplySkinDesignRequest,
  CreateSkinDesignRequest,
  SaveSkinDraftRequest,
  type SkinDesign,
  type ColonySkinVersion,
} from '@glob2/protocol';
import { requireAccount, type Identity } from '../identity.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { apiError } from '../errors.ts';
import { authorizedSkin } from './equipment.ts';
import { canonicalSkinImage, canonicalMaterialMap } from './images.ts';
import { createSnapshot, requireDesigner } from './snapshot.ts';
import { knownSwarmMesh, webpSkinVersion } from './manifest.ts';

export async function skinCollectionRoutes(app: FastifyInstance, identity: Identity) {
  const { db, blobs } = app.services;
  const saves = new SharedLimit(db, 'skin-autosave', 120, 60000);
  async function accountId(request: Parameters<typeof requireAccount>[1]) {
    const { account } = await requireAccount(identity, request);
    if (account.kind !== 'registered' || account.status !== 'active')
      throw apiError('forbidden', 'Sign in to save your skins.');
    return account.id;
  }
  async function lockAccount(trx: Transaction<Database>, owner: string) {
    const account = await trx
      .selectFrom('accounts')
      .select(['kind', 'status'])
      .where('id', '=', owner)
      .forUpdate()
      .executeTakeFirstOrThrow();
    if (account.kind !== 'registered' || account.status !== 'active')
      throw apiError('forbidden', 'Sign in with an active account to save skins.');
  }
  async function lock(trx: Transaction<Database>, owner: string, id: string) {
    if (!/^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/i.test(id))
      throw apiError('not_found', 'Skin not found.');
    await lockAccount(trx, owner);
    const skin = await trx
      .selectFrom('colony_skins')
      .selectAll()
      .where('id', '=', id)
      .where('owner_account_id', '=', owner)
      .where('archived_at', 'is', null)
      .where('disabled_at', 'is', null)
      .forUpdate()
      .executeTakeFirst();
    if (!skin) throw apiError('not_found', 'Skin not found.');
    return skin;
  }
  async function bytes(trx: Transaction<Database>, sha: string) {
    const blob = await trx
      .selectFrom('blobs')
      .select('storage_key')
      .where('sha256', '=', sha)
      .executeTakeFirstOrThrow();
    const stream = await blobs.get(blob.storage_key);
    if (!stream) throw apiError('not_found', 'Skin artwork is unavailable.');
    const chunks: Buffer[] = [];
    for await (const chunk of stream) chunks.push(Buffer.from(chunk));
    return Buffer.concat(chunks);
  }
  async function initialize(trx: Transaction<Database>, id: string) {
    const existing = await trx
      .selectFrom('colony_skin_designs')
      .selectAll()
      .where('skin_id', '=', id)
      .executeTakeFirst();
    if (existing) return existing;
    const version = await trx
      .selectFrom('colony_skin_versions')
      .selectAll()
      .where('skin_id', '=', id)
      .orderBy('created_at', 'desc')
      .orderBy('id')
      .executeTakeFirst();
    if (!version) throw apiError('not_found', 'Skin artwork is unavailable.');
    const revision = randomUUID();
    return trx
      .insertInto('colony_skin_designs')
      .values({
        skin_id: id,
        revision,
        applied_revision: revision,
        applied_version_id: version.id,
        building_color: version.building_color,
        swarm_mesh: version.swarm_mesh,
        swarm_view_angle: version.swarm_view_angle,
        image: await bytes(trx, version.texture_sha256),
        material: await bytes(trx, version.material_sha256),
        updated_at: new Date(),
      })
      .returningAll()
      .executeTakeFirstOrThrow();
  }
  function design(name: string, row: Awaited<ReturnType<typeof initialize>>): SkinDesign {
    return {
      name,
      skinId: row.skin_id,
      revision: row.revision,
      appliedRevision: row.applied_revision,
      appliedVersionId: row.applied_version_id,
      buildingColor: row.building_color,
      swarmMesh: knownSwarmMesh(row.swarm_mesh) ?? 'classic',
      swarmViewAngle: row.swarm_view_angle,
      imageBase64: row.image.toString('base64'),
      materialBase64: row.material.toString('base64'),
    };
  }
  app.get('/api/v1/skins/collection', async (request, reply) => {
    const owner = await accountId(request);
    reply.header('Cache-Control', 'private, no-store');
    return db.transaction().execute(async (trx) => {
      await lockAccount(trx, owner);
      const skins = await trx
        .selectFrom('colony_skins')
        .select(['id', 'name'])
        .where('owner_account_id', '=', owner)
        .where('archived_at', 'is', null)
        .where('disabled_at', 'is', null)
        .orderBy('created_at', 'desc')
        .execute();
      const designs: SkinDesign[] = [];
      for (const skin of skins) designs.push(design(skin.name, await initialize(trx, skin.id)));
      const grants = await trx
        .selectFrom('entitlements')
        .select('entitlement')
        .where('account_id', '=', owner)
        .where('revoked_at', 'is', null)
        .where((eb) =>
          eb.or([eb('expires_at', 'is', null), eb('expires_at', '>', sql<Date>`now()`)]),
        )
        .execute();
      const owned = grants.map((g) => g.entitlement);
      const presets = owned.length
        ? await trx
            .selectFrom('colony_skins as s')
            .innerJoin('colony_skin_versions as v', 'v.skin_id', 's.id')
            .select([
              's.name',
              'v.id',
              'v.skin_id as skinId',
              'v.texture_sha256 as textureSha256',
              'v.material_sha256 as materialSha256',
              'v.manifest_sha256 as manifestSha256',
              'v.layout',
              'v.building_color as buildingColor',
              'v.swarm_mesh as swarmMesh',
              'v.swarm_view_angle as swarmViewAngle',
            ])
            .where('s.kind', '=', 'preset')
            .where('s.disabled_at', 'is', null)
            .where('s.entitlement', 'in', owned)
            .orderBy('v.created_at', 'desc')
            .execute()
        : [];
      const unique = [
        ...new Map(
          presets
            .filter((p) => knownSwarmMesh(p.swarmMesh))
            .reverse()
            .map((p) => [p.skinId, p]),
        ).values(),
      ];
      const equipment = await trx
        .selectFrom('colony_skin_equipment as e')
        .innerJoin('colony_skin_versions as v', 'v.id', 'e.version_id')
        .select(['e.version_id', 'v.skin_id'])
        .where('e.account_id', '=', owner)
        .executeTakeFirst();
      const artwork = equipment
        ? await trx
            .selectFrom('colony_skin_sprites')
            .select('status')
            .where('version_id', '=', equipment.version_id)
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
            .executeTakeFirst()
        : undefined;
      return {
        ...(equipment ? { activeArtworkStatus: artwork?.status ?? 'pending' } : {}),
        designs,
        presets: await Promise.all(
          unique.map(async (p) => ({
            ...(await webpSkinVersion(app.services, p as ColonySkinVersion)),
            name: p.name,
          })),
        ),
        equippedVersionId: equipment?.version_id ?? null,
        activeSkinId: equipment?.skin_id ?? null,
        canUseCustom: owned.includes('skins:designer'),
      };
    });
  });
  app.post('/api/v1/skins/designs', async (request) => {
    const owner = await accountId(request);
    const input = body(CreateSkinDesignRequest, request.body);
    if (!input.name.trim()) throw apiError('bad_request', 'Choose a skin name.');
    await enforce(saves, owner, undefined, 'Too many skin saves. Try again shortly.');
    return db.transaction().execute(async (trx) => {
      await lockAccount(trx, owner);
      const previous = await trx
        .selectFrom('colony_skins')
        .selectAll()
        .where('id', '=', input.id)
        .executeTakeFirst();
      if (previous) {
        await lock(trx, owner, input.id);
        return { design: design(previous.name, await initialize(trx, input.id)) };
      }
      const count = await trx
        .selectFrom('colony_skins')
        .select(trx.fn.countAll<string>().as('count'))
        .where('owner_account_id', '=', owner)
        .where('archived_at', 'is', null)
        .executeTakeFirstOrThrow();
      if (Number(count.count) >= 100)
        throw apiError(
          'bad_request',
          'Your collection has 100 designs. Delete an unused design to make room.',
        );
      let content;
      if (input.sourceSkinId) {
        const source = await trx
          .selectFrom('colony_skins')
          .select(['kind'])
          .where('id', '=', input.sourceSkinId)
          .executeTakeFirst();
        if (source?.kind === 'preset') {
          const latest = await trx
            .selectFrom('colony_skin_versions')
            .select('id')
            .where('skin_id', '=', input.sourceSkinId)
            .orderBy('created_at', 'desc')
            .executeTakeFirstOrThrow();
          const version = await authorizedSkin(trx, owner, latest.id);
          content = {
            ...version,
            image: await bytes(trx, version.texture_sha256),
            material: await bytes(trx, version.material_sha256),
          };
        } else {
          await lock(trx, owner, input.sourceSkinId);
          content = await initialize(trx, input.sourceSkinId);
        }
      }
      const image =
        content?.image ??
        (await sharp({ create: { width: 512, height: 512, channels: 3, background: '#ffffff' } })
          .webp({ lossless: true })
          .toBuffer());
      const material =
        content?.material ??
        (await sharp({ create: { width: 512, height: 512, channels: 3, background: '#000000' } })
          .webp({ lossless: true })
          .toBuffer());
      await trx
        .insertInto('colony_skins')
        .values({
          id: input.id,
          owner_account_id: owner,
          kind: 'custom',
          name: input.name.trim(),
          entitlement: 'skins:designer',
        })
        .execute();
      const row = await trx
        .insertInto('colony_skin_designs')
        .values({
          skin_id: input.id,
          revision: randomUUID(),
          building_color: content?.building_color ?? 0xed9252,
          swarm_mesh: content?.swarm_mesh ?? 'classic',
          swarm_view_angle: content?.swarm_view_angle ?? 0,
          image,
          material,
          updated_at: new Date(),
        })
        .returningAll()
        .executeTakeFirstOrThrow();
      return { design: design(input.name.trim(), row) };
    });
  });
  app.put<{ Params: { id: string } }>(
    '/api/v1/skins/designs/:id',
    { bodyLimit: 1600000 },
    async (request) => {
      const owner = await accountId(request);
      const input = body(SaveSkinDraftRequest, request.body);
      if (!input.name.trim()) throw apiError('bad_request', 'Choose a skin name.');
      await enforce(saves, owner, undefined, 'Too many skin saves. Try again shortly.');
      const [image, material] = await Promise.all([
        canonicalSkinImage(input.imageBase64),
        canonicalMaterialMap(input.materialBase64),
      ]);
      return db.transaction().execute(async (trx) => {
        await lock(trx, owner, request.params.id);
        const existing = await initialize(trx, request.params.id);
        if (existing.revision !== input.revision)
          throw apiError(
            'conflict',
            'This skin changed on another device. Your changes are safe here.',
          );
        const revision = randomUUID();
        await trx
          .updateTable('colony_skins')
          .set({ name: input.name.trim() })
          .where('id', '=', request.params.id)
          .execute();
        await trx
          .updateTable('colony_skin_designs')
          .set({
            revision,
            image,
            material,
            building_color: input.buildingColor,
            swarm_mesh: input.swarmMesh ?? 'classic',
            swarm_view_angle: input.swarmViewAngle ?? 0,
            updated_at: new Date(),
          })
          .where('skin_id', '=', request.params.id)
          .execute();
        return { revision };
      });
    },
  );
  app.post<{ Params: { id: string } }>('/api/v1/skins/designs/:id/use', async (request) => {
    const owner = await accountId(request);
    const input = body(ApplySkinDesignRequest, request.body);
    await enforce(saves, owner, undefined, 'Too many skin saves. Try again shortly.');
    return db.transaction().execute(async (trx) => {
      await lock(trx, owner, request.params.id);
      await requireDesigner(trx, owner);
      const row = await initialize(trx, request.params.id);
      if (row.revision !== input.revision)
        throw apiError(
          'conflict',
          'This skin changed on another device. Reload it before using it.',
        );
      const version = await createSnapshot(trx, blobs, owner, {
        skinId: row.skin_id,
        buildingColor: row.building_color,
        swarmMesh: row.swarm_mesh,
        swarmViewAngle: row.swarm_view_angle,
        image: await canonicalSkinImage(row.image.toString('base64')),
        material: await canonicalMaterialMap(row.material.toString('base64')),
      });
      await trx
        .insertInto('colony_skin_equipment')
        .values({
          account_id: owner,
          version_id: version.id,
          building_color: row.building_color,
          updated_at: new Date(),
        })
        .onConflict((oc) =>
          oc.column('account_id').doUpdateSet({
            version_id: version.id,
            building_color: row.building_color,
            updated_at: new Date(),
          }),
        )
        .execute();
      await trx
        .updateTable('colony_skin_designs')
        .set({ applied_revision: row.revision, applied_version_id: version.id })
        .where('skin_id', '=', row.skin_id)
        .execute();
      return { version, revision: row.revision };
    });
  });
  app.delete<{ Params: { id: string } }>('/api/v1/skins/designs/:id', async (request) => {
    const owner = await accountId(request);
    await db.transaction().execute(async (trx) => {
      await lock(trx, owner, request.params.id);
      const versions = trx
        .selectFrom('colony_skin_versions')
        .select('id')
        .where('skin_id', '=', request.params.id);
      await trx
        .deleteFrom('colony_skin_equipment')
        .where('account_id', '=', owner)
        .where('version_id', 'in', versions)
        .execute();
      await trx
        .updateTable('colony_skins')
        .set({ archived_at: new Date() })
        .where('id', '=', request.params.id)
        .execute();
      await trx
        .deleteFrom('colony_skin_designs')
        .where('skin_id', '=', request.params.id)
        .execute();
    });
    return { deleted: true };
  });
}
