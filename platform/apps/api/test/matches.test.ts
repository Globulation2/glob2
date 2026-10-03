// Matches end to end on the platform side: AccessPolicy denials, uploads and
// validation, relay registration/heartbeat/placement/draining, match-end
// intake, the queue MatchStarter, queue methods and NOTIFY forwarding.
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import type { AccessPolicy } from '@glob2/core';
import { resolveQueue } from '@glob2/core';
import { checkDocument as check, simVersionKey } from '@glob2/protocol';
import {
  PgQueueNotifier,
  PlatformMatchStarter,
  StartError,
  abortMatchesOnLostRelays,
  chooseRelay,
  createMatch,
  expireStartingMatches,
  queueMatchSetup,
  type MatchProposal,
  type RelayCandidate,
} from '@glob2/worker';
import {
  FakeEngine,
  PINNED_KEY,
  RELAY_KEY,
  fakeMapBytes,
  fakeSaveBytes,
  guestPlayer,
  registerRelay,
  registeredPlayer,
  relayCall,
  relayRegistration,
  roomState,
  serveSim,
  verifyTicket,
  waitUntil,
  type Player,
} from './playSupport.ts';
import { SIM, createHarness, json, type Harness, type Instance } from './support.ts';

const ORIGIN = 'http://play.test';
const SIM_KEY = simVersionKey(SIM);

let harness: Harness;
let a: Instance;
let b: Instance;
let engine: FakeEngine;
const players: Player[] = [];

const queues = [
  { id: 'casual-1v1', name: 'Casual 1v1', mode: '1v1' as const, rated: false },
  { id: 'ranked-1v1', name: 'Ranked 1v1', mode: '1v1' as const, rated: true },
];

beforeAll(async () => {
  harness = await createHarness();
  await serveSim(harness.database.db);
  const relayKeys = [{ key: RELAY_KEY }, { key: PINNED_KEY, relayId: 'relay-pinned' }];
  const instance = { queues, auth: { providers: [], local: { enabled: true } } };
  a = await harness.start({ origin: ORIGIN, relayKeys, instance });
  b = await harness.start({ origin: ORIGIN, relayKeys, instance });
  engine = new FakeEngine(harness.database.db, harness.blobs);
});

afterAll(async () => {
  engine?.stop();
  for (const player of players) player.client.close();
  await harness?.close();
});

async function player(instance: Instance = a): Promise<Player> {
  const p = await guestPlayer(instance);
  players.push(p);
  return p;
}

async function resetRelays(): Promise<void> {
  await harness.database.db.updateTable('matches').set({ relay_id: null }).execute();
  await harness.database.db.deleteFrom('relays').execute();
}

// ----------------------------------------------------------------- policy

describe('AccessPolicy', () => {
  it('denies hosting, joining and queueing through an injected policy', async () => {
    const deny = (what: string) => async () => ({
      allowed: false as const,
      reason: `${what} needs a supporter pass`,
      requiredEntitlement: 'supporter',
    });
    const denyAll: AccessPolicy = {
      name: 'deny-test',
      canHost: deny('hosting'),
      canJoin: deny('joining'),
      canQueue: deny('queueing'),
    };
    const denied = await harness.start({ origin: ORIGIN, access: denyAll, instance: { queues } });
    const host = await player(a);
    const room = (await host.client.ok('room.create', { name: 'Open', visibility: 'link' }))[
      'room'
    ] as { code: string };
    const p = await player(denied);
    const create = await p.client.call('room.create', { name: 'x', visibility: 'link' });
    expect(create.error).toMatchObject({
      code: 'access_denied',
      message: 'hosting needs a supporter pass',
      details: { requiredEntitlement: 'supporter' },
    });
    const join = await p.client.call('room.join', { code: room.code });
    expect(join.error?.code).toBe('access_denied');
    const queue = await p.client.call('queue.join', { queueId: 'casual-1v1', regions: [] });
    expect(queue.error?.code).toBe('access_denied');
    expect(
      await harness.database.db
        .selectFrom('queue_tickets')
        .select('id')
        .where('account_id', '=', p.accountId)
        .execute(),
    ).toEqual([]);
  });

  it('checks every seated player again at start', async () => {
    let allowJoin = true;
    const policy: AccessPolicy = {
      name: 'switchable',
      canHost: async () => ({ allowed: true }),
      canJoin: async () =>
        allowJoin ? { allowed: true } : { allowed: false, reason: 'joining closed' },
      canQueue: async () => ({ allowed: true }),
    };
    const gated = await harness.start({
      origin: ORIGIN,
      access: policy,
      relayKeys: [{ key: RELAY_KEY }],
    });
    await registerRelay(gated, 'relay-gate');
    const host = await player(gated);
    const guest = await player(gated);
    const room = (await host.client.ok('room.create', { name: 'Gate', visibility: 'link' }))[
      'room'
    ] as { id: string; code: string };
    await guest.client.ok('room.join', { code: room.code });
    await guest.client.ok('room.setSeat', { roomId: room.id, seat: 1, occupant: { kind: 'self' } });
    await guest.client.ok('room.setReady', { roomId: room.id, ready: true });
    await host.client.ok('room.update', {
      roomId: room.id,
      revision: (
        await harness.database.db
          .selectFrom('rooms')
          .select('revision')
          .where('id', '=', room.id)
          .executeTakeFirstOrThrow()
      ).revision,
      changes: { map: { kind: 'generated', generator: generator(2, 5) } },
    });
    await engine.runPending();
    await roomState(host.client, (r) => r['mapStatus'] === 'ready');
    await guest.client.ok('room.setReady', { roomId: room.id, ready: true });
    allowJoin = false;
    const start = await host.client.call('room.start', { roomId: room.id });
    expect(start.error).toMatchObject({ code: 'access_denied', details: { seat: 1 } });
    const status = await harness.database.db
      .selectFrom('rooms')
      .select('status')
      .where('id', '=', room.id)
      .executeTakeFirstOrThrow();
    expect(status.status).toBe('open');
    await resetRelays();
  });
});

