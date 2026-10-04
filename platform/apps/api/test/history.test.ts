// History, leaderboard and profile REST against Postgres, over a seeded
// history (historySeed.ts): leaderboard visibility and paging, AI ladders per
// sim version, profiles (full, guest-minimal, banned, aggregates from the 0004
// views), match lists and cursors, match detail (teams, timelines, economy,
// verification detail), artifact downloads and their access rules, the
// moderators' match lookup, the home page's live stats, and the mobile
// app-link files.
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { sql } from 'kysely';
import { checkDocument } from '@glob2/protocol';
import { SEEDED_QUEUES, seedHistory, type SeededHistory } from './historySeed.ts';
import { createHarness, json, type Harness, type Instance } from './support.ts';

const ORIGIN = 'http://play.test';

// Response documents are checked against their schemas; fields are read loosely.
// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Doc = any;

let harness: Harness;
let api: Instance;
let seed: SeededHistory;

beforeAll(async () => {
  harness = await createHarness();
  api = await harness.start({
    origin: ORIGIN,
    instance: {
      queues: SEEDED_QUEUES,
      appLinks: {
        android: {
          sha256CertFingerprints: [Array.from({ length: 32 }, () => 'AB').join(':')],
        },
        ios: { appIds: ['ABCDE12345.org.globulation2.glob2'] },
      },
    },
  });
  seed = await seedHistory(harness.database.db, harness.blobs, {
    replayBytes: Buffer.from('a real replay would be here'),
  });
});

afterAll(async () => {
  await harness?.close();
});

function get(path: string, session?: string): Promise<Response> {
  return fetch(`${api.url}${path}`, {
    headers: session ? { cookie: `glob2_session=${session}` } : {},
  });
}

async function ok(path: string, schema?: string, session?: string) {
  const response = await get(path, session);
  const body = await json(response);
  expect(response.status, JSON.stringify(body)).toBe(200);
  if (schema) expect(checkDocument(schema, body).issues).toEqual([]);
  return body as Record<string, Doc>;
}

describe('leaderboards', () => {
  it('ranks registered active accounts only, flagging provisional ratings', async () => {
    const page = await ok('/api/v1/leaderboards/ranked-1v1', 'LeaderboardPage');
    expect(page['name']).toBe('1 vs 1 ranked');
    const names = page['entries'].map((e: Doc) => e.entity.account.displayName);
    // The banned account's higher rating is not listed; AIs are separate.
    expect(names).not.toContain('Spammer');
    expect(page['entries'].every((e: Doc) => e.entity.kind === 'account')).toBe(true);
    expect(page['entries'][0].entity.account.displayName).toBe('Kestrel');
    expect(page['entries'].map((e: { rank: number }) => e.rank)).toEqual(
      page['entries'].map((_: unknown, i: number) => i + 1),
    );
    const ana = page['entries'].find((e: Doc) => e.entity.account.displayName === 'Ana_M');
    expect(ana.provisional).toBe(true);
    const settled = await ok('/api/v1/leaderboards/ranked-1v1?provisional=exclude');
    expect(settled['entries'].some((e: Doc) => e.entity.account.displayName === 'Ana_M')).toBe(
      false,
    );
  });

  it('pages with a cursor', async () => {
    const first = await ok('/api/v1/leaderboards/ranked-1v1?limit=2', 'LeaderboardPage');
    expect(first['entries']).toHaveLength(2);
    const second = await ok(
      `/api/v1/leaderboards/ranked-1v1?limit=2&cursor=${first['nextCursor']}`,
      'LeaderboardPage',
    );
    expect(second['entries'][0].rank).toBe(3);
  });

  it('lists AI entities per sim version, current version first', async () => {
    const ai = await ok('/api/v1/leaderboards/ranked-1v1/ai', 'AiLeaderboard');
    expect(ai['groups']).toHaveLength(2);
    const current = ai['groups'].find((g: { current: boolean }) => g.current);
    expect(current.entries.map((e: Doc) => e.entity.ai)).toEqual(['nicowar', 'maxima']);
    expect(ai['groups'].find((g: { current: boolean }) => !g.current).entries[0].entity.ai).toBe(
      'cortex',
    );
  });

  it('404s for unknown ladders and rejects bad parameters', async () => {
    expect((await get('/api/v1/leaderboards/nope')).status).toBe(404);
    expect((await get('/api/v1/leaderboards/ranked-1v1?provisional=maybe')).status).toBe(400);
    expect((await get('/api/v1/leaderboards/ranked-1v1?limit=0')).status).toBe(400);
    // A configured ladder nobody has played yet is empty, not missing.
    const empty = await ok('/api/v1/leaderboards/ranked-2v2');
    expect(empty['entries'].map((e: Doc) => e.entity.account.displayName)).toEqual(['Bradley']);
  });
});

