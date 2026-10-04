// Self-service account deletion: confirmation, sign-out everywhere, and the
// player's names scrubbed from match setups, chat, room names and the audit
// log (ids kept), with unsettled matches finished later by the worker.
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { sql } from 'kysely';
import { DEFAULT_INSTANCE_CONFIG } from '@glob2/core';
import { scrubSettledMatchNames } from '@glob2/play';
import { registeredPlayer, type Player } from './playSupport.ts';
import { SIM, createHarness, postJson, type Harness, type Instance } from './support.ts';

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

const bearer = (player: Player) => ({
  authorization: `Bearer ${player.accessToken}`,
  'content-type': 'application/json',
});

function deleteMe(player: Player, confirmDisplayName: string) {
  return fetch(`${api.url}/api/v1/accounts/me`, {
    method: 'DELETE',
    headers: bearer(player),
    body: JSON.stringify({ confirmDisplayName }),
  });
}

/** A match with both players seated, as the platform stores it. */
async function match(players: Player[], status: 'ended' | 'running', verification: string) {
  const db = harness.database.db;
  const setup = {
    schemaVersion: 1,
    seats: players.map((p, i) => ({
      kind: 'human',
      name: p.displayName,
      accountId: p.accountId,
      team: i,
    })),
  };
  const row = await db
    .insertInto('matches')
    .values({
      sim_version: SIM_KEY,
      origin: 'room',
      status,
      verification: verification as 'pending',
      setup: JSON.stringify(setup),
      seed: 1,
      map_hash: 'ab'.repeat(32),
    })
    .returning('id')
    .executeTakeFirstOrThrow();
  await db
    .insertInto('match_participants')
    .values(
      players.map((p, i) => ({
        match_id: row.id,
        seat: i,
        team: i,
        kind: 'human' as const,
        account_id: p.accountId,
        display_name: p.displayName,
      })),
    )
    .execute();
  await db
    .insertInto('engine_jobs')
    .values({
      kind: 'verify-match',
      sim_version: SIM_KEY,
      payload: JSON.stringify({ matchId: row.id, setup, recordHash: 'cd'.repeat(32) }),
      status: verification === 'pending' ? 'queued' : 'succeeded',
    })
    .execute();
  return row.id;
}

async function seatNames(matchId: string) {
  const db = harness.database.db;
  const m = await db
    .selectFrom('matches')
    .select('setup')
    .where('id', '=', matchId)
    .executeTakeFirstOrThrow();
  const job = await db
    .selectFrom('engine_jobs')
    .select('payload')
    .where(sql<string>`payload ->> 'matchId'`, '=', matchId)
    .executeTakeFirstOrThrow();
  const names = (setup: unknown) =>
    (setup as { seats: { name: string; accountId: string }[] }).seats.map((s) => [
      s.accountId,
      s.name,
    ]);
  return { setup: names(m.setup), job: names((job.payload as { setup: unknown }).setup) };
}