function generator(teams: number, seed: number) {
  return {
    generatorId: 'even-ground',
    revision: 2,
    params: { width: 7, height: 7, teams },
    seed,
    candidates: 3,
    startingUnitLevel: 0,
  };
}

// ---------------------------------------------------------------- uploads

describe('uploads', () => {
  async function upload(p: Player, bytes: Buffer, format: 'map' | 'save', name = 'file') {
    return fetch(
      `${a.url}/api/v1/uploads?format=${format}&simVersion=${SIM_KEY}&fileName=${name}`,
      {
        method: 'POST',
        headers: {
          authorization: `Bearer ${p.accessToken}`,
          'content-type': 'application/octet-stream',
        },
        body: bytes,
      },
    );
  }

  it('stores a private map, validates it with an engine job and lets the owner use it in a room', async () => {
    const owner = await player();
    const response = await upload(owner, fakeMapBytes(4, 99), 'map', 'four.map');
    expect(response.status).toBe(201);
    const pending = await json(response);
    expect(check('MapUpload', pending).stage).toBe('ok');
    expect(pending).toMatchObject({ status: 'pending', format: 'map', fileName: 'four.map' });
    const hash = pending['sha256'] as string;

    await engine.runPending();
    const valid = await json(
      await fetch(`${a.url}/api/v1/uploads/${pending['id']}`, {
        headers: { authorization: `Bearer ${owner.accessToken}` },
      }),
    );
    expect(valid).toMatchObject({ status: 'valid', map: { teamCount: 4 } });
    // Uploading the same bytes again returns the same upload.
    const again = await json(await upload(owner, fakeMapBytes(4, 99), 'map'));
    expect(again['id']).toBe(pending['id']);

    // Private: others cannot see the upload or download the bytes...
    const other = await player(b);
    const peek = await fetch(`${a.url}/api/v1/uploads/${pending['id']}`, {
      headers: { authorization: `Bearer ${other.accessToken}` },
    });
    expect(peek.status).toBe(404);
    const anonymous = await fetch(`${a.url}/api/v1/blobs/maps/${hash}`);
    expect(anonymous.status).toBe(401);
    const stranger = await fetch(`${a.url}/api/v1/blobs/maps/${hash}`, {
      headers: { authorization: `Bearer ${other.accessToken}` },
    });
    expect(stranger.status).toBe(404);

    // ...until they are in a room playing it.
    const created = (
      await owner.client.ok('room.create', {
        name: 'Uploaded',
        visibility: 'link',
        map: { kind: 'upload', format: 'map', hash },
      })
    )['room'] as { code: string; seats: unknown[]; mapStatus: string; teams: unknown[] };
    expect(created.mapStatus).toBe('ready');
    expect(created.seats).toHaveLength(4);
    expect(created.teams).toHaveLength(4);
    await other.client.ok('room.join', { code: created.code });
    const member = await fetch(`${b.url}/api/v1/blobs/maps/${hash}`, {
      headers: { authorization: `Bearer ${other.accessToken}` },
    });
    expect(member.status).toBe(200);
    expect(Buffer.from(await member.arrayBuffer()).equals(fakeMapBytes(4, 99))).toBe(true);
    expect(member.headers.get('etag')).toBe(`"${hash}"`);
    // Someone else's upload cannot be chosen.
    const steal = await other.client.call('room.create', {
      name: 'x',
      visibility: 'link',
      map: { kind: 'upload', format: 'map', hash },
    });
    expect(steal.error?.code).toBe('bad_request');
  });

  it('reports the players of a save and rejects files the engine cannot load', async () => {
    const owner = await player();
    const save = await json(
      await upload(
        owner,
        fakeSaveBytes([
          { name: 'Alice', team: 0, kind: 'human' },
          { name: 'Cortex', team: 1, kind: 'ai' },
        ]),
        'save',
      ),
    );
    const junk = await json(await upload(owner, Buffer.from('not a map'), 'map'));
    await engine.runPending();
    const readSave = await json(
      await fetch(`${a.url}/api/v1/uploads/${save['id']}`, {
        headers: { authorization: `Bearer ${owner.accessToken}` },
      }),
    );
    expect(readSave).toMatchObject({
      status: 'valid',
      players: [
        { name: 'Alice', team: 0, kind: 'human' },
        { name: 'Cortex', team: 1, kind: 'ai' },
      ],
    });
    const readJunk = await json(
      await fetch(`${a.url}/api/v1/uploads/${junk['id']}`, {
        headers: { authorization: `Bearer ${owner.accessToken}` },
      }),
    );
    expect(readJunk).toMatchObject({ status: 'invalid', reason: 'not a Globulation 2 file' });
    const room = await owner.client.call('room.create', {
      name: 'x',
      visibility: 'link',
      map: { kind: 'upload', format: 'map', hash: junk['sha256'] },
    });
    expect(room.error?.code).toBe('bad_request');
    const reteamed = await owner.client.ok('room.create', {
      name: 'Saved game',
      visibility: 'link',
      map: {
        kind: 'upload',
        format: 'save',
        hash: save['sha256'],
        reteaming: [{ name: 'Alice', team: 0, accountId: owner.accountId }],
      },
    });
    expect((reteamed['room'] as { map: { reteaming: unknown[] } }).map.reteaming).toHaveLength(1);
  });

  it('refuses uploads for versions the instance does not serve and empty bodies', async () => {
    const owner = await player();
    const wrongVersion = await fetch(
      `${a.url}/api/v1/uploads?format=map&simVersion=1-1-${'00'.repeat(32)}`,
      {
        method: 'POST',
        headers: {
          authorization: `Bearer ${owner.accessToken}`,
          'content-type': 'application/octet-stream',
        },
        body: fakeMapBytes(2, 1),
      },
    );
    expect(wrongVersion.status).toBe(426);
    const anonymous = await fetch(`${a.url}/api/v1/uploads?format=map&simVersion=${SIM_KEY}`, {
      method: 'POST',
      headers: { 'content-type': 'application/octet-stream' },
      body: fakeMapBytes(2, 1),
    });
    expect(anonymous.status).toBe(401);
  });
});

