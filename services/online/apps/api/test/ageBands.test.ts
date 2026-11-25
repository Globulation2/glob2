// Age bands: the one-time age question and platform signals, and the
// server-side restriction of social features for under-13 and undeclared
// accounts: room chat (sending and receiving), match chat (the ticket's chat
// claim), sharing content, renaming and photos.
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { DEFAULT_INSTANCE_CONFIG } from '@glob2/core';
import { checkDocument as check } from '@glob2/protocol';
import { allHaveSocialFeatures, mayChangeAgeBand } from '../src/auth/ageGate.ts';
import {
  FakeEngine,
  RELAY_KEY,
  declareAge,
  guestPlayer,
  registerRelay,
  registeredPlayer,
  roomState,
  serveSim,
  verifyTicket,
  type Player,
} from './playSupport.ts';
import { createHarness, json, postJson, type Harness, type Instance } from './support.ts';

const GENERATOR = {
  generatorId: 'even-ground',
  revision: 2,
  params: { width: 7, height: 7, teams: 2 },
  seed: 99,
  candidates: 5,
  startingUnitLevel: 0,
};

let harness: Harness;
let api: Instance;
let engine: FakeEngine;
const players: Player[] = [];

beforeAll(async () => {
  harness = await createHarness();
  await serveSim(harness.database.db);
  api = await harness.start({
    relayKeys: [{ key: RELAY_KEY }],
    instance: {
      auth: { ...DEFAULT_INSTANCE_CONFIG.auth, local: { enabled: true, allowRegistration: true } },
      // Many players connect from one address in this file.
      limits: { authPerMinute: 1000, guestsPerHour: 1000, realtimeConnectionsPerIp: 1000 },
    },
  });
  engine = new FakeEngine(harness.database.db, harness.blobs);
});

afterAll(async () => {
  engine?.stop();
  for (const player of players) player.client.close();
  await harness?.close();
});

const me = async (player: Player) =>
  json(
    await fetch(`${api.url}/api/v1/accounts/me`, {
      headers: { authorization: `Bearer ${player.accessToken}` },
    }),
  );

const send = (player: Player, method: string, path: string, body?: unknown) =>
  fetch(`${api.url}${path}`, {
    method,
    headers: {
      authorization: `Bearer ${player.accessToken}`,
      ...(body === undefined ? {} : { 'content-type': 'application/json' }),
    },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }),
  });

async function guest(age: 'unknown' | 'under_13' | '13_plus') {
  const p = await guestPlayer(api, undefined, age);
  players.push(p);
  return p;
}

async function registered(name: string, age: 'unknown' | 'under_13' | '13_plus') {
  const p = await registeredPlayer(api, name, age);
  players.push(p);
  return p;
}

