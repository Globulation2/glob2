// Rooms over realtime across two API replicas: lifecycle and permissions,
// NOTIFY fan-out, map generation, sim-version partitioning, invite codes,
// the start sequence down to tickets verified with the published JWKS, and
// the invite landing page.
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { STANDARD_RULES, checkDocument as check, simVersionKey } from '@glob2/protocol';
import {
  FakeEngine,
  OTHER_SIM,
  RELAY_KEY,
  guestPlayer,
  registerRelay,
  roomState,
  serveSim,
  verifyTicket,
  waitUntil,
  type Player,
} from './playSupport.ts';
import { SIM, createHarness, json, type Harness, type Instance } from './support.ts';

const ORIGIN = 'http://play.test';
const GENERATOR = {
  generatorId: 'even-ground',
  revision: 2,
  params: { width: 7, height: 7, teams: 3 },
  seed: 1234,
  candidates: 5,
  startingUnitLevel: 0,
};

let harness: Harness;
let a: Instance;
let b: Instance;
let engine: FakeEngine;
const players: Player[] = [];

beforeAll(async () => {
  harness = await createHarness();
  await serveSim(harness.database.db);
  const relayKeys = [{ key: RELAY_KEY }];
  a = await harness.start({ origin: ORIGIN, relayKeys });
  b = await harness.start({ origin: ORIGIN, relayKeys });
  engine = new FakeEngine(harness.database.db, harness.blobs);
});

afterAll(async () => {
  engine?.stop();
  for (const player of players) player.client.close();
  await harness?.close();
});

async function player(instance: Instance, sim = SIM): Promise<Player> {
  const p = await guestPlayer(instance, sim);
  players.push(p);
  return p;
}

type Room = Record<string, unknown> & {
  id: string;
  code: string;
  revision: number;
  status: string;
  seats: {
    seat: number;
    occupant: { kind: string; accountId?: string; ready?: boolean };
    locked?: boolean;
  }[];
  members: { accountId: string; connected: boolean; seat?: number }[];
  map?: { kind: string; hash?: string };
  mapStatus?: string;
  teams: { team: number; alliance: number }[];
};