// ----------------------------------------------------------------- relays

describe('relays', () => {
  it('chooses the relay closest to the worst-placed player, else the least loaded', () => {
    const relay = (id: string, region: string, load = 0, maxMatches = 10): RelayCandidate => ({
      id,
      region,
      load,
      maxMatches,
      publicUrl: `wss://${id}/relay`,
    });
    const relays = [
      relay('eu', 'eu-west', 5),
      relay('us', 'us-east', 0),
      relay('ap', 'ap-south', 1),
    ];
    const eu = [
      { region: 'eu-west', rttMs: 20 },
      { region: 'us-east', rttMs: 110 },
    ];
    const us = [
      { region: 'eu-west', rttMs: 120 },
      { region: 'us-east', rttMs: 25 },
    ];
    // Worst RTT: eu 120, us 110 → us.
    expect(chooseRelay(relays, { players: [eu, us] })?.id).toBe('us');
    expect(chooseRelay(relays, { players: [eu] })?.id).toBe('eu');
    // Nobody near: still placed (on the closest measured relay).
    expect(chooseRelay([relay('ap', 'ap-south')], { players: [eu] })?.id).toBe('ap');
    // No probes: preferred region, then least loaded.
    expect(chooseRelay(relays, { players: [[], []] })?.id).toBe('us');
    expect(chooseRelay(relays, { players: [], preferredRegion: 'ap-south' })?.id).toBe('ap');
    // Full and excluded relays are skipped.
    expect(chooseRelay([relay('eu', 'eu-west', 10)], { players: [eu] })).toBeUndefined();
    expect(chooseRelay(relays, { players: [eu], exclude: ['eu'] })?.id).toBe('us');
  });

  it('authenticates relays by key, pins keys to relay ids and re-registers unknown relays', async () => {
    const bad = await relayCall(a, 'POST', '/relays/register', relayRegistration('relay-x'), {
      key: 'wrong',
    });
    expect(bad.status).toBe(401);
    const pinnedWrong = await relayCall(
      a,
      'POST',
      '/relays/register',
      relayRegistration('relay-x'),
      {
        key: PINNED_KEY,
      },
    );
    expect(pinnedWrong.status).toBe(403);
    const pinned = await relayCall(
      a,
      'POST',
      '/relays/register',
      relayRegistration('relay-pinned'),
      {
        key: PINNED_KEY,
      },
    );
    expect(pinned.status).toBe(200);
    const registered = await json(pinned);
    expect(check('RelayRegistrationResponse', registered).stage).toBe('ok');
    expect(registered).toEqual({
      relayId: 'relay-pinned',
      heartbeatIntervalSeconds: 15,
      jwksUrl: `${ORIGIN}/.well-known/jwks.json`,
    });
    const invalid = await relayCall(a, 'POST', '/relays/register', { relayId: 'x' });
    expect(invalid.status).toBe(400);

    const unknown = await relayCall(b, 'POST', '/relays/heartbeat', {
      relayId: 'relay-unknown',
      load: { matches: 0, connections: 0 },
      draining: false,
      activeMatchIds: [],
    });
    expect(unknown.status).toBe(404);
    expect(await json(unknown)).toEqual({ ok: false, reregister: true });
    await resetRelays();
  });

  it('places matches on healthy, non-draining relays and moves a refused match to another', async () => {
    await registerRelay(a, 'relay-1', { region: 'eu-west' });
    await registerRelay(a, 'relay-2', { region: 'us-east', draining: true });
    const host = await player(a);
    const guest = await player(b);
    const { matchId } = await startRoomMatch(host, guest, [{ region: 'us-east', rttMs: 10 }]);
    // relay-2 is closer but draining.
    const placed = await harness.database.db
      .selectFrom('matches')
      .select('relay_id')
      .where('id', '=', matchId)
      .executeTakeFirstOrThrow();
    expect(placed.relay_id).toBe('relay-1');

    // relay-1 refuses the new match (Reject 5); relay-2 stopped draining.
    const heartbeat = await relayCall(a, 'POST', '/relays/heartbeat', {
      relayId: 'relay-2',
      load: { matches: 0, connections: 0 },
      draining: false,
      activeMatchIds: [],
    });
    expect(await json(heartbeat)).toEqual({ ok: true });
    host.client.clear();
    guest.client.clear();
    const moved = await guest.client.ok('match.reconnect', { matchId, relayUnavailable: true });
    expect(moved['relayUrl']).toBe('wss://relay-2.relays.test/relay');
    // Everyone gets the new relay.
    const pushed = await host.client.event('match.start');
    expect((await verifyTicket(a, pushed['ticket'] as string)).relayUrl).toBe(
      'wss://relay-2.relays.test/relay',
    );

    // Once the relay reports the match, it runs and stays put.
    await relayCall(a, 'POST', '/relays/heartbeat', {
      relayId: 'relay-2',
      load: { matches: 1, connections: 2 },
      draining: false,
      activeMatchIds: [matchId],
    });
    const running = await harness.database.db
      .selectFrom('matches')
      .select(['status', 'started_at'])
      .where('id', '=', matchId)
      .executeTakeFirstOrThrow();
    expect(running.status).toBe('running');
    expect(running.started_at).toBeInstanceOf(Date);
    const stays = await guest.client.ok('match.reconnect', { matchId, relayUnavailable: true });
    expect(stays['relayUrl']).toBe('wss://relay-2.relays.test/relay');

    // A stale relay gets no matches.
    await harness.database.db
      .updateTable('relays')
      .set({ last_heartbeat_at: new Date(Date.now() - 120_000) })
      .execute();
    const p1 = await player(a);
    const p2 = await player(a);
    const failed = await tryStartRoomMatch(p1, p2);
    expect(failed.error?.code).toBe('unavailable');
    await resetRelays();
  });
});