describe('age band transitions', () => {
  it('lets moving to under 13 always, and leaving it only by the platform that set it or a moderator', () => {
    expect(mayChangeAgeBand('unknown', null, '13_plus', 'self')).toBe(true);
    expect(mayChangeAgeBand('unknown', null, 'under_13', 'self')).toBe(true);
    expect(mayChangeAgeBand('13_plus', 'self', 'under_13', 'self')).toBe(true);
    expect(mayChangeAgeBand('13_plus', 'self', 'under_13', 'apple')).toBe(true);
    expect(mayChangeAgeBand('under_13', 'self', '13_plus', 'self')).toBe(false);
    expect(mayChangeAgeBand('under_13', 'self', '13_plus', 'apple')).toBe(false);
    expect(mayChangeAgeBand('under_13', 'apple', '13_plus', 'apple')).toBe(true);
    expect(mayChangeAgeBand('under_13', 'apple', '13_plus', 'google_play')).toBe(false);
    expect(mayChangeAgeBand('under_13', 'google_play', '13_plus', 'google_play')).toBe(true);
    expect(mayChangeAgeBand('under_13', 'self', '13_plus', 'moderator')).toBe(true);
  });

  it('starts unknown without social features and records the answer', async () => {
    const p = await guest('unknown');
    const before = await me(p);
    expect(check('SelfAccount', before).stage).toBe('ok');
    expect(before).toMatchObject({ ageBand: 'unknown', socialFeatures: false });
    expect(before['ageBandSource']).toBeUndefined();

    const answered = await declareAge(api, p.accessToken, '13_plus');
    expect(answered.status).toBe(200);
    expect(await json(answered)).toMatchObject({
      ageBand: '13_plus',
      ageBandSource: 'self',
      socialFeatures: true,
    });
    const row = await harness.database.db
      .selectFrom('accounts')
      .select(['age_band', 'age_band_source', 'age_band_at'])
      .where('id', '=', p.accountId)
      .executeTakeFirstOrThrow();
    expect(row).toMatchObject({ age_band: '13_plus', age_band_source: 'self' });
    expect(row.age_band_at).toBeInstanceOf(Date);
  });

  it('keeps a self-declared under 13 locked against the player answering again', async () => {
    const p = await guest('under_13');
    const again = await declareAge(api, p.accessToken, '13_plus');
    expect(again.status).toBe(403);
    expect(await json(again)).toMatchObject({
      code: 'forbidden',
      details: { reason: 'age_band_locked' },
    });
    // Nor can the client claim a platform signal to lift it.
    expect((await declareAge(api, p.accessToken, '13_plus', 'apple')).status).toBe(403);
    expect(await me(p)).toMatchObject({ ageBand: 'under_13', socialFeatures: false });
  });

  it('follows a platform signal that set under 13 when the same platform later reports 13 plus', async () => {
    const p = await guest('unknown');
    expect((await declareAge(api, p.accessToken, 'under_13', 'google_play')).status).toBe(200);
    expect((await declareAge(api, p.accessToken, '13_plus', 'self')).status).toBe(403);
    const grown = await declareAge(api, p.accessToken, '13_plus', 'google_play');
    expect(grown.status).toBe(200);
    expect(await json(grown)).toMatchObject({ ageBand: '13_plus', ageBandSource: 'google_play' });
  });

  it('counts a match as chat-safe only when every human account exists and is 13 plus', async () => {
    const a = await guest('13_plus');
    const b = await guest('13_plus');
    const db = harness.database.db;
    expect(await allHaveSocialFeatures(db, [a.accountId, b.accountId, a.accountId])).toBe(true);
    expect(
      await allHaveSocialFeatures(db, [a.accountId, '00000000-0000-4000-8000-000000000000']),
    ).toBe(false);
    expect(await allHaveSocialFeatures(db, [])).toBe(true);
  });

  it('keeps a self-declared under 13 locked against a relayed platform claim', async () => {
    const p = await guest('under_13');
    // Re-declaring under 13 "from Apple" must not hand the lock to that claim...
    expect((await declareAge(api, p.accessToken, 'under_13', 'apple')).status).toBe(200);
    expect(await me(p)).toMatchObject({ ageBand: 'under_13', ageBandSource: 'self' });
    // ...so the same claim cannot lift it either.
    expect((await declareAge(api, p.accessToken, '13_plus', 'apple')).status).toBe(403);
  });

  it('rejects an unknown band or source', async () => {
    const p = await guest('unknown');
    expect(
      (await send(p, 'PUT', '/api/v1/accounts/me/age-band', { band: 'unknown', source: 'self' }))
        .status,
    ).toBe(400);
    expect(
      (
        await send(p, 'PUT', '/api/v1/accounts/me/age-band', {
          band: '13_plus',
          source: 'moderator',
        })
      ).status,
    ).toBe(400);
  });

  it('lets a moderator change a locked band, with an audit entry', async () => {
    const child = await guest('under_13');
    const moderator = await registered('Mod', '13_plus');
    await harness.database.db
      .updateTable('accounts')
      .set({ role: 'moderator' })
      .where('id', '=', moderator.accountId)
      .execute();
    const changed = await send(
      moderator,
      'POST',
      `/api/v1/admin/accounts/${child.accountId}/age-band`,
      {
        band: '13_plus',
        reason: 'Parent confirmed the age by e-mail.',
      },
    );
    expect(changed.status).toBe(200);
    expect(await json(changed)).toMatchObject({ ageBand: '13_plus', ageBandSource: 'moderator' });
    const audit = await harness.database.db
      .selectFrom('admin_audit_log')
      .select(['action'])
      .where('target_id', '=', child.accountId)
      .execute();
    expect(audit.map((a) => a.action)).toContain('account.age_band');
    // Ordinary players cannot.
    const other = await guest('13_plus');
    expect(
      (
        await send(other, 'POST', `/api/v1/admin/accounts/${child.accountId}/age-band`, {
          band: '13_plus',
        })
      ).status,
    ).toBe(403);
  });
});

