import { sql, type Kysely, type Transaction } from 'kysely';
import type { Database } from '@glob2/db';
import { apiError } from '../errors.ts';

/** Resolve authorization afresh when equipping and again when freezing a match.
 * An old equipment row alone never proves that a purchase is still valid. */
export async function authorizedSkin(
  db: Kysely<Database> | Transaction<Database>,
  accountId: string,
  versionId: string,
) {
  const account = await db
    .selectFrom('accounts')
    .select(['status', 'kind'])
    .where('id', '=', accountId)
    .executeTakeFirst();
  if (!account || account.status !== 'active' || account.kind !== 'registered') {
    throw apiError('forbidden', 'A recoverable active account is required for colony skins.');
  }
  const version = await db
    .selectFrom('colony_skin_versions as v')
    .innerJoin('colony_skins as s', 's.id', 'v.skin_id')
    .select([
      'v.id',
      'v.skin_id',
      'v.texture_sha256',
      'v.layout',
      'v.building_color',
      'v.manifest_sha256',
      's.owner_account_id',
      's.entitlement',
      's.disabled_at',
    ])
    .where('v.id', '=', versionId)
    .executeTakeFirst();
  if (!version || version.disabled_at)
    throw apiError('not_found', 'This colony skin is unavailable.');
  if (version.owner_account_id && version.owner_account_id !== accountId) {
    throw apiError('forbidden', 'This custom skin belongs to another account.');
  }
  const grant = await db
    .selectFrom('entitlements')
    .select('id')
    .where('account_id', '=', accountId)
    .where('entitlement', '=', version.entitlement)
    .where('revoked_at', 'is', null)
    .where((eb) => eb.or([eb('expires_at', 'is', null), eb('expires_at', '>', sql<Date>`now()`)]))
    .executeTakeFirst();
  if (!grant) throw apiError('forbidden', 'This account does not own this colony skin.');
  return version;
}

export async function equipSkin(
  db: Kysely<Database>,
  accountId: string,
  versionId: string | null,
  buildingColor?: number,
) {
  await db.transaction().execute(async (trx) => {
    // Serialize concurrent equipment changes for this account.
    await trx.selectFrom('accounts').select('id').where('id', '=', accountId).forUpdate().execute();
    if (versionId === null) {
      await trx.deleteFrom('colony_skin_equipment').where('account_id', '=', accountId).execute();
      return;
    }
    await authorizedSkin(trx, accountId, versionId);
    await trx
      .insertInto('colony_skin_equipment')
      .values({
        account_id: accountId,
        version_id: versionId,
        building_color: buildingColor ?? null,
      })
      .onConflict((oc) =>
        oc.column('account_id').doUpdateSet({
          version_id: versionId,
          building_color: buildingColor ?? null,
          updated_at: sql`now()`,
        }),
      )
      .execute();
  });
}