/** Host and guest in a fresh room on a ready 2-team map, both ready. */
async function readyRoom(
  host: Player,
  guest: Player,
  regions: { region: string; rttMs: number }[] = [],
) {
  const seed = Math.floor(Math.random() * 1e9);
  const room = (
    await host.client.ok('room.create', {
      name: 'Match',
      visibility: 'link',
      map: { kind: 'generated', generator: generator(2, seed) },
      regions,
    })
  )['room'] as { id: string; code: string };
  await engine.runPending();
  await waitUntil(async () => {
    const row = await harness.database.db
      .selectFrom('rooms')
      .select('settings')
      .where('id', '=', room.id)
      .executeTakeFirstOrThrow();
    return (row.settings as { mapStatus?: string }).mapStatus === 'ready';
  });
  await guest.client.ok('room.join', { code: room.code, regions });
  await guest.client.ok('room.setSeat', { roomId: room.id, seat: 1, occupant: { kind: 'self' } });
  await guest.client.ok('room.setReady', { roomId: room.id, ready: true });
  return room;
}

async function tryStartRoomMatch(host: Player, guest: Player) {
  const room = await readyRoom(host, guest);
  return host.client.call('room.start', { roomId: room.id });
}

async function startRoomMatch(
  host: Player,
  guest: Player,
  regions: { region: string; rttMs: number }[] = [],
) {
  const room = await readyRoom(host, guest, regions);
  const started = await host.client.ok('room.start', { roomId: room.id });
  return { matchId: started['matchId'] as string, roomId: room.id };
}

// ----------------------------------------------------------------- intake