describe('rooms', () => {
  let host: Player;
  let guest: Player;
  let room: Room;

  it('creates a room on a generated map that becomes ready when generation finishes', async () => {
    host = await player(a);
    const created = await host.client.ok('room.create', {
      name: 'Evening game',
      visibility: 'public',
      map: { kind: 'generated', generator: GENERATOR },
      regions: [{ region: 'eu-west', rttMs: 30 }],
    });
    expect(check('RealtimeRoomCreateResult', created).stage).toBe('ok');
    room = created['room'] as Room;
    expect(room.mapStatus).toBe('pending');
    expect(room.map?.hash).toBeUndefined();
    expect(room.seats).toHaveLength(3);
    expect(room.seats[0]!.occupant).toMatchObject({ kind: 'human', accountId: host.accountId });
    expect(room.code).toMatch(/^[A-Z2-9]{10}$/);
    expect(room['inviteUrl']).toBe(`${ORIGIN}/j/${room.code}`);

    // The engine agent finishes the job; the worker's result task NOTIFYs map_jobs.
    expect(await engine.runPending()).toBe(1);
    room = (await roomState(host.client, (r) => r['mapStatus'] === 'ready')) as Room;
    expect(room.map?.hash).toMatch(/^[0-9a-f]{64}$/);
    // Generated map bytes are public: anyone can download them by hash.
    const download = await fetch(`${a.url}/api/v1/blobs/maps/${room.map!.hash}`);
    expect(download.status).toBe(200);
    expect(await download.text()).toBe('GLOB2MAP:3:1234');
  });

  it('lets a player on another replica join by code and fans changes out to both', async () => {
    guest = await player(b);
    host.client.clear();
    const joined = await guest.client.ok('room.join', {
      code: room.code.toLowerCase(),
      regions: [{ region: 'eu-west', rttMs: 45 }],
    });
    expect((joined['room'] as Room).members).toHaveLength(2);
    // A new member takes the first open seat ("anyone with the invite can take it").
    expect((joined['room'] as Room).seats[1]!.occupant).toMatchObject({
      kind: 'human',
      accountId: guest.accountId,
      ready: false,
    });
    expect(
      (joined['room'] as Room).members.find((m) => m.accountId === guest.accountId)?.seat,
    ).toBe(1);
    // The host (replica A) learns about the join that happened on replica B.
    room = (await roomState(host.client, (r) => (r['members'] as unknown[]).length === 2)) as Room;
    // Joining again is idempotent.
    await guest.client.ok('room.join', { code: room.code });
  });

  it('leaves a joiner unseated when every seat is taken or locked, and lists them as a member', async () => {
    const other = await player(a);
    const full = (
      await other.client.ok('room.create', {
        name: 'Full',
        visibility: 'link',
        map: {
          kind: 'generated',
          generator: { ...GENERATOR, params: { ...GENERATOR.params, teams: 2 } },
        },
      })
    )['room'] as Room;
    await other.client.ok('room.setSeat', {
      roomId: full.id,
      seat: 1,
      occupant: { kind: 'locked' },
    });
    const late = await player(b);
    const joined = (await late.client.ok('room.join', { code: full.code }))['room'] as Room;
    expect(joined.seats.some((s) => s.occupant.accountId === late.accountId)).toBe(false);
    const member = joined.members.find((m) => m.accountId === late.accountId);
    expect(member).toBeDefined();
    expect(member?.seat).toBeUndefined();
    // Ready needs a seat.
    const ready = await late.client.call('room.setReady', { roomId: full.id, ready: true });
    expect(ready.error?.code).toBe('conflict');
    // Rejoining (idempotent) does not move anyone.
    const again = (await late.client.ok('room.join', { code: full.code }))['room'] as Room;
    expect(again.seats.some((s) => s.occupant.accountId === late.accountId)).toBe(false);
    await late.client.ok('room.leave', { roomId: full.id });
    await other.client.ok('room.leave', { roomId: full.id });
  });

  it('enforces seat permissions and locks', async () => {
    // Members move themselves into open seats.
    const moved = await guest.client.ok('room.setSeat', {
      roomId: room.id,
      seat: 1,
      occupant: { kind: 'self' },
    });
    expect((moved['room'] as Room).seats[1]!.occupant).toMatchObject({
      accountId: guest.accountId,
    });
    // Only the host adds AIs, locks seats or empties other seats.
    for (const occupant of [{ kind: 'ai', ai: 'cortex' }, { kind: 'locked' }]) {
      const denied = await guest.client.call('room.setSeat', {
        roomId: room.id,
        seat: 2,
        occupant,
      });
      expect(denied.error?.code).toBe('forbidden');
    }
    const kick = await guest.client.call('room.setSeat', {
      roomId: room.id,
      seat: 0,
      occupant: { kind: 'open' },
    });
    expect(kick.error?.code).toBe('forbidden');
    // Taking an occupied seat fails.
    const taken = await guest.client.call('room.setSeat', {
      roomId: room.id,
      seat: 0,
      occupant: { kind: 'self' },
    });
    expect(taken.error?.code).toBe('conflict');

    const locked = await host.client.ok('room.setSeat', {
      roomId: room.id,
      seat: 2,
      occupant: { kind: 'locked' },
    });
    expect((locked['room'] as Room).seats[2]!.locked).toBe(true);
    const intoLocked = await guest.client.call('room.setSeat', {
      roomId: room.id,
      seat: 2,
      occupant: { kind: 'self' },
    });
    expect(intoLocked.error?.code).toBe('conflict');
    // The host only changes settings; a guest cannot.
    const notHost = await guest.client.call('room.update', {
      roomId: room.id,
      revision: 0,
      changes: { name: 'mine' },
    });
    expect(notHost.error?.code).toBe('forbidden');
  });

  it('clears Ready when rules change and rejects stale revisions', async () => {
    const ready = await guest.client.ok('room.setReady', { roomId: room.id, ready: true });
    room = ready['room'] as Room;
    expect(room.seats[1]!.occupant.ready).toBe(true);
    const stale = await host.client.call('room.update', {
      roomId: room.id,
      revision: room.revision - 1,
      changes: { rules: { ...STANDARD_RULES, mapDiscovered: true } },
    });
    expect(stale.error?.code).toBe('conflict');
    // Renaming keeps Ready; changing the rules clears it.
    const renamed = await host.client.ok('room.update', {
      roomId: room.id,
      revision: room.revision,
      changes: { name: 'Evening game 2' },
    });
    room = renamed['room'] as Room;
    expect(room.seats[1]!.occupant.ready).toBe(true);
    const changed = await host.client.ok('room.update', {
      roomId: room.id,
      revision: room.revision,
      changes: {
        rules: { ...STANDARD_RULES, mapDiscovered: true },
        teams: [
          { team: 0, alliance: 0 },
          { team: 1, alliance: 1 },
          { team: 2, alliance: 1 },
        ],
      },
    });
    room = changed['room'] as Room;
    expect(room.seats[1]!.occupant.ready).toBe(false);
    expect(room.teams[2]!.alliance).toBe(1);
    // The guest sees the same state through replica B.
    await roomState(guest.client, (r) => r['revision'] === room.revision);
    const badTeams = await host.client.call('room.update', {
      roomId: room.id,
      revision: room.revision,
      changes: { teams: [{ team: 0, alliance: 0 }] },
    });
    expect(badTeams.error?.code).toBe('bad_request');
  });

  it('carries chat across replicas and enforces mutes', async () => {
    const sent = await guest.client.ok('room.chat', { roomId: room.id, text: 'glhf' });
    expect(check('RealtimeRoomChatResult', sent).stage).toBe('ok');
    const received = await host.client.event('room.chat');
    expect(received['message']).toMatchObject({ text: 'glhf', accountId: guest.accountId });

    await harness.database.db
      .updateTable('accounts')
      .set({ muted_until: new Date(Date.now() + 60_000) })
      .where('id', '=', guest.accountId)
      .execute();
    // A mute applies at once, without signing in again.
    const muted = await guest.client.call('room.chat', { roomId: room.id, text: 'spam' });
    expect(muted.error?.code).toBe('forbidden');
    await harness.database.db
      .updateTable('accounts')
      .set({ muted_until: null })
      .where('id', '=', guest.accountId)
      .execute();
  });

  it('refuses to start until every seated player is ready, then fails cleanly without relays', async () => {
    const notReady = await host.client.call('room.start', { roomId: room.id });
    expect(notReady.error?.code).toBe('conflict');
    const nonHost = await guest.client.call('room.start', { roomId: room.id });
    expect(nonHost.error?.code).toBe('forbidden');
    room = (await guest.client.ok('room.setReady', { roomId: room.id, ready: true }))[
      'room'
    ] as Room;

    const noRelay = await host.client.call('room.start', { roomId: room.id });
    expect(noRelay.error?.code).toBe('unavailable');
    // The room is open again, Ready kept.
    const state = (
      await harness.database.db
        .selectFrom('rooms')
        .select('status')
        .where('id', '=', room.id)
        .executeTakeFirstOrThrow()
    ).status;
    expect(state).toBe('open');
  });

  it('starts: every seated player gets match.start with a ticket that verifies against the JWKS', async () => {
    await registerRelay(a, 'relay-eu-1', { region: 'eu-west' });
    host.client.clear();
    guest.client.clear();
    const started = await host.client.ok('room.start', { roomId: room.id });
    const matchId = started['matchId'] as string;

    for (const p of [host, guest]) {
      const assignment = await p.client.event('match.start');
      expect(check('RealtimeEventMatchStart', assignment).stage).toBe('ok');
      expect(assignment['matchId']).toBe(matchId);
      expect(assignment['relayUrl']).toBe('wss://relay-eu-1.relays.test/relay');
      const setup = assignment['setup'] as {
        seats: { seat: number; kind: string; team: number; ai?: string; accountId?: string }[];
        map: { kind: string; hash: string };
        teams: { alliance: number }[];
        seed: number;
      };
      expect(setup.map).toMatchObject({ kind: 'generated', hash: room.map!.hash });
      // The locked, empty seat's team is closed: no player, no colony.
      expect(setup.seats.map((s) => s.kind)).toEqual(['human', 'human', 'closed']);
      expect(setup.seats[2]).toEqual({ seat: 2, kind: 'closed', team: 2 });
      expect(setup.teams.map((t) => t.alliance)).toEqual([0, 1, 1]);
      expect(assignment['mapUrl']).toBe(`${ORIGIN}/api/v1/blobs/maps/${room.map!.hash}`);

      const claims = await verifyTicket(a, assignment['ticket'] as string);
      expect(claims).toMatchObject({
        matchId,
        sub: p.accountId,
        accountId: p.accountId,
        seat: p === host ? 0 : 1,
        humanSeats: [0, 1],
        relayUrl: 'wss://relay-eu-1.relays.test/relay',
        simVersion: SIM,
        entitlements: [],
        iss: ORIGIN,
      });
      expect(claims.exp - Date.now() / 1000).toBeGreaterThan(600);
    }
    const inMatch = await roomState(guest.client, (r) => r['status'] === 'in_match');
    expect(inMatch['matchId']).toBe(matchId);
    const match = await harness.database.db
      .selectFrom('matches')
      .selectAll()
      .where('id', '=', matchId)
      .executeTakeFirstOrThrow();
    expect(match).toMatchObject({
      origin: 'room',
      room_id: room.id,
      rated: false,
      status: 'starting',
      relay_id: 'relay-eu-1',
    });
    expect(match.seed).toBe((match.setup as { seed: number }).seed);
    // Participants are the players; the closed seat is none.
    const participants = await harness.database.db
      .selectFrom('match_participants')
      .select(['seat', 'team', 'kind'])
      .where('match_id', '=', matchId)
      .orderBy('seat')
      .execute();
    expect(participants).toEqual([
      { seat: 0, team: 0, kind: 'human' },
      { seat: 1, team: 1, kind: 'human' },
    ]);

    // match.reconnect re-issues a ticket for the running match.
    const again = await guest.client.ok('match.reconnect', { matchId });
    expect(check('RealtimeMatchReconnectResult', again).stage).toBe('ok');
    const claims = await verifyTicket(b, again['ticket'] as string);
    expect(claims.seat).toBe(1);
    const stranger = await player(a);
    const notSeated = await stranger.client.call('match.reconnect', { matchId });
    expect(notSeated.error?.code).toBe('not_found');
    // Settings are frozen while the match runs.
    const frozen = await host.client.call('room.setReady', { roomId: room.id, ready: false });
    expect(frozen.error?.code).toBe('conflict');
  });

  it('closes the room for everyone when the host leaves', async () => {
    const other = await player(a);
    const created = (await other.client.ok('room.create', { name: 'Short', visibility: 'link' }))[
      'room'
    ] as Room;
    const joiner = await player(b);
    await joiner.client.ok('room.join', { code: created.code });
    await other.client.ok('room.leave', { roomId: created.id });
    const closed = await joiner.client.event('room.closed');
    expect(closed).toEqual({ roomId: created.id, reason: 'host_closed' });
    const again = await joiner.client.call('room.join', { code: created.code });
    expect(again.error?.code).toBe('not_found');
    // A plain member leaving keeps the room open.
    const third = await player(a);
    const kept = (await third.client.ok('room.create', { name: 'Kept', visibility: 'link' }))[
      'room'
    ] as Room;
    await joiner.client.ok('room.join', { code: kept.code });
    await joiner.client.ok('room.leave', { roomId: kept.id });
    await roomState(third.client, (r) => (r['members'] as unknown[]).length === 1);
  });

  it('lets the host kick a player, who cannot rejoin that room for ten minutes', async () => {
    const owner = await player(a);
    const kicked = (await owner.client.ok('room.create', { name: 'Kicks', visibility: 'link' }))[
      'room'
    ] as Room;
    const target = await player(b);
    const bystander = await player(a);
    await target.client.ok('room.join', { code: kicked.code });
    await bystander.client.ok('room.join', { code: kicked.code });
    await target.client.ok('room.setSeat', {
      roomId: kicked.id,
      seat: 1,
      occupant: { kind: 'self' },
    });
    // Only the host kicks, never themselves, and only members.
    expect(
      (await bystander.client.call('room.kick', { roomId: kicked.id, accountId: target.accountId }))
        .error?.code,
    ).toBe('forbidden');
    expect(
      (await owner.client.call('room.kick', { roomId: kicked.id, accountId: owner.accountId }))
        .error?.code,
    ).toBe('bad_request');
    const outsider = await player(a);
    expect(
      (await owner.client.call('room.kick', { roomId: kicked.id, accountId: outsider.accountId }))
        .error?.code,
    ).toBe('not_found');

    target.client.clear();
    const result = await owner.client.ok('room.kick', {
      roomId: kicked.id,
      accountId: target.accountId,
    });
    expect(check('RealtimeRoomKickResult', result).stage).toBe('ok');
    const after = result['room'] as Room;
    expect(after.members.map((m) => m.accountId)).not.toContain(target.accountId);
    expect(after.seats[1]!.occupant.kind).toBe('open');
    // The kicked player (on the other replica) is told, and the rest see the new state.
    expect(await target.client.event('room.closed')).toEqual({
      roomId: kicked.id,
      reason: 'kicked',
    });
    await roomState(bystander.client, (r) => (r['members'] as unknown[]).length === 2);

    const refused = await target.client.call('room.join', { code: kicked.code });
    expect(refused.error?.code).toBe('forbidden');
    const until = Date.parse((refused.error?.details as { until: string }).until);
    expect(until - Date.now()).toBeGreaterThan(9 * 60_000);
    expect(until - Date.now()).toBeLessThanOrEqual(10 * 60_000 + 5_000);

    // Once the ban runs out (and the sweep deletes it), joining works again.
    const db = harness.database.db;
    await db
      .updateTable('room_kicks')
      .set({ until: new Date(Date.now() - 1000) })
      .where('room_id', '=', kicked.id)
      .execute();
    await target.client.ok('room.join', { code: kicked.code });
  });
});