describe('profiles', () => {
  it('shows ratings, rating history, recent matches and aggregates', async () => {
    const profile = await ok(`/api/v1/players/${seed.accounts.bradley}`, 'PlayerProfile');
    expect(profile['detail']).toBe('full');
    expect(profile['status']).toBeUndefined();
    const oneVsOne = profile['ratings'].find((r: { ladder: string }) => r.ladder === 'ranked-1v1');
    expect(oneVsOne.rank).toBeGreaterThan(0);
    expect(oneVsOne.name).toBe('1 vs 1 ranked');
    expect(profile['ratingHistory']).toHaveLength(8);
    // Oldest first: the graph reads left to right.
    const times = profile['ratingHistory'].map((p: { at: string }) => Date.parse(p.at));
    expect([...times].sort((a, b) => a - b)).toEqual(times);
    expect(profile['recentMatches'][0].id).toBe(seed.pendingMatch);
    const aggregates = profile['aggregates'];
    expect(aggregates.games).toBe(8);
    expect(aggregates.wins + aggregates.losses).toBe(8);
    const queue = aggregates.winRates.find((w: { dimension: string }) => w.dimension === 'queue');
    expect(queue).toMatchObject({ key: 'ranked-1v1', label: '1 vs 1 ranked', games: 8 });
    const map = aggregates.winRates.find((w: { dimension: string }) => w.dimension === 'map');
    expect(map).toMatchObject({ label: 'Even Ground Classic', mapId: seed.mapId });
    expect(aggregates.medianTicks).toBeGreaterThan(0);
    expect(aggregates.economy.matchId).toBe(seed.featuredMatch);
    expect(aggregates.economy.points.length).toBeGreaterThan(10);
    expect(aggregates.economy.points[0].gamesAtTick).toBe(8);
  });

  it('gives guests a minimal profile', async () => {
    const profile = await ok(`/api/v1/players/${seed.accounts.guest}`, 'PlayerProfile');
    expect(profile).toMatchObject({ detail: 'minimal', ratings: [], ratingHistory: [] });
    expect(profile['aggregates']).toBeUndefined();
    expect(profile['recentMatches']).toHaveLength(1);
  });

  it('hides banned accounts from everyone but moderators', async () => {
    expect((await get(`/api/v1/players/${seed.accounts.banned}`)).status).toBe(404);
    expect((await get(`/api/v1/players/${seed.accounts.banned}`, seed.userSession)).status).toBe(
      404,
    );
    const seen = await ok(
      `/api/v1/players/${seed.accounts.banned}`,
      'PlayerProfile',
      seed.adminSession,
    );
    expect(seen['status']).toBe('banned');
    expect((await get('/api/v1/players/not-a-uuid')).status).toBe(404);
  });

  it('pages a player’s matches and filters by queue', async () => {
    const first = await ok(`/api/v1/players/${seed.accounts.bradley}/matches?limit=4`, 'MatchList');
    expect(first['items']).toHaveLength(4);
    const rest = await ok(
      `/api/v1/players/${seed.accounts.bradley}/matches?limit=50&cursor=${first['nextCursor']}`,
      'MatchList',
    );
    expect(rest['nextCursor']).toBeUndefined();
    const ids = [...first['items'], ...rest['items']].map((m: { id: string }) => m.id);
    expect(new Set(ids).size).toBe(9);
    expect(ids).toEqual([seed.pendingMatch, ...seed.rankedMatches]);
    const rooms = await ok(`/api/v1/players/${seed.accounts.ana}/matches?queue=room`, 'MatchList');
    expect(rooms['items']).toHaveLength(2);
    expect((await get(`/api/v1/players/${seed.accounts.ana}/matches?cursor=garbage`)).status).toBe(
      400,
    );
  });
});