describe('match-end intake', () => {
  it('stores the record, records the report once, reopens the room and queues verification', async () => {
    await registerRelay(a, 'relay-end');
    const host = await player(a);
    const guest = await player(b);
    const { matchId, roomId } = await startRoomMatch(host, guest);

    const setup = await relayCall(a, 'GET', `/matches/${matchId}/setup`);
    expect(setup.status).toBe(200);
    const setupJson = await json(setup);
    expect(check('MatchSetup', setupJson).stage).toBe('ok');
    expect((setupJson['map'] as { hash: string }).hash).toMatch(/^[0-9a-f]{64}$/);

    const record = new Uint8Array(Buffer.from('G2MR fake record bytes'));
    const put = await relayCall(a, 'PUT', `/matches/${matchId}/record`, record);
    expect(put.status).toBe(200);
    const receipt = await json(put);
    expect(check('RelayRecordReceipt', receipt).stage).toBe('ok');
    // Spool retries: the same upload again is fine.
    expect((await relayCall(b, 'PUT', `/matches/${matchId}/record`, record)).status).toBe(200);

    const report = {
      matchId,
      relayId: 'relay-end',
      simVersion: SIM,
      startedAt: '2026-10-01T12:00:05Z',
      endedAt: '2026-10-01T12:20:05Z',
      finalTick: 30000,
      reason: 'completed',
      seats: [
        { seat: 0, disconnects: 0, droppedForDesync: false },
        { seat: 1, disconnects: 2, quitTick: 29000, droppedForDesync: false },
      ],
      desync: { flagged: false, minoritySeats: [] },
      record: { sha256: receipt['sha256'], size: record.length, formatVersion: 1 },
    };
    host.client.clear();
    const ended = await relayCall(a, 'POST', `/matches/${matchId}/end`, report);
    expect(ended.status).toBe(200);
    expect(await json(ended)).toEqual({ ok: true });
    const duplicate = await relayCall(b, 'POST', `/matches/${matchId}/end`, report);
    expect(await json(duplicate)).toEqual({ ok: true, duplicate: true });

    const db = harness.database.db;
    const match = await db
      .selectFrom('matches')
      .selectAll()
      .where('id', '=', matchId)
      .executeTakeFirstOrThrow();
    expect(match).toMatchObject({ status: 'ended', end_reason: 'completed', final_tick: 30000 });
    const participants = await db
      .selectFrom('match_participants')
      .select(['seat', 'disconnects', 'quit_tick', 'outcome'])
      .where('match_id', '=', matchId)
      .orderBy('seat')
      .execute();
    expect(participants).toEqual([
      { seat: 0, disconnects: 0, quit_tick: null, outcome: null },
      { seat: 1, disconnects: 2, quit_tick: 29000, outcome: null },
    ]);
    const artifact = await db
      .selectFrom('match_artifacts')
      .select('blob_sha256')
      .where('match_id', '=', matchId)
      .executeTakeFirstOrThrow();
    expect(artifact.blob_sha256).toBe(receipt['sha256']);
    const verify = await db
      .selectFrom('engine_jobs')
      .select(['kind', 'payload'])
      .where('kind', '=', 'verify-match')
      .execute();
    const mine = verify.filter((j) => (j.payload as { matchId: string }).matchId === matchId);
    expect(mine).toHaveLength(1);
    expect((mine[0]!.payload as { recordHash: string }).recordHash).toBe(receipt['sha256']);

    // Participants hear about it; the room is open again with Ready cleared.
    const updated = await host.client.event('match.updated');
    expect(check('RealtimeEventMatchUpdated', updated).stage).toBe('ok');
    expect((updated['match'] as { status: string }).status).toBe('ended');
    const reopened = await roomState(host.client, (r) => r['status'] === 'open');
    expect(reopened['id']).toBe(roomId);
    expect(
      (reopened['seats'] as { occupant: { ready?: boolean } }[]).every((s) => !s.occupant.ready),
    ).toBe(true);

    // After the end, a different record is refused.
    const other = await relayCall(
      a,
      'PUT',
      `/matches/${matchId}/record`,
      new Uint8Array([1, 2, 3]),
    );
    expect(other.status).toBe(409);
    await resetRelays();
  });

  it('cancels matches nobody reached and reopens their rooms', async () => {
    await registerRelay(a, 'relay-stale');
    const host = await player(a);
    const { matchId, roomId } = await startRoomMatch(host, await player(b));
    const db = harness.database.db;
    await db
      .updateTable('matches')
      .set({ created_at: new Date(Date.now() - 3_600_000) })
      .where('id', '=', matchId)
      .execute();
    host.client.clear();
    expect(await expireStartingMatches(db)).toBe(1);
    const match = await db
      .selectFrom('matches')
      .select(['status', 'verification'])
      .where('id', '=', matchId)
      .executeTakeFirstOrThrow();
    expect(match).toEqual({ status: 'cancelled', verification: 'not_applicable' });
    const room = await roomState(host.client, (r) => r['status'] === 'open');
    expect(room['id']).toBe(roomId);
    const late = await host.client.call('match.reconnect', { matchId });
    expect(late.error?.code).toBe('not_found');
    await resetRelays();
  });

  it('aborts running matches its relay stopped reporting, without a rating change', async () => {
    await registerRelay(a, 'relay-lost');
    const host = await player(a);
    const guest = await player(b);
    const { matchId, roomId } = await startRoomMatch(host, guest);
    const db = harness.database.db;
    const beat = (activeMatchIds: string[]) =>
      relayCall(a, 'POST', '/relays/heartbeat', {
        relayId: 'relay-lost',
        load: { matches: activeMatchIds.length, connections: 2 },
        draining: false,
        activeMatchIds,
      });
    // The relay reports the match: it runs, and it was seen just now.
    await beat([matchId]);
    const running = await db
      .selectFrom('matches')
      .select(['status', 'relay_seen_at'])
      .where('id', '=', matchId)
      .executeTakeFirstOrThrow();
    expect(running.status).toBe('running');
    expect(running.relay_seen_at).not.toBeNull();
    // Within the grace nothing happens.
    expect(await abortMatchesOnLostRelays(db)).toEqual([]);

    // The relay restarts and keeps heartbeating, but no longer lists the match.
    await beat([]);
    await db
      .updateTable('matches')
      .set({ relay_seen_at: new Date(Date.now() - 600_000) })
      .where('id', '=', matchId)
      .execute();
    host.client.clear();
    guest.client.clear();
    expect(await abortMatchesOnLostRelays(db)).toEqual([matchId]);
    const match = await db
      .selectFrom('matches')
      .select(['status', 'end_reason', 'verification', 'rating_status', 'ended_at'])
      .where('id', '=', matchId)
      .executeTakeFirstOrThrow();
    expect(match).toMatchObject({
      status: 'ended',
      end_reason: 'aborted',
      verification: 'not_applicable',
      rating_status: 'not_rated',
    });
    expect(match.ended_at).not.toBeNull();
    // Nobody's rating moved, and no verify job was queued.
    expect(
      await db
        .selectFrom('rating_history')
        .select('entity_id')
        .where('match_id', '=', matchId)
        .execute(),
    ).toEqual([]);
    const jobs = await db
      .selectFrom('engine_jobs')
      .select('payload')
      .where('kind', '=', 'verify-match')
      .execute();
    expect(jobs.some((j) => (j.payload as { matchId: string }).matchId === matchId)).toBe(false);

    // Both players hear it (on either replica), and the room reopens.
    for (const p of [host, guest]) {
      const updated = await p.client.event('match.updated');
      expect(check('RealtimeEventMatchUpdated', updated).stage).toBe('ok');
      expect(updated['match']).toMatchObject({ status: 'ended', endReason: 'aborted' });
    }
    const reopened = await roomState(host.client, (r) => r['status'] === 'open');
    expect(reopened['id']).toBe(roomId);
    const gone = await host.client.call('match.reconnect', { matchId });
    expect(gone.error?.code).toBe('not_found');
    // A second sweep finds nothing more.
    expect(await abortMatchesOnLostRelays(db)).toEqual([]);

    // The relay was alive after all and reports the real end: it replaces the abort.
    const record = new Uint8Array(Buffer.from('G2MR late record'));
    const receipt = await json(await relayCall(a, 'PUT', `/matches/${matchId}/record`, record));
    const ended = await relayCall(a, 'POST', `/matches/${matchId}/end`, {
      matchId,
      relayId: 'relay-lost',
      simVersion: SIM,
      startedAt: '2026-10-01T12:00:05Z',
      endedAt: '2026-10-01T12:20:05Z',
      finalTick: 30000,
      reason: 'completed',
      seats: [
        { seat: 0, disconnects: 0, droppedForDesync: false },
        { seat: 1, disconnects: 0, droppedForDesync: false },
      ],
      desync: { flagged: false, minoritySeats: [] },
      record: { sha256: receipt['sha256'], size: record.length, formatVersion: 1 },
    });
    expect(await json(ended)).toEqual({ ok: true });
    expect(
      await db
        .selectFrom('matches')
        .select(['end_reason', 'verification', 'rating_status'])
        .where('id', '=', matchId)
        .executeTakeFirstOrThrow(),
    ).toEqual({ end_reason: 'completed', verification: 'pending', rating_status: 'pending' });
    await resetRelays();
  });

  it('refuses an end report before the record and marks leavers of an abandoned game', async () => {
    await registerRelay(a, 'relay-abandon');
    const { matchId } = await startRoomMatch(await player(a), await player(a));
    const report = {
      matchId,
      relayId: 'relay-abandon',
      simVersion: SIM,
      startedAt: '2026-10-01T12:00:05Z',
      endedAt: '2026-10-01T12:02:05Z',
      finalTick: 3000,
      reason: 'abandoned',
      seats: [
        { seat: 0, disconnects: 1, quitTick: 2000, droppedForDesync: false },
        { seat: 1, disconnects: 0, quitTick: 2990, droppedForDesync: false },
      ],
      desync: { flagged: false, minoritySeats: [] },
      record: { sha256: 'ab'.repeat(32), size: 10, formatVersion: 1 },
    };
    const early = await relayCall(a, 'POST', `/matches/${matchId}/end`, report);
    expect(early.status).toBe(409);
    const put = await json(
      await relayCall(a, 'PUT', `/matches/${matchId}/record`, new Uint8Array(Buffer.from('rec'))),
    );
    const mismatch = await relayCall(a, 'POST', `/matches/${matchId}/end`, report);
    expect(mismatch.status).toBe(409);
    const ok = await relayCall(a, 'POST', `/matches/${matchId}/end`, {
      ...report,
      record: { ...report.record, sha256: put['sha256'] },
    });
    expect(ok.status).toBe(200);
    const outcomes = await harness.database.db
      .selectFrom('match_participants')
      .select('outcome')
      .where('match_id', '=', matchId)
      .execute();
    expect(outcomes.map((o) => o.outcome)).toEqual(['abandoned', 'abandoned']);
    const unknown = await relayCall(a, 'POST', `/matches/${crypto.randomUUID()}/end`, {
      ...report,
      matchId: undefined,
    });
    expect(unknown.status).toBe(400);
    const pinned = await relayCall(a, 'GET', `/matches/${matchId}/setup`, undefined, {
      key: PINNED_KEY,
    });
    expect(pinned.status).toBe(403);
    await resetRelays();
  });
});