describe('sim versions and invite codes', () => {
  it('answers update_required to joiners with another sim version and to unsupported clients', async () => {
    const host = await player(a);
    const room = (await host.client.ok('room.create', { name: 'v125', visibility: 'link' }))[
      'room'
    ] as Room;
    // OTHER_SIM is served too, but it is not the room's version.
    await serveSim(harness.database.db, OTHER_SIM);
    const other = await player(b, OTHER_SIM);
    const wrong = await other.client.call('room.join', { code: room.code });
    expect(wrong.error?.code).toBe('update_required');
    await harness.database.db
      .deleteFrom('engine_agents')
      .where('sim_version', '=', simVersionKey(OTHER_SIM))
      .execute();
    const unsupported = await player(b, OTHER_SIM);
    const create = await unsupported.client.call('room.create', { name: 'x', visibility: 'link' });
    expect(create.error?.code).toBe('update_required');
  });

  it('rate-limits failed invite-code lookups', async () => {
    const prober = await player(a);
    for (let i = 0; i < 10; i++) {
      const miss = await prober.client.call('room.join', {
        code: `NOPE${String(i).padStart(4, '0')}`,
      });
      expect(miss.error?.code).toBe('not_found');
    }
    const limited = await prober.client.call('room.join', { code: 'NOPE9999' });
    expect(limited.error?.code).toBe('rate_limited');
  });
});

