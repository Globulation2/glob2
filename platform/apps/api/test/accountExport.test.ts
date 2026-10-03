// "Download my data" (GET /api/v1/accounts/me/export): the owner gets every
// stored row about the account, other people's data and secrets stay out,
// and every account column in the schema is either exported or deliberately
// left out (so a new table cannot silently escape the export).
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { sql } from 'kysely';
import { createHash, randomUUID } from 'node:crypto';
import { DEFAULT_INSTANCE_CONFIG } from '@glob2/core';
import { ACCOUNT_EXPORT_FORMAT, AccountExport, schemaIssues } from '@glob2/protocol';
import { EXPORTED_ACCOUNT_COLUMNS, UNEXPORTED_ACCOUNT_COLUMNS } from '../src/auth/accountExport.ts';
import { registeredPlayer, type Player } from './playSupport.ts';
import { SIM, createHarness, type Harness, type Instance } from './support.ts';

let harness: Harness;
let api: Instance;
const SIM_KEY = `${SIM.versionMinor}-${SIM.netProtocol}-${SIM.dataHash}`;

beforeAll(async () => {
  harness = await createHarness();
  api = await harness.start({
    instance: {
      auth: { ...DEFAULT_INSTANCE_CONFIG.auth, local: { enabled: true, allowRegistration: true } },
    },
  });
});

afterAll(async () => {
  await harness?.close();
});

const exportOf = (player: Player) =>
  fetch(`${api.url}/api/v1/accounts/me/export`, {
    headers: { authorization: `Bearer ${player.accessToken}` },
  });