// --------------------------------------------------------- queue starter

/** A starting proposal (the matchmaker's row) for the given humans; AI fills the rest. */
async function proposal(
  humans: string[],
  overrides: Partial<MatchProposal> = {},
): Promise<MatchProposal> {
  const value = proposalValue(humans, overrides);
  await harness.database.db
    .insertInto('match_proposals')
    .values({
      id: value.id,
      queue_id: value.queueId,
      sim_version: value.simVersion,
      region: value.region,
      rated: value.rated,
      backfilled: value.backfilled,
      status: 'starting',
      map: JSON.stringify(value.map),
    })
    .execute();
  return value;
}

function proposalValue(humans: string[], overrides: Partial<MatchProposal>): MatchProposal {
  return {
    id: crypto.randomUUID(),
    queueId: 'casual-1v1',
    rated: false,
    backfilled: humans.length < 2,
    simVersion: SIM_KEY,
    region: 'eu-west',
    map: {
      generatorId: 'even-ground',
      revision: 2,
      params: { width: 7, height: 7 },
      candidates: 3,
      startingUnitLevel: 0,
    },
    seats: [0, 1].map((slot) =>
      humans[slot]
        ? {
            slot,
            side: slot,
            kind: 'human' as const,
            accountId: humans[slot],
            ratingEntityId: null,
            mu: 25,
            sigma: 25 / 3,
          }
        : {
            slot,
            side: slot,
            kind: 'ai' as const,
            ai: 'cortex' as const,
            ratingEntityId: null,
            mu: 25,
            sigma: 8,
          },
    ),
    ...overrides,
  };
}

