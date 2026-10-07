import { sql, type Transaction } from 'kysely';
import type { Database } from '@glob2/db';
import { enqueueSkinSprites, putContent, type BlobStore } from '@glob2/core';
import type { ColonySkinVersion } from '@glob2/protocol';
import { apiError } from '../errors.ts';
import { skinManifestSha256, type SkinContent } from './manifest.ts';

export async function requireDesigner(trx: Transaction<Database>, accountId: string) {
  const account = await trx
    .selectFrom('accounts')
    .select(['kind', 'status'])
    .where('id', '=', accountId)
    .forUpdate()
    .executeTakeFirstOrThrow();
  if (account.kind !== 'registered' || account.status !== 'active')
    throw apiError('forbidden', 'Sign in with an active account to use a custom skin.');
  const grant = await trx
    .selectFrom('entitlements')
    .select('id')
    .where('account_id', '=', accountId)
    .where('entitlement', '=', 'skins:designer')
    .where('revoked_at', 'is', null)
    .where((eb) => eb.or([eb('expires_at', 'is', null), eb('expires_at', '>', sql<Date>`now()`)]))
    .executeTakeFirst();
  if (!grant) throw apiError('forbidden', 'Unlock the skin designer to use custom skins.');
}

/** Caller holds the account/design locks. Versions remain immutable and deduplicated. */
export async function createSnapshot(
  trx: Transaction<Database>,
  blobs: BlobStore,
  accountId: string,
  input: {
    skinId: string;
    buildingColor: number;
    swarmMesh: string;
    swarmViewAngle: number;
    image: Buffer;
    material: Buffer;
  },
): Promise<ColonySkinVersion> {
  const [texture, material] = await Promise.all([
    putContent(blobs, input.image),
    putContent(blobs, input.material),
  ]);
  for (const blob of [texture, material])
    await trx
      .insertInto('blobs')
      .values({
        sha256: blob.sha256,
        size: blob.size,
        storage_key: blob.key,
        content_type: 'image/webp',
        visibility: 'private',
        owner_account_id: accountId,
      })
      .onConflict((oc) => oc.column('sha256').doNothing())
      .execute();
  const content: SkinContent = {
    skinId: input.skinId,
    textureSha256: texture.sha256,
    materialSha256: material.sha256,
    layout: 'colony-v2',
    buildingColor: input.buildingColor,
    swarmMesh: input.swarmMesh as SkinContent['swarmMesh'],
    swarmViewAngle: input.swarmViewAngle,
  };
  const digest = skinManifestSha256(content);
  let version = await trx
    .selectFrom('colony_skin_versions')
    .select('id')
    .where('manifest_sha256', '=', digest)
    .executeTakeFirst();
  version ??= await trx
    .insertInto('colony_skin_versions')
    .values({
      skin_id: input.skinId,
      texture_sha256: texture.sha256,
      material_sha256: material.sha256,
      layout: 'colony-v2',
      building_color: input.buildingColor,
      swarm_mesh: input.swarmMesh,
      swarm_view_angle: input.swarmViewAngle,
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
    id: version.id,
    ...content,
    manifestSha256: digest,
    softwareStatus: derivative?.status ?? 'pending',
  };
}
