// Recovery paths of the API: rooms left in 'starting' by a crash, realtime
// re-sync after the pub/sub listener reconnects, spilled (large) fan-out
// payloads, and the admin re-verify endpoint.
import { sql } from 'kysely';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { allowAllPolicy } from '@glob2/core';
import { RoomService, START_INTERRUPTED_NOTICE } from '../src/play/rooms.ts';
import {
  FakeEngine,
  RELAY_KEY,
  guestPlayer,
  registerRelay,
  roomState,
  serveSim,
  waitUntil,
  type Player,
} from './playSupport.ts';
import { createHarness, logger, type Harness, type Instance } from './support.ts';

const ORIGIN = 'http://reliability.test';
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
let rooms: RoomService;
const players: Player[] = [];

beforeAll(async () => {
  harness = await createHarness();
  await serveSim(harness.database.db);
  api = await harness.start({ origin: ORIGIN, relayKeys: [{ key: RELAY_KEY }] });
  engine = new FakeEngine(harness.database.db, harness.blobs);
  rooms = new RoomService({
    db: harness.database.db,
    jobs: harness.jobs,
    access: allowAllPolicy,
    origin: ORIGIN,
    logger,
  });
  await registerRelay(api, 'relay-rel-1', { region: 'eu-west' });
});

afterAll(async () => {
  for (const p of players) p.client.close();
  // Let the presence updates of the closing sockets finish before the
  // database goes away.
  await waitUntil(async () => {
    const left = await harness.database.db
      .selectFrom('room_members')
      .select('account_id')
      .where('connected', '=', true)
      .execute();
    return left.length === 0;
  }, 10_000).catch(() => undefined);
  await harness?.close();
});

async function player(): Promise<Player> {
  const p = await guestPlayer(api);
  players.push(p);
  return p;
}

/** A room with host and guest seated and ready, on a generated map that is ready. */
async function readyRoom() {
  const host = await player();
  const guest = await player();
  const created = await host.client.ok('room.create', {
    name: 'Recovery',
    visibility: 'link',
    map: { kind: 'generated', generator: { ...GENERATOR, seed: Math.floor(Math.random() * 1e6) } },
    regions: [{ region: 'eu-west', rttMs: 20 }],
  });
  const room = created['room'] as { id: string; code: string };
  await engine.runPending();
  await roomState(host.client, (r) => r['mapStatus'] === 'ready');
  await guest.client.ok('room.join', {
    code: room.code,
    regions: [{ region: 'eu-west', rttMs: 25 }],
  });
  await guest.client.ok('room.setReady', { roomId: room.id, ready: true });
  return { host, guest, roomId: room.id };
}

/** Kills this replica's LISTEN connection; PgPubSub reconnects and the hub re-syncs. */
async function dropListener(instance: Instance): Promise<void> {
  const before = instance.app.identity.hub.resyncCount;
  await sql`SELECT pg_terminate_backend(pid) FROM pg_stat_activity
            WHERE application_name = 'glob2-pubsub' AND datname = current_database()`.execute(
    harness.database.db,
  );
  await waitUntil(() => instance.app.identity.hub.resyncCount > before, 10_000);
}

describe('rooms stuck in starting', () => {
  it('reopens a room whose start was interrupted before its match existed, with a notice', async () => {
    const { host, guest, roomId } = await readyRoom();
    // The API died right after committing 'starting'.
    await sql`UPDATE rooms SET status = 'starting', starting_since = now() - interval '10 minutes'
              WHERE id = ${roomId}`.execute(harness.database.db);
    host.client.clear();
    guest.client.clear();
    expect(await rooms.recoverStarting()).toBe(1);
    for (const p of [host, guest]) {
      const state = await roomState(p.client, (r) => r['status'] === 'open' && 'notice' in r);
      expect(state['notice']).toBe(START_INTERRUPTED_NOTICE);
    }
    // Nothing left to recover; a fresh start clears the notice.
    expect(await rooms.recoverStarting()).toBe(0);
    host.client.clear();
    await host.client.ok('room.start', { roomId });
    const started = await roomState(host.client, (r) => r['status'] === 'in_match');
    expect(started['notice']).toBeUndefined();
  });

  it('resumes a room whose match was created before the crash; players get match.start again', async () => {
    const { host, guest, roomId } = await readyRoom();
    const { matchId } = (await host.client.ok('room.start', { roomId })) as { matchId: string };
    await host.client.event('match.start');
    await guest.client.event('match.start');
    // The process died between createMatch and the in_match update.
    await sql`UPDATE rooms SET status = 'starting', match_id = NULL,
                starting_since = now() - interval '10 minutes'
              WHERE id = ${roomId}`.execute(harness.database.db);
    await sql`UPDATE matches SET created_at = now() - interval '9 minutes' WHERE id = ${matchId}`.execute(
      harness.database.db,
    );
    host.client.clear();
    guest.client.clear();
    expect(await rooms.recoverStarting()).toBe(1);
    const room = await harness.database.db
      .selectFrom('rooms')
      .select(['status', 'match_id', 'starting_since'])
      .where('id', '=', roomId)
      .executeTakeFirstOrThrow();
    expect(room).toEqual({ status: 'in_match', match_id: matchId, starting_since: null });
    for (const p of [host, guest]) {
      expect(((await p.client.event('match.start')) as { matchId: string }).matchId).toBe(matchId);
    }
  });
});

describe('realtime after a pub/sub reconnect', () => {
  it('re-sends room state the sockets missed and match tickets of starting matches', async () => {
    const { host, guest, roomId } = await readyRoom();
    // A change committed while notifications were lost (no NOTIFY reaches anyone).
    await sql`UPDATE rooms SET name = 'Renamed while away', revision = revision + 1 WHERE id = ${roomId}`.execute(
      harness.database.db,
    );
    host.client.clear();
    guest.client.clear();
    await dropListener(api);
    for (const p of [host, guest]) {
      await roomState(p.client, (r) => r['name'] === 'Renamed while away');
    }

    const { matchId } = (await host.client.ok('room.start', { roomId })) as { matchId: string };
    await host.client.event('match.start');
    host.client.clear();
    await dropListener(api);
    expect(((await host.client.event('match.start')) as { matchId: string }).matchId).toBe(matchId);
  });

  it('delivers fan-out payloads larger than NOTIFY allows', async () => {
    const p = await player();
    const big = 'x'.repeat(20_000);
    await api.app.identity.hub.sendToAccount(p.accountId, 'match.rematchOffered', {
      matchId: '00000000-0000-4000-8000-000000000000',
      roomId: '00000000-0000-4000-8000-000000000001',
      code: 'ABCDEFGHJK',
      host: big,
    });
    const data = (await p.client.event('match.rematchOffered')) as { host: string };
    expect(data.host).toBe(big);
  });
});