describe('PlatformMatchStarter', () => {
  it('is idempotent per proposal, generates the map and places the match', async () => {
    await registerRelay(a, 'relay-q');
    const p1 = await player(a);
    const p2 = await player(b);
    const starter = new PlatformMatchStarter({
      db: harness.database.db,
      jobs: harness.jobs,
      access: {
        name: 'all',
        canHost: async () => ({ allowed: true }),
        canJoin: async () => ({ allowed: true }),
        canQueue: async () => ({ allowed: true }),
      },
      generationTimeoutMs: 5000,
    });
    engine.start();
    const prop = await proposal([p1.accountId, p2.accountId]);
    const [first, second] = await Promise.all([starter.start(prop), starter.start(prop)]);
    expect(second.matchId).toBe(first.matchId);
    expect((await starter.start(prop)).matchId).toBe(first.matchId);
    engine.stop();
    const matches = await harness.database.db
      .selectFrom('matches')
      .selectAll()
      .where('proposal_id', '=', prop.id)
      .execute();
    expect(matches).toHaveLength(1);
    const setup = matches[0]!.setup as ReturnType<typeof queueMatchSetup>;
    expect(check('MatchSetup', setup).stage).toBe('ok');
    expect(setup.seats.map((s) => s.name)).toEqual([p1.displayName, p2.displayName]);
    expect(matches[0]).toMatchObject({
      origin: 'queue',
      queue_id: 'casual-1v1',
      relay_id: 'relay-q',
    });
    // Generated once, even though start ran three times.
    expect(engine.ran.filter((r) => r.kind === 'generate-map').length).toBeGreaterThanOrEqual(1);
    const generated = await harness.database.db
      .selectFrom('generated_maps')
      .select('map_hash')
      .where('map_hash', '=', setup.map.hash)
      .execute();
    expect(generated).toHaveLength(1);
  });

  it('uses a warm map when the pool has one, and fails cleanly without relays or access', async () => {
    const p1 = await player(a);
    const warmHash = 'ee'.repeat(32);
    const starter = new PlatformMatchStarter({
      db: harness.database.db,
      jobs: harness.jobs,
      access: {
        name: 'all',
        canHost: async () => ({ allowed: true }),
        canJoin: async () => ({ allowed: true }),
        canQueue: async () => ({ allowed: true }),
      },
      warmMaps: {
        takeWarmMap: async (queueId, simVersionKey) =>
          queueId === 'casual-1v1' && simVersionKey === SIM_KEY
            ? { generator: { ...generator(2, 77) }, mapHash: warmHash }
            : undefined,
      },
    });
    const started = await starter.start(await proposal([p1.accountId]));
    const match = await harness.database.db
      .selectFrom('matches')
      .select(['map_hash', 'setup'])
      .where('id', '=', started.matchId)
      .executeTakeFirstOrThrow();
    expect(match.map_hash).toBe(warmHash);
    expect((match.setup as { seats: { kind: string; ai?: string }[] }).seats[1]).toMatchObject({
      kind: 'ai',
      ai: 'cortex',
    });
    await resetRelays();
    await expect(starter.start(await proposal([p1.accountId]))).rejects.toMatchObject({
      code: 'unavailable',
    });
    const denying = new PlatformMatchStarter({
      db: harness.database.db,
      jobs: harness.jobs,
      access: {
        name: 'deny',
        canHost: async () => ({ allowed: true }),
        canJoin: async () => ({ allowed: true }),
        canQueue: async () => ({ allowed: false, reason: 'no' }),
      },
    });
    await expect(denying.start(await proposal([p1.accountId]))).rejects.toBeInstanceOf(StartError);
  });
});