describe('deleting my account', () => {
  it('needs the display name typed exactly', async () => {
    const player = await registeredPlayer(api, 'Careful');
    const wrong = await deleteMe(player, 'careful');
    expect(wrong.status).toBe(400);
    expect(((await wrong.json()) as { details: { reason: string } }).details.reason).toBe(
      'confirmation_mismatch',
    );
    const me = await fetch(`${api.url}/api/v1/accounts/me`, { headers: bearer(player) });
    expect(me.status).toBe(200);
    player.client.close();
  });

  it('removes private drafts and disables published skins while preserving immutable versions', async () => {
    const db = harness.database.db;
    const painter = await registeredPlayer(api, 'LeavingPainter');
    const preset = await db
      .selectFrom('colony_skin_versions')
      .selectAll()
      .executeTakeFirstOrThrow();
    const skin = await db
      .insertInto('colony_skins')
      .values({
        owner_account_id: painter.accountId,
        kind: 'custom',
        name: 'Personal paint',
        entitlement: 'skins:designer',
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    const version = await db
      .insertInto('colony_skin_versions')
      .values({
        skin_id: skin.id,
        texture_sha256: preset.texture_sha256,
        material_sha256: preset.material_sha256,
        layout: 'colony-v2',
        building_color: 123,
        manifest_sha256: 'fe'.repeat(32),
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    await db
      .insertInto('colony_skin_equipment')
      .values({ account_id: painter.accountId, version_id: version.id })
      .execute();
    await db
      .insertInto('colony_skin_drafts')
      .values({
        account_id: painter.accountId,
        revision: crypto.randomUUID(),
        name: 'Private paint',
        building_color: 123,
        image: Buffer.from('private draft'),
        material: Buffer.from('private material'),
      })
      .execute();
    expect((await deleteMe(painter, painter.displayName)).status).toBe(204);
    expect(
      await db
        .selectFrom('colony_skin_drafts')
        .selectAll()
        .where('account_id', '=', painter.accountId)
        .execute(),
    ).toEqual([]);
    expect(
      await db
        .selectFrom('colony_skin_equipment')
        .selectAll()
        .where('account_id', '=', painter.accountId)
        .execute(),
    ).toEqual([]);
    expect(
      await db
        .selectFrom('colony_skins')
        .select(['name', 'disabled_at'])
        .where('id', '=', skin.id)
        .executeTakeFirstOrThrow(),
    ).toEqual({ name: 'Deleted skin', disabled_at: expect.any(Date) });
    for (const route of ['texture', 'material'])
      expect((await fetch(`${api.url}/api/v1/skins/versions/${version.id}/${route}`)).status).toBe(
        404,
      );
    expect(
      await db
        .selectFrom('colony_skin_versions')
        .select('id')
        .where('id', '=', version.id)
        .executeTakeFirst(),
    ).toBeDefined();
    painter.client.close();
  });

  it('signs out everywhere and scrubs the name, keeping ids', async () => {
    const db = harness.database.db;
    const leaver = await registeredPlayer(api, 'Leaver');
    const friend = await registeredPlayer(api, 'Friend');
    const name = leaver.displayName;

    const settled = await match([leaver, friend], 'ended', 'verified');
    const running = await match([leaver, friend], 'running', 'pending');
    const room = await db
      .insertInto('rooms')
      .values({
        code: `DEL${Math.floor(Math.random() * 1e6)}`,
        name: `${name}'s room`,
        visibility: 'link',
        host_account_id: leaver.accountId,
        sim_version: SIM_KEY,
        settings: '{}',
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    await db
      .insertInto('room_chat_messages')
      .values([
        { room_id: room.id, account_id: leaver.accountId, text: 'hello all' },
        { room_id: room.id, account_id: friend.accountId, text: `gg ${name}, rematch?` },
        { room_id: room.id, account_id: friend.accountId, text: `${name}s are not names` },
      ])
      .execute();
    await db
      .insertInto('admin_audit_log')
      .values({
        action: 'account.mute',
        target_type: 'account',
        target_id: leaver.accountId,
        details: JSON.stringify({ minutes: 10, reason: `${name} spammed chat` }),
      })
      .execute();

    const { subject: username } = await db
      .selectFrom('identities')
      .select('subject')
      .where('account_id', '=', leaver.accountId)
      .executeTakeFirstOrThrow();

    expect((await deleteMe(leaver, ` ${name} `)).status).toBe(204);
    // Every session is gone: the access token's account is deleted, the socket closed.
    expect((await fetch(`${api.url}/api/v1/accounts/me`, { headers: bearer(leaver) })).status).toBe(
      401,
    );
    expect((await leaver.client.waitClosed()) !== undefined).toBe(true);
    const relogin = await postJson(`${api.url}/api/v1/auth/local/sign-in`, {
      username,
      password: 'correct horse battery staple',
      platform: 'desktop',
    });
    expect(relogin.status).toBe(401);

    const account = await db
      .selectFrom('accounts')
      .selectAll()
      .where('id', '=', leaver.accountId)
      .executeTakeFirstOrThrow();
    expect(account).toMatchObject({ status: 'deleted', display_name: 'Deleted player' });
    expect(account.deleted_at).not.toBeNull();
    expect(
      await db
        .selectFrom('identities')
        .select('id')
        .where('account_id', '=', leaver.accountId)
        .execute(),
    ).toEqual([]);

    // Settled match: setup and verify job scrubbed now; the friend keeps their name.
    expect(await seatNames(settled)).toEqual({
      setup: [
        [leaver.accountId, 'Deleted player'],
        [friend.accountId, friend.displayName],
      ],
      job: [
        [leaver.accountId, 'Deleted player'],
        [friend.accountId, friend.displayName],
      ],
    });
    // Running match: left for the verifier, then scrubbed once settled.
    expect((await seatNames(running)).setup[0]).toEqual([leaver.accountId, name]);
    expect(await scrubSettledMatchNames(db)).toBe(0);
    await db
      .updateTable('matches')
      .set({ status: 'ended', verification: 'verified' })
      .where('id', '=', running)
      .execute();
    await db
      .updateTable('engine_jobs')
      .set({ status: 'succeeded' })
      .where(sql<string>`payload ->> 'matchId'`, '=', running)
      .execute();
    expect(await scrubSettledMatchNames(db)).toBe(1);
    expect(await seatNames(running)).toMatchObject({
      setup: [[leaver.accountId, 'Deleted player'], expect.anything()],
      job: [[leaver.accountId, 'Deleted player'], expect.anything()],
    });
    expect(await db.selectFrom('account_name_scrubs').selectAll().execute()).toEqual([]);

    const participants = await db
      .selectFrom('match_participants')
      .select(['account_id', 'display_name'])
      .where('account_id', '=', leaver.accountId)
      .execute();
    expect(participants.map((p) => p.display_name)).toEqual(['Deleted player', 'Deleted player']);

    // Chat: own messages deleted; the name in others' messages replaced (whole words only).
    const chat = await db
      .selectFrom('room_chat_messages')
      .select(['account_id', 'text'])
      .where('room_id', '=', room.id)
      .orderBy('sent_at')
      .execute();
    expect(chat.map((c) => c.text).sort()).toEqual(
      ['gg Deleted player, rematch?', `${name}s are not names`].sort(),
    );
    const roomRow = await db
      .selectFrom('rooms')
      .select('name')
      .where('id', '=', room.id)
      .executeTakeFirstOrThrow();
    expect(roomRow.name).toBe("Deleted player's room");

    // Audit log (read as the migrator: the API may not change it, and the
    // worker may not read it): names gone, ids and actions kept.
    const audit = await harness.database
      .as('migrator')
      .db.selectFrom('admin_audit_log')
      .select(['action', 'actor_account_id', 'details'])
      .where('target_id', '=', leaver.accountId)
      .orderBy('created_at')
      .execute();
    expect(audit.map((a) => a.action)).toEqual(['account.mute', 'account.delete']);
    expect(JSON.stringify(audit)).not.toContain(name);
    expect(audit[0]!.details).toMatchObject({
      minutes: 10,
      reason: 'Deleted player spammed chat',
      scrubbed: true,
    });
    expect(audit[1]!.actor_account_id).toBe(leaver.accountId);
    expect(audit[1]!.details).toMatchObject({ self: true, kind: 'registered' });

    // The username is free again.
    const reused = await postJson(`${api.url}/api/v1/auth/local/register`, {
      username,
      password: 'another long password',
      platform: 'desktop',
    });
    expect(reused.status).toBe(200);
    friend.client.close();
  });
});