describe('room and match chat', () => {
  let host: Player;
  let child: Player;
  let undeclared: Player;
  let roomId: string;
  let code: string;

  it('refuses room chat from under-13 and undeclared members and delivers none to them', async () => {
    host = await guest('13_plus');
    child = await guest('under_13');
    undeclared = await guest('unknown');
    const created = await host.client.ok('room.create', {
      name: 'Family game',
      visibility: 'link',
      map: { kind: 'generated', generator: GENERATOR },
      regions: [{ region: 'eu-west', rttMs: 30 }],
    });
    const room = created['room'] as { id: string; code: string };
    roomId = room.id;
    code = room.code;
    await child.client.ok('room.join', { code, regions: [{ region: 'eu-west', rttMs: 30 }] });
    await undeclared.client.ok('room.join', { code, regions: [{ region: 'eu-west', rttMs: 30 }] });

    for (const p of [child, undeclared]) {
      const refused = await p.client.call('room.chat', { roomId, text: 'hello' });
      expect(refused.error?.code).toBe('forbidden');
      expect(refused.error?.details).toMatchObject({ reason: 'age_restricted' });
    }

    child.client.clear();
    undeclared.client.clear();
    host.client.clear();
    await host.client.ok('room.chat', { roomId, text: 'hi all' });
    // The sender (13_plus) gets its own message; restricted members get nothing.
    expect((await host.client.event('room.chat'))['message']).toMatchObject({ text: 'hi all' });
    await expect(child.client.event('room.chat', 500)).rejects.toThrow('timed out');
    await expect(undeclared.client.event('room.chat', 500)).rejects.toThrow('timed out');
  });

  it('applies an answer at once: a member who declares 13 plus can chat without signing in again', async () => {
    expect((await declareAge(api, undeclared.accessToken, '13_plus')).status).toBe(200);
    host.client.clear();
    await undeclared.client.ok('room.chat', { roomId, text: 'now I can' });
    expect((await host.client.event('room.chat'))['message']).toMatchObject({ text: 'now I can' });
    await expect(child.client.event('room.chat', 500)).rejects.toThrow('timed out');
  });

  it('issues every ticket of a match with an under-13 player with chat off', async () => {
    // Two seats: the host and the child; the third member watches.
    await undeclared.client.ok('room.leave', { roomId });
    expect(await engine.runPending()).toBe(1);
    await roomState(host.client, (r) => r['mapStatus'] === 'ready');
    await child.client.ok('room.setReady', { roomId, ready: true });
    await registerRelay(api, 'relay-age-1', { region: 'eu-west' });
    host.client.clear();
    child.client.clear();
    const started = await host.client.ok('room.start', { roomId });
    for (const p of [host, child]) {
      const assignment = await p.client.event('match.start');
      expect(assignment['matchId']).toBe(started['matchId']);
      expect(assignment['chat']).toBe(false);
      const claims = (await verifyTicket(api, assignment['ticket'] as string)) as {
        chat?: boolean;
      };
      expect(claims.chat).toBe(false);
    }
  });

  it('issues tickets without a chat claim when every player is 13 plus', async () => {
    const a = await guest('13_plus');
    const b = await guest('13_plus');
    const created = await a.client.ok('room.create', {
      name: 'Adults',
      visibility: 'link',
      map: { kind: 'generated', generator: GENERATOR },
      regions: [{ region: 'eu-west', rttMs: 30 }],
    });
    const room = created['room'] as { id: string; code: string };
    await b.client.ok('room.join', {
      code: room.code,
      regions: [{ region: 'eu-west', rttMs: 30 }],
    });
    await engine.runPending();
    await roomState(a.client, (r) => r['mapStatus'] === 'ready');
    await b.client.ok('room.setReady', { roomId: room.id, ready: true });
    a.client.clear();
    b.client.clear();
    await a.client.ok('room.start', { roomId: room.id });
    for (const p of [a, b]) {
      const assignment = await p.client.event('match.start');
      expect(assignment['chat']).toBeUndefined();
      const claims = (await verifyTicket(api, assignment['ticket'] as string)) as {
        chat?: boolean;
      };
      expect(claims.chat).toBeUndefined();
    }
  });
});