/** Stores one of everything about `owner`, with `other` in the same match and room. */
async function seed(owner: Player, other: Player) {
  const db = harness.database.db;
  const setup = {
    schemaVersion: 1,
    seats: [owner, other].map((p, i) => ({
      kind: 'human',
      name: p.displayName,
      accountId: p.accountId,
      team: i,
    })),
  };
  const match = await db
    .insertInto('matches')
    .values({
      sim_version: SIM_KEY,
      origin: 'room',
      status: 'ended',
      verification: 'verified',
      rated: true,
      setup: JSON.stringify(setup),
      seed: 7,
      map_hash: 'ab'.repeat(32),
      end_reason: 'completed',
    })
    .returning('id')
    .executeTakeFirstOrThrow();
  const entity = await db
    .insertInto('rating_entities')
    .values({ kind: 'account', account_id: owner.accountId })
    .returning('id')
    .executeTakeFirstOrThrow();
  await db
    .insertInto('match_participants')
    .values([
      {
        match_id: match.id,
        seat: 0,
        team: 0,
        kind: 'human',
        account_id: owner.accountId,
        rating_entity_id: entity.id,
        display_name: owner.displayName,
        outcome: 'won',
        rating_before: 1500,
        rating_after: 1532,
        network: JSON.stringify({ rttMs: 42 }),
      },
      {
        match_id: match.id,
        seat: 1,
        team: 1,
        kind: 'human',
        account_id: other.accountId,
        display_name: other.displayName,
        outcome: 'lost',
      },
    ])
    .execute();
  await db
    .insertInto('ratings')
    .values({ entity_id: entity.id, ladder: 'ranked-1v1', mu: 27, sigma: 7, games: 1, wins: 1 })
    .execute();
  await db
    .insertInto('rating_history')
    .values({
      match_id: match.id,
      entity_id: entity.id,
      ladder: 'ranked-1v1',
      result: 'won',
      mu_before: 25,
      sigma_before: 8.3,
      mu_after: 27,
      sigma_after: 7,
      display_before: 1500,
      display_after: 1532,
    })
    .execute();

  const room = await db
    .insertInto('rooms')
    .values({
      code: `EXP${Math.floor(Math.random() * 1e6)}`,
      name: 'Export room',
      visibility: 'link',
      host_account_id: owner.accountId,
      sim_version: SIM_KEY,
      settings: JSON.stringify({ speed: 'normal' }),
    })
    .returning('id')
    .executeTakeFirstOrThrow();
  await db
    .insertInto('room_members')
    .values([
      { room_id: room.id, account_id: owner.accountId },
      { room_id: room.id, account_id: other.accountId },
    ])
    .execute();
  await db
    .insertInto('room_chat_messages')
    .values([
      { room_id: room.id, account_id: owner.accountId, text: 'my own words' },
      { room_id: room.id, account_id: other.accountId, text: 'someone else said this' },
    ])
    .execute();
  await db
    .insertInto('queue_tickets')
    .values({
      queue_id: 'ranked-1v1',
      account_id: owner.accountId,
      sim_version: SIM_KEY,
      region_rtts: JSON.stringify([{ region: 'ca-central', rttMs: 31 }]),
      status: 'cancelled',
    })
    .execute();

  const blob = randomUUID().replaceAll('-', '').repeat(2);
  await db
    .insertInto('blobs')
    .values({ sha256: blob, size: 10, content_type: 'application/octet-stream', storage_key: blob })
    .execute();
  const map = await db
    .insertInto('maps')
    .values({ owner_account_id: owner.accountId, title: 'My map', visibility: 'public' })
    .returning('id')
    .executeTakeFirstOrThrow();
  await db
    .insertInto('map_versions')
    .values({ map_id: map.id, hash: blob, size: 10, notes: 'first cut' })
    .execute();
  await db
    .insertInto('map_likes')
    .values({ map_id: map.id, account_id: owner.accountId })
    .execute();
  await db
    .insertInto('map_reports')
    .values({ map_id: map.id, reporter_account_id: owner.accountId, reason: 'other', details: 'x' })
    .execute();
  await db
    .insertInto('map_uploads')
    .values({
      owner_account_id: owner.accountId,
      blob_sha256: blob,
      format: 'map',
      sim_version: SIM_KEY,
      file_name: 'mine.map',
    })
    .execute();
  await db
    .insertInto('map_downloads')
    .values([
      { map_id: map.id, downloader: `a:${owner.accountId}` },
      { map_id: map.id, downloader: `a:${other.accountId}` },
    ])
    .execute();
  await db
    .insertInto('admin_audit_log')
    .values({
      actor_account_id: other.accountId,
      action: 'account.mute',
      target_type: 'account',
      target_id: owner.accountId,
      details: JSON.stringify({ minutes: 10 }),
    })
    .execute();
  const draftImage = Buffer.from(
    'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Y9ZQmcAAAAASUVORK5CYII=',
    'base64',
  );
  for (const player of [owner, other]) {
    const hash = createHash('sha256').update(player.accountId).digest('hex');
    await db
      .insertInto('blobs')
      .values({
        sha256: hash,
        size: draftImage.length,
        content_type: 'image/png',
        storage_key: `sha256/${hash}`,
      })
      .execute();
    const skin = await db
      .insertInto('colony_skins')
      .values({
        kind: 'custom',
        owner_account_id: player.accountId,
        name: `${player.displayName} paint`,
        entitlement: 'skins:designer',
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    const version = await db
      .insertInto('colony_skin_versions')
      .values({
        skin_id: skin.id,
        texture_sha256: hash,
        layout: 'colony-v1',
        building_color: 0x123456,
        manifest_sha256: createHash('sha256').update(`${hash}:manifest`).digest('hex'),
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    await db
      .insertInto('colony_skin_equipment')
      .values({ account_id: player.accountId, version_id: version.id, building_color: 0x654321 })
      .execute();
    await db
      .insertInto('colony_skin_drafts')
      .values({
        account_id: player.accountId,
        revision: randomUUID(),
        skin_id: skin.id,
        name: 'Private draft',
        building_color: 0x112233,
        image: draftImage,
      })
      .execute();
    await db
      .insertInto('match_colony_skins')
      .values({
        match_id: match.id,
        team_index: player === owner ? 0 : 1,
        account_id: player.accountId,
        version_id: version.id,
        building_color: 0x654321,
        assertion: 'private-signed-appearance',
      })
      .execute();
    const purchase = await db
      .insertInto('skin_purchases')
      .values({
        account_id: player.accountId,
        request_id: randomUUID(),
        sku: 'designer',
        entitlement: 'skins:designer',
        price_id: 'price_designer',
        status: 'paid',
        recovery_cursor: 'private-recovery-cursor',
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    await db
      .insertInto('skin_payment_events')
      .values({
        id: `evt_export_${player.accountId}`,
        event_type: 'checkout.session.completed',
        purchase_id: purchase.id,
      })
      .execute();
    await db
      .insertInto('colony_skin_reports')
      .values({
        version_id: version.id,
        reporter_account_id: player.accountId,
        reason: 'My report',
        resolution: 'dismissed',
        resolved_at: new Date(),
        resolved_by_account_id: other.accountId,
        resolution_reason: 'Reviewed',
      })
      .execute();
  }
  return { matchId: match.id, roomId: room.id, mapId: map.id };
}

describe('downloading my data', () => {
  it('needs a signed-in account', async () => {
    const response = await fetch(`${api.url}/api/v1/accounts/me/export`);
    expect(response.status).toBe(401);
  });

  it('returns everything stored about the account and nothing secret', async () => {
    const owner = await registeredPlayer(api, 'Exporter');
    const other = await registeredPlayer(api, 'Bystander');
    const { matchId, roomId, mapId } = await seed(owner, other);

    const response = await exportOf(owner);
    expect(response.status).toBe(200);
    expect(response.headers.get('content-type')).toMatch(/^application\/json/);
    expect(response.headers.get('content-disposition')).toMatch(
      /^attachment; filename="glob2-account-[0-9a-f]{8}-\d{4}-\d{2}-\d{2}\.json"$/,
    );
    expect(response.headers.get('cache-control')).toBe('no-store');
    const text = await response.text();
    const data = JSON.parse(text) as AccountExport;
    expect(schemaIssues(AccountExport, data)).toEqual([]);

    expect(data.format).toBe(ACCOUNT_EXPORT_FORMAT);
    expect(data.instance).toBe(api.url);
    expect(data.account).toMatchObject({
      id: owner.accountId,
      displayName: owner.displayName,
      kind: 'registered',
      role: 'user',
      status: 'active',
    });
    expect(data.signIn.identities).toEqual([
      expect.objectContaining({ provider: 'local', subject: expect.any(String) }),
    ]);
    expect(data.signIn.refreshTokens.length).toBeGreaterThan(0);
    expect(data.moderation).toEqual([
      expect.objectContaining({ action: 'account.mute', details: { minutes: 10 } }),
    ]);

    expect(data.matches).toEqual([
      expect.objectContaining({
        matchId,
        seat: 0,
        outcome: 'won',
        ratingBefore: 1500,
        ratingAfter: 1532,
        network: { rttMs: 42 },
        page: `${api.url}/matches/${matchId}`,
      }),
    ]);
    expect(data.ratings).toEqual([
      expect.objectContaining({ ladder: 'ranked-1v1', games: 1, wins: 1 }),
    ]);
    expect(data.ratingHistory).toEqual([
      expect.objectContaining({ matchId, displayBefore: 1500, displayAfter: 1532 }),
    ]);

    expect(data.rooms.hosted).toEqual([
      expect.objectContaining({ id: roomId, name: 'Export room', settings: { speed: 'normal' } }),
    ]);
    expect(data.rooms.memberships).toEqual([
      expect.objectContaining({ roomId, roomName: 'Export room' }),
    ]);
    expect(data.rooms.chat).toEqual([expect.objectContaining({ roomId, text: 'my own words' })]);
    expect(data.matchmaking.tickets).toEqual([
      expect.objectContaining({
        queueId: 'ranked-1v1',
        regionRtts: [{ region: 'ca-central', rttMs: 31 }],
      }),
    ]);

    expect(data.maps.published).toEqual([
      expect.objectContaining({
        id: mapId,
        title: 'My map',
        versions: [expect.objectContaining({ notes: 'first cut', size: 10 })],
      }),
    ]);
    expect(data.maps.likes).toEqual([expect.objectContaining({ mapId })]);
    expect(data.maps.reports).toEqual([expect.objectContaining({ mapId, reason: 'other' })]);
    expect(data.maps.uploads).toEqual([expect.objectContaining({ fileName: 'mine.map' })]);
    expect(data.maps.downloads).toEqual([
      { mapId, day: expect.stringMatching(/^\d{4}-\d{2}-\d{2}$/) },
    ]);

    const skins = data.skins!;
    expect(skins.published).toEqual([
      expect.objectContaining({
        name: `${owner.displayName} paint`,
        versions: [expect.objectContaining({ layout: 'colony-v1', buildingColor: 0x123456 })],
      }),
    ]);
    expect(skins.equipment).toEqual([expect.objectContaining({ buildingColor: 0x654321 })]);
    expect(skins.drafts).toEqual([
      expect.objectContaining({
        name: 'Private draft',
        contentType: 'image/png',
        imageBase64: expect.any(String),
      }),
    ]);
    expect(
      Buffer.from(skins.drafts[0]!['imageBase64'] as string, 'base64')
        .subarray(1, 4)
        .toString(),
    ).toBe('PNG');
    expect(skins.matches).toEqual([expect.objectContaining({ matchId, teamIndex: 0 })]);
    expect(skins.purchases).toEqual([expect.objectContaining({ sku: 'designer', status: 'paid' })]);
    expect(skins.paymentEvents).toEqual([
      expect.objectContaining({ eventType: 'checkout.session.completed' }),
    ]);
    expect(skins.reports).toEqual([
      expect.objectContaining({ reason: 'My report', resolutionReason: 'Reviewed' }),
    ]);
    expect(text).not.toContain('private-signed-appearance');
    expect(text).not.toContain('private-recovery-cursor');
    expect(text).not.toContain(`${other.displayName} paint`);

    // Nothing secret, and nothing that belongs to the other player.
    const db = harness.database.db;
    const secrets = [
      ...(
        await db
          .selectFrom('identities')
          .select('password_hash')
          .where('account_id', '=', owner.accountId)
          .execute()
      ).map((r) => r.password_hash),
      ...(
        await db
          .selectFrom('refresh_tokens')
          .select(['token_hash', 'family_id'])
          .where('account_id', '=', owner.accountId)
          .execute()
      ).flatMap((r) => [r.token_hash, r.family_id]),
    ].filter((s): s is string => !!s);
    expect(secrets.length).toBeGreaterThan(1);
    for (const secret of secrets) expect(text).not.toContain(secret);
    expect(text).not.toMatch(
      /passwordHash|tokenHash|credentialHash|resumeHash|bindingHash|confirmationCode|familyId/,
    );
    expect(text).not.toContain('someone else said this');
    expect(text).not.toContain(other.accountId);
    expect(text).not.toContain(owner.accessToken);

    // The other player's export has their own seat, not the owner's.
    const theirs = (await (await exportOf(other)).json()) as AccountExport;
    expect(theirs.matches).toEqual([
      expect.objectContaining({ matchId, seat: 1, outcome: 'lost' }),
    ]);
    expect(theirs.rooms.hosted).toEqual([]);
    expect(theirs.rooms.chat).toEqual([
      expect.objectContaining({ text: 'someone else said this' }),
    ]);
    expect(theirs.moderation).toEqual([]);

    owner.client.close();
    other.client.close();
  });

  it('exports owned Hive credits and sessions without live capabilities', async () => {
    const owner = await registeredPlayer(api, 'HiveExporter');
    const other = await registeredPlayer(api, 'OtherHiveExporter');
    const { matchId } = await seed(owner, other);
    const leases: string[] = [];
    for (const [seat, player] of [owner, other].entries()) {
      const session = randomUUID();
      const lease = randomUUID();
      leases.push(lease);
      await sql`INSERT INTO hive_wallets(account_id,balance,reserved) VALUES(${player.accountId},10,1)`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO hive_ledger(id,account_id,amount,kind) VALUES(${randomUUID()},${player.accountId},10,'grant')`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO hive_calls(id,account_id,reserved,status,rate) VALUES(${randomUUID()},${player.accountId},1,'reserved','{}')`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO hive_purchases(id,account_id,pack) VALUES(${randomUUID()},${player.accountId},'{}')`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO hive_sessions(id,account_id,match_id,seat,team,lease,client_id,run_id) VALUES(${session},${player.accountId},${matchId},${seat},${seat},${lease},${lease},${lease})`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO hive_events(session_id,dedup,kind,body) VALUES(${session},'one','command',${JSON.stringify({ text: player.displayName })}::jsonb)`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO hive_operations(id,session_id,generation,lease,status,request) VALUES(${randomUUID()},${session},0,${lease},'pending','{}')`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO hive_programs(session_id,id,revision,definition,status) VALUES(${session},${randomUUID()},1,'{}','active')`.execute(
        harness.database.db,
      );
    }
    for (const [player, excluded] of [
      [owner, other],
      [other, owner],
    ] as const) {
      const text = await (await exportOf(player)).text();
      const data = JSON.parse(text) as AccountExport;
      expect(schemaIssues(AccountExport, data)).toEqual([]);
      expect(data.hive).toBeDefined();
      for (const list of Object.values(data.hive!)) expect(list).toHaveLength(1);
      expect(data.hive!.wallets).toEqual([{ balance: 10, reserved: 1 }]);
      expect(data.hive!.events).toEqual([
        expect.objectContaining({ body: { text: player.displayName } }),
      ]);
      expect(text).not.toContain(excluded.accountId);
      expect(text).not.toContain(excluded.displayName);
      for (const lease of leases) expect(text).not.toContain(lease);
      expect(JSON.stringify(data.hive)).not.toMatch(/lease|clientId|runId/);
    }
    owner.client.close();
    other.client.close();
  });

  it('exports owned Studio conversations, revisions and separate credits without leases', async () => {
    const owner = await registeredPlayer(api, 'StudioExporter');
    const other = await registeredPlayer(api, 'OtherStudioExporter');
    const leases: string[] = [];
    for (const player of [owner, other]) {
      const thread = randomUUID();
      const request = randomUUID();
      const lease = randomUUID();
      leases.push(lease);
      await sql`INSERT INTO map_wallets(account_id,balance,reserved) VALUES(${player.accountId},3,0)`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO map_ledger(id,account_id,amount,kind) VALUES(${randomUUID()},${player.accountId},3,'grant')`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO map_calls(id,account_id,reserved,status,rate) VALUES(${randomUUID()},${player.accountId},1,'settled','{}')`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO map_purchases(id,account_id,pack) VALUES(${randomUUID()},${player.accountId},'{}')`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO studio_threads(id,account_id,title,brief) VALUES(${thread},${player.accountId},${player.displayName},'More rivers')`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO studio_messages(id,thread_id,role,text) VALUES(${randomUUID()},${thread},'user',${player.displayName})`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO studio_requests(id,thread_id,account_id,kind,status,input,checkpoints,lease,charged) VALUES(${request},${thread},${player.accountId},'generate','ready','{"players":2}','{"version":1}',${lease},true)`.execute(
        harness.database.db,
      );
      await sql`INSERT INTO studio_attempts(id,request_id,stage,model,status,input,output) VALUES(${randomUUID()},${request},'design','fixture','completed','{"feedback":"More rivers"}','{"layout":"rivers"}')`.execute(
        harness.database.db,
      );
    }
    for (const [player, excluded] of [
      [owner, other],
      [other, owner],
    ] as const) {
      const text = await (await exportOf(player)).text();
      const data = JSON.parse(text) as AccountExport;
      expect(schemaIssues(AccountExport, data)).toEqual([]);
      for (const list of Object.values(data.mapStudio!)) expect(list).toHaveLength(1);
      expect(data.mapStudio!.wallets).toEqual([{ balance: 3, reserved: 0 }]);
      expect(data.hive!.wallets).toEqual([]);
      expect(data.mapStudio!.messages).toEqual([
        expect.objectContaining({ text: player.displayName }),
      ]);
      expect(data.mapStudio!.requests).toEqual([
        expect.objectContaining({
          input: { players: 2 },
          checkpoints: { version: 1 },
          charged: true,
        }),
      ]);
      expect(data.mapStudio!.attempts).toEqual([
        expect.objectContaining({
          input: { feedback: 'More rivers' },
          output: { layout: 'rivers' },
        }),
      ]);
      expect(text).not.toContain(excluded.accountId);
      expect(text).not.toContain(excluded.displayName);
      for (const lease of leases) expect(text).not.toContain(lease);
      expect(JSON.stringify(data.mapStudio)).not.toMatch(/lease/);
    }
    owner.client.close();
    other.client.close();
  });

  it('is rate limited per account', async () => {
    const player = await registeredPlayer(api, 'Hoarder');
    const statuses: number[] = [];
    for (let i = 0; i < 11; i++) statuses.push((await exportOf(player)).status);
    expect(statuses.slice(0, 10).every((s) => s === 200)).toBe(true);
    expect(statuses[10]).toBe(429);
    player.client.close();
  });

  it('covers every column that refers to an account', async () => {
    const { rows } = await sql<{ table: string; column: string }>`
      SELECT c.conrelid::regclass::text AS table, a.attname AS column
      FROM pg_constraint c
      JOIN pg_attribute a ON a.attrelid = c.conrelid AND a.attnum = ANY (c.conkey)
      WHERE c.contype = 'f' AND c.confrelid = 'accounts'::regclass
      ORDER BY 1, 2`.execute(harness.database.db);
    expect(rows.length).toBeGreaterThan(20);
    const missing = rows
      .map(({ table, column }) => `${table}.${column}`)
      .filter((name) => {
        const [table, column] = name.split('.') as [string, string];
        return (
          !EXPORTED_ACCOUNT_COLUMNS[table]?.includes(column) && !UNEXPORTED_ACCOUNT_COLUMNS[name]
        );
      });
    expect(missing).toEqual([]);
  });
});
