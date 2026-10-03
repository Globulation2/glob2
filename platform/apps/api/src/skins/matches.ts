import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import {
  COLONY_SKIN_AUDIENCE,
  COLONY_SKIN_TYPE,
  type ColonySkinClaims,
  type ColonySkinVersion,
  type MatchColonySkin,
  type MatchSetup,
} from '@glob2/protocol';
import type { SigningKeys } from '../auth/keys.ts';
import { HttpError } from '../errors.ts';
import { authorizedSkin } from './equipment.ts';

/** Serialize the first assignment across API replicas, freezing defaults too.
 * Shared colony policy: the lowest numbered human seat chooses its appearance.
 * Later equipment/purchase changes affect future matches only. */
export async function matchColonySkins(
  db: Kysely<Database>,
  keys: SigningKeys,
  origin: string,
  matchId: string,
  freeze = true,
): Promise<MatchColonySkin[]> {
  const sign = (
    team: number,
    accountId: string,
    version: ColonySkinVersion,
    buildingColor: number,
  ) => {
    const iat = Math.floor(Date.now() / 1000);
    const claims: ColonySkinClaims = {
      iss: origin,
      aud: COLONY_SKIN_AUDIENCE,
      sub: accountId,
      iat,
      exp: iat + 86400,
      matchId,
      team,
      accountId,
      version,
      buildingColor,
    };
    return keys.sign(COLONY_SKIN_TYPE, claims);
  };
  if (freeze)
    await db.transaction().execute(async (trx) => {
      const match = await trx
        .selectFrom('matches')
        .select(['setup', 'skins_frozen_at', 'status'])
        .where('id', '=', matchId)
        .forUpdate()
        .executeTakeFirst();
      if (!match || match.skins_frozen_at || !['starting', 'running'].includes(match.status))
        return;
      const setup = match.setup as unknown as MatchSetup;
      const chosen = new Set<number>();
      for (const seat of [...setup.seats].sort((a, b) => a.seat - b.seat)) {
        if (seat.kind !== 'human' || chosen.has(seat.team)) continue;
        chosen.add(seat.team);
        if (!seat.accountId) continue;
        const equipment = await trx
          .selectFrom('colony_skin_equipment')
          .select(['version_id', 'building_color'])
          .where('account_id', '=', seat.accountId)
          .executeTakeFirst();
        if (!equipment) continue;
        let authorized;
        try {
          authorized = await authorizedSkin(trx, seat.accountId, equipment.version_id);
        } catch (error) {
          if (error instanceof HttpError && (error.statusCode === 403 || error.statusCode === 404))
            continue;
          throw error;
        }
        const version: ColonySkinVersion = {
          id: authorized.id,
          skinId: authorized.skin_id,
          textureSha256: authorized.texture_sha256,
          manifestSha256: authorized.manifest_sha256,
          layout: authorized.layout,
          buildingColor: authorized.building_color,
        };
        await trx
          .insertInto('match_colony_skins')
          .values({
            match_id: matchId,
            team_index: seat.team,
            account_id: seat.accountId,
            version_id: version.id,
            building_color: equipment.building_color ?? version.buildingColor,
            assertion: sign(
              seat.team,
              seat.accountId,
              version,
              equipment.building_color ?? version.buildingColor,
            ),
          })
          .execute();
      }
      await trx
        .updateTable('matches')
        .set({ skins_frozen_at: sql`now()` })
        .where('id', '=', matchId)
        .execute();
    });
  const rows = await db
    .selectFrom('match_colony_skins as m')
    .innerJoin('colony_skin_versions as v', 'v.id', 'm.version_id')
    .innerJoin('colony_skins as s', 's.id', 'v.skin_id')
    .where('s.disabled_at', 'is', null)
    .select([
      'm.team_index',
      'm.account_id',
      'm.building_color as chosen_color',
      'v.id',
      'v.skin_id',
      'v.texture_sha256',
      'v.manifest_sha256',
      'v.layout',
      'v.building_color',
    ])
    .where('m.match_id', '=', matchId)
    .orderBy('m.team_index')
    .execute();
  return rows.map((row) => {
    const version: ColonySkinVersion = {
      id: row.id,
      skinId: row.skin_id,
      textureSha256: row.texture_sha256,
      manifestSha256: row.manifest_sha256,
      layout: row.layout,
      buildingColor: row.building_color,
    };
    // Refresh only authorization lifetime; the frozen content never changes.
    return {
      team: row.team_index,
      accountId: row.account_id,
      version,
      buildingColor: row.chosen_color,
      assertion: sign(row.team_index, row.account_id, version, row.chosen_color),
    };
  });
}