describe('sharing, names and photos', () => {
  const createMap = (p: Player, visibility: string) =>
    send(p, 'POST', '/api/v1/maps', { title: `Map ${visibility}`, visibility });

  it('makes a new map private by default for an under-13 player', async () => {
    const child = await registered('Kiddo', 'under_13');
    const made = await send(child, 'POST', '/api/v1/maps', { title: 'No visibility given' });
    expect(made.status).toBe(201);
    expect(await json(made)).toMatchObject({ visibility: 'private' });
  });

  it('refuses sharing a building family before looking it up', async () => {
    const child = await registered('Builder', 'under_13');
    const missing = '00000000-0000-4000-8000-000000000000';
    const refused = await send(child, 'PATCH', `/api/v1/buildings/${missing}`, {
      visibility: 'public',
    });
    expect(refused.status).toBe(403);
    expect(await json(refused)).toMatchObject({ details: { reason: 'age_restricted' } });
    const adult = await registered('Builder', '13_plus');
    expect(
      (await send(adult, 'PATCH', `/api/v1/buildings/${missing}`, { visibility: 'public' })).status,
    ).toBe(404);
  });

  it('lets an under-13 player host by invite link but not list a public room', async () => {
    const child = await guest('under_13');
    const listed = await child.client.call('room.create', {
      name: 'Anyone welcome',
      visibility: 'public',
      map: { kind: 'generated', generator: GENERATOR },
      regions: [{ region: 'eu-west', rttMs: 30 }],
    });
    expect(listed.error?.code).toBe('forbidden');
    expect(listed.error?.details).toMatchObject({ reason: 'age_restricted' });
    const linked = await child.client.ok('room.create', {
      name: 'Friends only',
      visibility: 'link',
      map: { kind: 'generated', generator: GENERATOR },
      regions: [{ region: 'eu-west', rttMs: 30 }],
    });
    const room = linked['room'] as { id: string; revision: number };
    const relisted = await child.client.call('room.update', {
      roomId: room.id,
      revision: room.revision,
      changes: { visibility: 'public' },
    });
    expect(relisted.error?.code).toBe('forbidden');
  });

  it('lets an under-13 player keep maps private but not share them', async () => {
    const child = await registered('Kid', 'under_13');
    for (const visibility of ['public', 'unlisted']) {
      const refused = await createMap(child, visibility);
      expect(refused.status).toBe(403);
      expect(await json(refused)).toMatchObject({ details: { reason: 'age_restricted' } });
    }
    const own = await createMap(child, 'private');
    expect(own.status).toBe(201);
    const id = (await json(own))['id'] as string;
    expect(
      (await send(child, 'PATCH', `/api/v1/maps/${id}`, { visibility: 'unlisted' })).status,
    ).toBe(403);
  });

  it('makes shared content private and drops the photo when an account becomes under 13', async () => {
    const p = await registered('Grownup', '13_plus');
    const shared = await createMap(p, 'public');
    expect(shared.status).toBe(201);
    const id = (await json(shared))['id'] as string;
    await harness.database.db
      .updateTable('accounts')
      .set({ avatar_source: 'automatic' })
      .where('id', '=', p.accountId)
      .execute();
    expect((await declareAge(api, p.accessToken, 'under_13')).status).toBe(200);
    const map = await harness.database.db
      .selectFrom('maps')
      .select('visibility')
      .where('id', '=', id)
      .executeTakeFirstOrThrow();
    expect(map.visibility).toBe('private');
    expect(await me(p)).toMatchObject({ avatarSource: 'initials', canRename: false });
  });

  it('refuses renaming and photos without social features', async () => {
    const p = await registered('Unsure', 'unknown');
    const rename = await send(p, 'PATCH', '/api/v1/accounts/me', { displayName: 'NewName' });
    expect(rename.status).toBe(403);
    expect(await json(rename)).toMatchObject({ details: { reason: 'age_restricted' } });
    const automatic = await send(p, 'PATCH', '/api/v1/accounts/me/avatar', { source: 'automatic' });
    expect(automatic.status).toBe(403);
    expect(
      (await send(p, 'PATCH', '/api/v1/accounts/me/avatar', { source: 'initials' })).status,
    ).toBe(200);
    // A new guest without an answer still plays online: it may join quick play.
    const visitor = await postJson(`${api.url}/api/v1/auth/guest`, { platform: 'desktop' });
    expect(visitor.status).toBe(200);
  });
});