describe('matches', () => {
  it('lists recent public matches: queues and public rooms, ended only', async () => {
    const list = await ok('/api/v1/matches?limit=50', 'MatchList');
    const ids = list['items'].map((m: { id: string }) => m.id);
    expect(ids).toContain(seed.publicRoomMatch);
    expect(ids).toContain(seed.vsAiMatch);
    expect(ids).not.toContain(seed.linkRoomMatch);
    expect(ids).not.toContain(seed.runningMatch);
    const summary = list['items'].find((m: { id: string }) => m.id === seed.featuredMatch);
    expect(summary.mapTitle).toBe('Even Ground Classic');
    expect(summary.participants[0].rating.ladder).toBe('ranked-1v1');
    const room = list['items'].find((m: { id: string }) => m.id === seed.publicRoomMatch);
    expect(room.mapTitle).toBe('Marchland');
    // Queue matches carry the queue's configured name, not just its id.
    expect(summary.queueId).toBe('ranked-1v1');
    expect(summary.queueName).toBe('1 vs 1 ranked');
    expect(room.queueName).toBeUndefined();
  });

  it('names a room match on the host’s premade map after the uploaded title', async () => {
    const detail = await ok(
      `/api/v1/matches/${seed.linkRoomMatch}`,
      'MatchDetail',
      seed.userSession,
    );
    expect(detail['match'].mapTitle).toBe('balanced for 2');
    expect(detail['map']).toMatchObject({ title: 'balanced for 2', width: 64, height: 64 });
    // A non-player's private upload of the same bytes never names it.
    expect(JSON.stringify(detail)).not.toContain('Kestrel secret');
    const anas = await ok(`/api/v1/players/${seed.accounts.ana}/matches?queue=room`, 'MatchList');
    const listed = anas['items'].find((m: { id: string }) => m.id === seed.linkRoomMatch);
    expect(listed.mapTitle).toBe('balanced for 2');
  });

  it('shows match detail with teams, timelines, economy and verification detail', async () => {
    const detail = await ok(`/api/v1/matches/${seed.featuredMatch}`, 'MatchDetail');
    expect(detail['teams']).toHaveLength(2);
    expect(detail['teams'][0].timeline[1]).toMatchObject({ tick: 512 });
    expect(detail['map']).toMatchObject({ title: 'Even Ground Classic', mapId: seed.mapId });
    expect(detail['verificationDetail'].orderRejections).toEqual([
      { seat: 1, rejected: 2, stale: 1, reasons: { foreign_unit: 2 }, firstRejectedTick: 812 },
    ]);
    expect(detail['economy']).toHaveLength(2);
    // Anonymous viewers get the replay, not the raw record.
    expect(detail['artifacts'].map((a: { kind: string }) => a.kind)).toEqual(['replay']);
    expect(detail['artifacts'][0].url).toBe(
      `${ORIGIN}/api/v1/matches/${seed.featuredMatch}/artifacts/replay`,
    );
    // A link-only room's match is still reachable by its id.
    await ok(`/api/v1/matches/${seed.linkRoomMatch}`, 'MatchDetail');
    const generated = await ok(`/api/v1/matches/${seed.vsAiMatch}`, 'MatchDetail');
    expect(generated['map']).toMatchObject({ generatorId: 'symmetric-arena' });
    expect((await get('/api/v1/matches/00000000-0000-4000-8000-000000000000')).status).toBe(404);
  });

  it('downloads replays publicly and records only for players and moderators', async () => {
    const replay = await get(`/api/v1/matches/${seed.featuredMatch}/artifacts/replay`);
    expect(replay.status).toBe(200);
    expect(replay.headers.get('access-control-allow-origin')).toBe('*');
    expect(replay.headers.get('content-disposition')).toContain('.replay');
    expect(Buffer.from(await replay.arrayBuffer()).toString()).toBe('a real replay would be here');
    expect((await get(`/api/v1/matches/${seed.featuredMatch}/artifacts/record`)).status).toBe(401);
    // Kestrel did not play the newest ranked match (Bradley against Mirelle).
    expect(
      (await get(`/api/v1/matches/${seed.featuredMatch}/artifacts/record`, seed.userSession))
        .status,
    ).toBe(404);
    const asAdmin = await get(
      `/api/v1/matches/${seed.featuredMatch}/artifacts/record`,
      seed.adminSession,
    );
    expect(asAdmin.status).toBe(200);
    expect((await get(`/api/v1/matches/${seed.featuredMatch}/artifacts/other`)).status).toBe(404);
    expect((await get(`/api/v1/matches/${seed.runningMatch}/artifacts/replay`)).status).toBe(404);
  });

  it('lets moderators look matches up by id, player name, account or status', async () => {
    expect((await get('/api/v1/admin/matches')).status).toBe(401);
    expect((await get('/api/v1/admin/matches', seed.userSession)).status).toBe(403);
    const byName = await ok('/api/v1/admin/matches?q=kestr', 'MatchList', seed.adminSession);
    expect(byName['items'].length).toBeGreaterThanOrEqual(6);
    const running = await ok(
      '/api/v1/admin/matches?status=running',
      'MatchList',
      seed.adminSession,
    );
    expect(running['items'].map((m: { id: string }) => m.id)).toEqual([seed.runningMatch]);
    const byId = await ok(
      `/api/v1/admin/matches?q=${seed.vsAiMatch}`,
      'MatchList',
      seed.adminSession,
    );
    expect(byId['items'].map((m: { id: string }) => m.id)).toEqual([seed.vsAiMatch]);
    const byAccount = await ok(
      `/api/v1/admin/matches?q=${seed.accounts.guest}`,
      'MatchList',
      seed.adminSession,
    );
    expect(byAccount['items'].map((m: { id: string }) => m.id)).toEqual([seed.publicRoomMatch]);
    expect((await get('/api/v1/admin/matches?status=lost', seed.adminSession)).status).toBe(400);
  });
});

