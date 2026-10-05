import { readFileSync } from 'node:fs';
import {
  COLONY_SKIN_TYPE,
  COLONY_SKIN_AUDIENCE,
  simVersionKey,
  type MatchSetup,
} from '@glob2/protocol';
import { SigningKeys } from '../src/auth/keys.ts';
import { matchColonySkins } from '../src/skins/matches.ts';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { createTestDatabase, type TestDatabase } from '../../../packages/db/test/support.ts';
import { authorizedSkin, equipSkin } from '../src/skins/equipment.ts';

let database: TestDatabase;
beforeAll(async () => {
  database = await createTestDatabase();
});
afterAll(async () => {
  await database?.drop();
});

describe('colony skin equipment', () => {
  it('requires ownership, honors revocation, and keeps published content immutable', async () => {
    const db = database.db;
    const account = await db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: 'Painter' })
      .returning('id')
      .executeTakeFirstOrThrow();
    const other = await db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: 'Other' })
      .returning('id')
      .executeTakeFirstOrThrow();
    const hash = 'a'.repeat(64);
    const materialHash = 'c'.repeat(64);
    for (const sha256 of [hash, materialHash])
      await db
        .insertInto('blobs')
        .values({
          sha256,
          size: 100,
          content_type: 'image/png',
          storage_key: `sha256/${sha256.slice(0, 2)}/${sha256}`,
        })
        .execute();
    const skin = await db
      .insertInto('colony_skins')
      .values({
        kind: 'custom',
        owner_account_id: account.id,
        name: 'Painted colony',
        entitlement: 'skins:designer',
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    const version = await db
      .insertInto('colony_skin_versions')
      .values({
        skin_id: skin.id,
        texture_sha256: hash,
        material_sha256: materialHash,
        layout: 'colony-v2',
        building_color: 0xff8800,
        swarm_mesh: 'crown',
        swarm_view_angle: 127,
        manifest_sha256: 'b'.repeat(64),
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    await expect(equipSkin(db, account.id, version.id)).rejects.toThrow('does not own');
    const grant = await db
      .insertInto('entitlements')
      .values({ account_id: account.id, entitlement: 'skins:designer', source: 'test' })
      .returning('id')
      .executeTakeFirstOrThrow();
    await db
      .insertInto('entitlements')
      .values({ account_id: other.id, entitlement: 'skins:designer', source: 'test' })
      .execute();
    await expect(equipSkin(db, other.id, version.id)).rejects.toThrow('another account');
    await equipSkin(db, account.id, version.id, 0x112233);
    expect(
      (
        await db
          .selectFrom('colony_skin_equipment')
          .selectAll()
          .where('account_id', '=', account.id)
          .executeTakeFirstOrThrow()
      ).version_id,
    ).toBe(version.id);
    await expect(
      db
        .updateTable('colony_skin_versions')
        .set({ building_color: 0 })
        .where('id', '=', version.id)
        .execute(),
    ).rejects.toThrow('immutable');
    await expect(
      db.deleteFrom('colony_skin_versions').where('id', '=', version.id).execute(),
    ).rejects.toThrow('immutable');
    const setup = JSON.parse(
      readFileSync(
        new URL(
          '../../../packages/protocol/fixtures/valid/MatchSetup/catalog-1v1.json',
          import.meta.url,
        ),
        'utf8',
      ),
    ) as MatchSetup;
    setup.seats = [
      { seat: 0, team: 0, kind: 'human', accountId: account.id, name: 'Painter' },
      { seat: 1, team: 0, kind: 'human', accountId: other.id, name: 'Other' },
    ];
    const makeMatch = async () =>
      db
        .insertInto('matches')
        .values({
          sim_version: simVersionKey(setup.simVersion),
          origin: 'room',
          setup: JSON.stringify(setup),
          seed: setup.seed,
          map_hash: setup.map.hash,
        })
        .returning('id')
        .executeTakeFirstOrThrow();
    const match = await makeMatch();
    const keys = SigningKeys.ephemeral();
    expect(await matchColonySkins(db, keys, 'https://play.test', match.id, false)).toEqual([]);
    expect(
      (
        await db
          .selectFrom('matches')
          .select('skins_frozen_at')
          .where('id', '=', match.id)
          .executeTakeFirstOrThrow()
      ).skins_frozen_at,
    ).toBeNull();
    const [first, concurrent] = await Promise.all([
      matchColonySkins(db, keys, 'https://play.test', match.id),
      matchColonySkins(db, keys, 'https://play.test', match.id),
    ]);
    expect(first).toHaveLength(1);
    expect(concurrent[0]?.version.id).toBe(version.id);
    const appearance = first[0];
    if (!appearance) throw new Error('Missing appearance');
    expect(appearance.accountId).toBe(account.id);
    expect(appearance.buildingColor).toBe(0x112233);
    expect(appearance.version.buildingColor).toBe(0xff8800);
    expect(appearance.version.swarmMesh).toBe('crown');
    expect(appearance.version.swarmViewAngle).toBe(127);
    const verified = keys.verify(appearance.assertion, {
      type: COLONY_SKIN_TYPE,
      audience: COLONY_SKIN_AUDIENCE,
    });
    expect(verified.claims).toMatchObject({
      matchId: match.id,
      team: 0,
      accountId: account.id,
      version: {
        id: version.id,
        textureSha256: hash,
        materialSha256: materialHash,
        layout: 'colony-v2',
        swarmMesh: 'crown',
      },
    });
    expect(() =>
      keys.verify(appearance.assertion, { type: 'glob2-match+jwt', audience: 'glob2-relay' }),
    ).toThrow();
    await equipSkin(db, account.id, null);
    expect((await matchColonySkins(db, keys, 'https://play.test', match.id))[0]?.version.id).toBe(
      version.id,
    );
    const defaults = await makeMatch();
    expect(await matchColonySkins(db, keys, 'https://play.test', defaults.id)).toEqual([]);
    await equipSkin(db, account.id, version.id);
    expect(await matchColonySkins(db, keys, 'https://play.test', defaults.id)).toEqual([]);
    await db
      .updateTable('entitlements')
      .set({ revoked_at: new Date() })
      .where('id', '=', grant.id)
      .execute();
    await expect(authorizedSkin(db, account.id, version.id)).rejects.toThrow('does not own');
    await db
      .updateTable('entitlements')
      .set({ revoked_at: null, expires_at: new Date(0) })
      .where('id', '=', grant.id)
      .execute();
    await expect(authorizedSkin(db, account.id, version.id)).rejects.toThrow('does not own');
    await db
      .updateTable('entitlements')
      .set({ expires_at: null })
      .where('id', '=', grant.id)
      .execute();
    await db
      .updateTable('colony_skins')
      .set({ disabled_at: new Date() })
      .where('id', '=', skin.id)
      .execute();
    await expect(authorizedSkin(db, account.id, version.id)).rejects.toThrow('unavailable');
    expect(await matchColonySkins(db, keys, 'https://play.test', match.id, false)).toEqual([]);
    await db
      .updateTable('colony_skins')
      .set({ disabled_at: null })
      .where('id', '=', skin.id)
      .execute();
    expect(
      (await matchColonySkins(db, keys, 'https://play.test', match.id, false))[0]?.version.id,
    ).toBe(version.id);
    await db
      .updateTable('accounts')
      .set({ status: 'banned' })
      .where('id', '=', account.id)
      .execute();
    await expect(authorizedSkin(db, account.id, version.id)).rejects.toThrow('active account');
    await equipSkin(db, account.id, null);
    expect(
      await db
        .selectFrom('colony_skin_equipment')
        .selectAll()
        .where('account_id', '=', account.id)
        .execute(),
    ).toEqual([]);
  });

  it('leaves out a stored swarm mesh this release does not know', async () => {
    const db = database.db;
    const account = await db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: 'Future' })
      .returning('id')
      .executeTakeFirstOrThrow();
    const hash = 'e'.repeat(64);
    await db
      .insertInto('blobs')
      .values({ sha256: hash, size: 100, content_type: 'image/png', storage_key: `k/${hash}` })
      .execute();
    const skin = await db
      .insertInto('colony_skins')
      .values({
        kind: 'custom',
        owner_account_id: account.id,
        name: 'Newer shape',
        entitlement: 'skins:designer',
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    // As written by a newer release, then read after a rollback.
    const version = await db
      .insertInto('colony_skin_versions')
      .values({
        skin_id: skin.id,
        texture_sha256: hash,
        material_sha256: hash,
        layout: 'colony-v2',
        building_color: 0x123456,
        swarm_mesh: 'pyramid',
        manifest_sha256: 'd'.repeat(64),
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    await db
      .insertInto('entitlements')
      .values({ account_id: account.id, entitlement: 'skins:designer', source: 'test' })
      .execute();
    await equipSkin(db, account.id, version.id);
    const setup = JSON.parse(
      readFileSync(
        new URL(
          '../../../packages/protocol/fixtures/valid/MatchSetup/catalog-1v1.json',
          import.meta.url,
        ),
        'utf8',
      ),
    ) as MatchSetup;
    setup.seats = [{ seat: 0, team: 0, kind: 'human', accountId: account.id, name: 'Future' }];
    const match = await db
      .insertInto('matches')
      .values({
        sim_version: simVersionKey(setup.simVersion),
        origin: 'room',
        setup: JSON.stringify(setup),
        seed: setup.seed,
        map_hash: setup.map.hash,
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    const keys = SigningKeys.ephemeral();
    // A cosmetic the API cannot sign must not block the match's appearances.
    expect(await matchColonySkins(db, keys, 'https://play.test', match.id)).toEqual([]);
    expect(
      (
        await db
          .selectFrom('matches')
          .select('skins_frozen_at')
          .where('id', '=', match.id)
          .executeTakeFirstOrThrow()
      ).skins_frozen_at,
    ).not.toBeNull();
  });
});