describe('room REST and the invite page', () => {
  let room: Room;
  let host: Player;

  beforeAll(async () => {
    host = await player(a);
    room = (await host.client.ok('room.create', { name: "Bea's <room>", visibility: 'public' }))[
      'room'
    ] as Room;
  });

  it('lists public open rooms for a sim version', async () => {
    const list = await json(
      await fetch(`${a.url}/api/v1/rooms?simVersion=${simVersionKey(SIM)}&limit=50`),
    );
    expect(check('RoomList', list).stage).toBe('ok');
    const items = list['items'] as { id: string; seatsTaken: number; seatsTotal: number }[];
    const mine = items.find((i) => i.id === room.id);
    expect(mine).toMatchObject({ seatsTaken: 1, seatsTotal: 2 });
    const other = await json(
      await fetch(`${a.url}/api/v1/rooms?simVersion=${simVersionKey(OTHER_SIM)}`),
    );
    expect((other['items'] as unknown[]).some((i) => (i as { id: string }).id === room.id)).toBe(
      false,
    );
    const missing = await fetch(`${a.url}/api/v1/rooms`);
    expect(missing.status).toBe(400);
  });

  it('describes an invite code', async () => {
    const info = await json(await fetch(`${b.url}/api/v1/invites/${room.code}`));
    expect(check('InviteInfo', info).stage).toBe('ok');
    expect(info).toMatchObject({ code: room.code, status: 'open', roomName: "Bea's <room>" });
    expect((await fetch(`${b.url}/api/v1/invites/ZZZZZZZZ`)).status).toBe(404);
  });

  it('serves a landing page that opens the app, offers the browser client and has OpenGraph tags', async () => {
    const page = await fetch(`${a.url}/j/${room.code}`);
    expect(page.status).toBe(200);
    const html = await page.text();
    const appLink = `glob2://join?instance=${encodeURIComponent(ORIGIN)}&#38;code=${room.code}`;
    expect(html).toContain(`href="${appLink}"`);
    expect(html).toContain(`href="${ORIGIN}/play/?join=${room.code}"`);
    expect(html).toContain('<meta property="og:title" content="Join Bea&#39;s &#60;room&#62;"');
    expect(html).toContain(`<meta property="og:url" content="${ORIGIN}/j/${room.code}"`);
    expect(html).not.toContain('<room>');
    const csp = page.headers.get('content-security-policy')!;
    const nonce = /script-src 'nonce-([^']+)'/.exec(csp)?.[1];
    expect(nonce).toBeTruthy();
    expect(html).toContain(`<script nonce="${nonce}">`);

    const unknown = await fetch(`${a.url}/j/NOSUCHCODE`);
    expect(unknown.status).toBe(404);
    const unknownHtml = await unknown.text();
    expect(unknownHtml).toContain('expired or does not exist');
    expect(unknownHtml).toContain('<meta property="og:title" content="Invite not found"');
    expect(unknownHtml).toContain(`href="${ORIGIN}/play/"`);
    expect(unknownHtml).not.toContain('<script');

    // A closed room's code is expired.
    await host.client.ok('room.leave', { roomId: room.id });
    await waitUntil(async () => (await fetch(`${a.url}/j/${room.code}`)).status === 404);
  });
});

describe('presence and sweeps', () => {
  it('marks a member disconnected when its last socket closes and back when it returns', async () => {
    const host = await player(a);
    const room = (await host.client.ok('room.create', { name: 'Presence', visibility: 'link' }))[
      'room'
    ] as Room;
    const guest = await player(b);
    await guest.client.ok('room.join', { code: room.code });
    guest.client.close();
    await roomState(host.client, (r) =>
      (r['members'] as { accountId: string; connected: boolean }[]).some(
        (m) => m.accountId === guest.accountId && !m.connected,
      ),
    );
    const back = await (await import('./support.ts')).RealtimeClient.connect(a.url);
    players.push({ ...guest, client: back });
    await back.hello(guest.accessToken);
    await roomState(host.client, (r) =>
      (r['members'] as { accountId: string; connected: boolean }[]).some(
        (m) => m.accountId === guest.accountId && m.connected,
      ),
    );
  });
});