describe('stats', () => {
  it('counts players online, live matches and matches of the last day', async () => {
    // One player waiting in the ranked 1v1 queue (the answer is cached, so before the first call).
    await sql`
      INSERT INTO queue_tickets (queue_id, account_id, sim_version, region_rtts, rating_mu, rating_sigma)
      SELECT 'ranked-1v1', ${seed.accounts.guest}, sim_version, '[]'::jsonb, 25, 8.3 FROM matches LIMIT 1
    `.execute(harness.database.db);
    const response = await get('/api/v1/stats');
    expect(response.headers.get('cache-control')).toBe('public, max-age=30');
    const stats = (await json(response)) as Doc;
    expect(checkDocument('InstanceStats', stats).issues).toEqual([]);
    // The seeded running match has players in it.
    expect(stats.liveMatches).toBeGreaterThanOrEqual(1);
    expect(stats.playersOnline).toBeGreaterThanOrEqual(1);
    expect(stats.matchesToday).toBeGreaterThanOrEqual(1);
    expect(stats.activeWindowMinutes).toBe(15);
    // Every configured queue, zero included.
    expect(stats.queues).toEqual([
      { id: 'ranked-1v1', searching: 1 },
      { id: 'ranked-2v2', searching: 0 },
    ]);
  });
});

describe('app links', () => {
  it('serves assetlinks.json and apple-app-site-association from instance config', async () => {
    const android = await get('/.well-known/assetlinks.json');
    expect(android.status).toBe(200);
    expect(android.headers.get('content-type')).toContain('application/json');
    const statements = (await android.json()) as {
      target: { package_name: string; sha256_cert_fingerprints: string[] };
    }[];
    expect(statements[0]!.target.package_name).toBe('org.globulation2.glob2');
    expect(statements[0]!.target.sha256_cert_fingerprints[0]).toMatch(/^AB(:AB){31}$/);
    const apple = await get('/.well-known/apple-app-site-association');
    expect(apple.status).toBe(200);
    const association = (await apple.json()) as {
      applinks: { details: { appIDs?: string[]; components?: unknown[]; paths?: string[] }[] };
    };
    expect(association.applinks.details[0]).toMatchObject({
      appIDs: ['ABCDE12345.org.globulation2.glob2'],
      components: [{ '/': '/j/*' }, { '/': '/play/*' }],
    });
    expect(association.applinks.details[1]).toMatchObject({ paths: ['/j/*', '/play/*'] });
  });

  it('answers 404 on instances without app links', async () => {
    const plain = await harness.start();
    expect((await fetch(`${plain.url}/.well-known/assetlinks.json`)).status).toBe(404);
    expect((await fetch(`${plain.url}/.well-known/apple-app-site-association`)).status).toBe(404);
  });
});