// ------------------------------------------------------------------ queues

describe('queue methods and NOTIFY forwarding', () => {
  it('joins, refuses duplicates and guests in rated queues, and leaves', async () => {
    const guest = await player(a);
    const joined = await guest.client.ok('queue.join', {
      queueId: 'casual-1v1',
      regions: [{ region: 'eu-west', rttMs: 30 }],
      allowAiOpponent: false,
    });
    expect(check('RealtimeQueueJoinResult', joined).stage).toBe('ok');
    const twice = await guest.client.call('queue.join', { queueId: 'casual-1v1', regions: [] });
    expect(twice.error?.code).toBe('conflict');
    const rated = await (
      await player(a)
    ).client.call('queue.join', { queueId: 'ranked-1v1', regions: [] });
    expect(rated.error?.code).toBe('forbidden');
    const missing = await guest.client.call('queue.join', { queueId: 'nope', regions: [] });
    expect(missing.error?.code).toBe('not_found');
    await guest.client.ok('queue.leave', { ticketId: joined['ticketId'] });
    const gone = await guest.client.call('queue.leave', { ticketId: joined['ticketId'] });
    expect(gone.error?.code).toBe('not_found');
    const respond = await guest.client.call('queue.respond', {
      proposalId: crypto.randomUUID(),
      accept: true,
    });
    expect(respond.error?.code).toBe('not_found');

    const registered = await registeredPlayer(b, 'Ranked');
    players.push(registered);
    const ok = await registered.client.ok('queue.join', { queueId: 'ranked-1v1', regions: [] });
    const ticket = await harness.database.db
      .selectFrom('queue_tickets')
      .select(['sim_version', 'status', 'queue_id'])
      .where('id', '=', ok['ticketId'] as string)
      .executeTakeFirstOrThrow();
    expect(ticket).toEqual({ sim_version: SIM_KEY, status: 'waiting', queue_id: 'ranked-1v1' });
    expect(resolveQueue(queues[1]!).rated).toBe(true);
  });

  it('forwards worker queue events to sockets on any replica and follows matchFound with match.start', async () => {
    await registerRelay(a, 'relay-n');
    const p1 = await player(b);
    const notifier = new PgQueueNotifier();
    await notifier.send(harness.database.db, p1.accountId, 'queue.status', {
      ticketId: crypto.randomUUID(),
      queueId: 'casual-1v1',
      waitedSeconds: 12,
    });
    const status = await p1.client.event('queue.status');
    expect(status['waitedSeconds']).toBe(12);

    const prop = await proposal([p1.accountId]);
    const setup = queueMatchSetup(prop, {
      seed: 5,
      generator: generator(2, 5),
      mapHash: 'ef'.repeat(32),
    });
    const created = await createMatch(harness.database.db, {
      setup,
      origin: 'queue',
      queueId: prop.queueId,
      proposalId: prop.id,
      rated: false,
      placement: { players: [] },
    });
    const ticketId = crypto.randomUUID();
    await notifier.send(harness.database.db, p1.accountId, 'queue.matchFound', {
      ticketId,
      matchId: created.matchId,
    });
    const found = await p1.client.event('queue.matchFound');
    expect(found).toEqual({ ticketId, matchId: created.matchId });
    const start = await p1.client.event('match.start');
    expect(start['matchId']).toBe(created.matchId);
    const claims = await verifyTicket(b, start['ticket'] as string);
    expect(claims).toMatchObject({
      seat: 0,
      humanSeats: [0],
      relayUrl: 'wss://relay-n.relays.test/relay',
    });
    await resetRelays();
  });
});
