// Map catalog against Postgres: creating maps and uploading versions (with
// validate-map and render-preview jobs), visibility rules for listings,
// details, files, previews and blobs by hash, filters and paging, likes,
// reports and moderation roles, download and play counts, and rooms that use
// catalog versions by hash.
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { checkDocument as check, simVersionKey } from '@glob2/protocol';
import {
  FakeEngine,
  RELAY_KEY,
  fakeMapBytes,
  guestPlayer,
  registerRelay,
  registeredPlayer,
  relayCall,
  serveSim,
  type Player,
} from './playSupport.ts';
import { SIM, createHarness, json, type Harness, type Instance } from './support.ts';

const ORIGIN = 'http://play.test';

let harness: Harness;
let a: Instance;
let b: Instance;
let engine: FakeEngine;
const players: Player[] = [];

beforeAll(async () => {
  harness = await createHarness();
  await serveSim(harness.database.db);
  // Many accounts and sockets from one address: lift the per-address limits.
  const instance = {
    auth: { providers: [], local: { enabled: true } },
    limits: { authPerMinute: 1000, guestsPerHour: 1000, realtimeConnectionsPerIp: 1000 },
  };
  a = await harness.start({ origin: ORIGIN, relayKeys: [{ key: RELAY_KEY }], instance });
  b = await harness.start({ origin: ORIGIN, relayKeys: [{ key: RELAY_KEY }], instance });
  engine = new FakeEngine(harness.database.db, harness.blobs);
});

afterAll(async () => {
  for (const player of players) player.client.close();
  await harness?.close();
});

async function registered(name = 'Mapper', instance: Instance = a): Promise<Player> {
  const p = await registeredPlayer(instance, name);
  players.push(p);
  return p;
}

async function guest(instance: Instance = a): Promise<Player> {
  const p = await guestPlayer(instance);
  players.push(p);
  return p;
}

async function moderator(role: 'moderator' | 'admin' = 'moderator'): Promise<Player> {
  const p = await registered(role === 'admin' ? 'Admin' : 'Mod');
  await harness.database.db
    .updateTable('accounts')
    .set({ role })
    .where('id', '=', p.accountId)
    .execute();
  return p;
}

/** A REST call on replica A (or `instance`), signed in as `who` when given. */
async function api(
  method: string,
  path: string,
  who?: Player,
  body?: unknown,
  instance: Instance = a,
): Promise<Response> {
  const headers: Record<string, string> = {};
  if (who) headers['authorization'] = `Bearer ${who.accessToken}`;
  let payload: string | Uint8Array | undefined;
  if (body instanceof Uint8Array) {
    headers['content-type'] = 'application/octet-stream';
    payload = body;
  } else if (body !== undefined) {
    headers['content-type'] = 'application/json';
    payload = JSON.stringify(body);
  }
  return fetch(`${instance.url}${path}`, { method, headers, body: payload });
}

async function createMap(owner: Player, body: Record<string, unknown> = {}) {
  const response = await api('POST', '/api/v1/maps', owner, {
    title: 'Two Rivers',
    description: 'A river map.',
    ...body,
  });
  expect(response.status).toBe(201);
  const map = await json(response);
  expect(check('MapInfo', map).stage).toBe('ok');
  return map as { id: string; visibility: string } & Record<string, unknown>;
}

async function upload(owner: Player, mapId: string, bytes: Uint8Array, query = '') {
  return api('POST', `/api/v1/maps/${mapId}/versions${query}`, owner, bytes);
}

/** Creates a map with one validated, previewed version. */
async function publishedMap(
  owner: Player,
  options: { teams?: number; seed?: number; visibility?: string; title?: string } = {},
) {
  const map = await createMap(owner, {
    ...(options.visibility ? { visibility: options.visibility } : {}),
    ...(options.title ? { title: options.title } : {}),
  });
  const bytes = fakeMapBytes(options.teams ?? 2, options.seed ?? Math.floor(Math.random() * 1e9));
  const response = await upload(owner, map.id, bytes);
  expect(response.status).toBe(201);
  const version = await json(response);
  await engine.runPending();
  return { id: map.id, hash: version['hash'] as string };
}

describe('creating maps and uploading versions', () => {
  it('validates and previews a version with engine jobs, then shows it as the latest', async () => {
    const owner = await registered();
    expect((await api('POST', '/api/v1/maps', undefined, { title: 'x' })).status).toBe(401);
    const map = await createMap(owner, { madeWith: 'hand' });
    // Unlisted unless the owner chooses otherwise; nothing to show yet.
    expect(map).toMatchObject({
      visibility: 'unlisted',
      hidden: false,
      madeWith: 'hand',
      stats: { plays: 0, downloads: 0, likes: 0 },
    });
    expect(map['latestVersion']).toBeUndefined();

    const bytes = fakeMapBytes(3, 101);
    const response = await upload(
      owner,
      map.id,
      bytes,
      `?simVersion=${simVersionKey(SIM)}&notes=First%20cut`,
    );
    expect(response.status).toBe(201);
    const pending = await json(response);
    expect(check('MapVersionInfo', pending).stage).toBe('ok');
    expect(pending).toMatchObject({
      validation: 'pending',
      preview: 'pending',
      notes: 'First cut',
    });
    expect(pending['downloadUrl']).toBe(
      `${ORIGIN}/api/v1/maps/${map.id}/versions/${pending['hash']}/file`,
    );
    const db = harness.database.db;
    const row = await db
      .selectFrom('map_versions')
      .select(['validate_job_id', 'preview_job_id'])
      .where('hash', '=', pending['hash'] as string)
      .executeTakeFirstOrThrow();
    const jobs = await db
      .selectFrom('engine_jobs')
      .select(['kind', 'payload'])
      .where('id', 'in', [row.validate_job_id ?? '', row.preview_job_id ?? ''])
      .execute();
    expect(jobs.map((j) => j.kind).sort()).toEqual(['render-preview', 'validate-map']);

    expect(await engine.runPending()).toBe(2);
    const detail = await json(await api('GET', `/api/v1/maps/${map.id}`, owner));
    expect(check('MapDetail', detail).stage).toBe('ok');
    const info = detail['map'] as Record<string, unknown>;
    expect(info['latestVersion']).toMatchObject({
      hash: pending['hash'],
      validation: 'valid',
      width: 64,
      height: 64,
      teamCount: 3,
      minVersionMinor: SIM.versionMinor,
      simVersion: SIM,
      fileTitle: 'Uploaded map',
      preview: 'ready',
      previewUrl: `${ORIGIN}/api/v1/maps/${map.id}/versions/${pending['hash']}/preview.png`,
    });
    expect(detail['viewer']).toEqual({
      owner: true,
      moderator: false,
      liked: false,
      reported: false,
    });

    // The same bytes again answer the existing version.
    const again = await upload(owner, map.id, bytes);
    expect(again.status).toBe(200);
    expect((await json(again))['validation']).toBe('valid');

    // The preview and the file are served.
    const preview = await api(
      'GET',
      `/api/v1/maps/${map.id}/versions/${pending['hash']}/preview.png`,
    );
    expect(preview.status).toBe(200);
    expect(preview.headers.get('content-type')).toBe('image/png');
    const file = await api('GET', `/api/v1/maps/${map.id}/versions/${pending['hash']}/file`);
    expect(file.status).toBe(200);
    expect(Buffer.from(await file.arrayBuffer()).equals(bytes)).toBe(true);
    expect(file.headers.get('content-disposition')).toContain('Two Rivers.map');
  });

  it('marks files the engine cannot load invalid and hides them from everyone but the owner', async () => {
    const owner = await registered();
    const map = await createMap(owner, { visibility: 'public' });
    const response = await upload(owner, map.id, new Uint8Array(Buffer.from('not a map at all')));
    const version = await json(response);
    await engine.runPending();
    const mine = await json(
      await api('GET', `/api/v1/maps/${map.id}/versions/${version['hash']}`, owner),
    );
    expect(mine).toMatchObject({
      validation: 'invalid',
      reason: 'not a Globulation 2 file',
      preview: 'failed',
    });
    const other = await api('GET', `/api/v1/maps/${map.id}/versions/${version['hash']}`);
    expect(other.status).toBe(404);
    // A map without a valid version is not listed, even when public.
    const listed = await json(await api('GET', '/api/v1/maps?limit=100'));
    expect((listed['items'] as { id: string }[]).some((m) => m.id === map.id)).toBe(false);
  });

  it('reuses a validation of the same bytes and refuses other owners, bad versions and empty bodies', async () => {
    const owner = await registered();
    const first = await publishedMap(owner, { seed: 555 });
    const copy = await createMap(owner, { title: 'Copy' });
    const response = await upload(owner, copy.id, fakeMapBytes(2, 555));
    expect(response.status).toBe(201);
    // Already validated and previewed: no new jobs.
    expect(await json(response)).toMatchObject({
      hash: first.hash,
      validation: 'valid',
      preview: 'ready',
    });
    expect(await engine.runPending()).toBe(0);

    const stranger = await registered('Stranger');
    expect((await upload(stranger, copy.id, fakeMapBytes(2, 1))).status).toBe(403);
    expect((await upload(owner, copy.id, fakeMapBytes(2, 1), '?simVersion=1-2-3')).status).toBe(
      400,
    );
    const other = { ...SIM, dataHash: 'cd'.repeat(32) };
    expect(
      (await upload(owner, copy.id, fakeMapBytes(2, 1), `?simVersion=${simVersionKey(other)}`))
        .status,
    ).toBe(426);
    expect((await upload(owner, copy.id, new Uint8Array())).status).toBe(400);
  });

  it('limits version uploads per account', async () => {
    const owner = await registered('Busy');
    const map = await createMap(owner);
    const statuses: number[] = [];
    for (let i = 0; i < 21; i++) {
      statuses.push((await upload(owner, map.id, fakeMapBytes(2, 70_000 + i))).status);
    }
    expect(statuses.slice(0, 20).every((s) => s === 201)).toBe(true);
    expect(statuses[20]).toBe(429);
    await engine.runPending();
  });

  it('lets only registered accounts publish', async () => {
    const visitor = await guest();
    const refused = await api('POST', '/api/v1/maps', visitor, {
      title: 'Mine',
      visibility: 'public',
    });
    expect(refused.status).toBe(403);
    const unlisted = await createMap(visitor);
    expect(unlisted.visibility).toBe('unlisted');
    const patch = await api('PATCH', `/api/v1/maps/${unlisted.id}`, visitor, {
      visibility: 'public',
    });
    expect(patch.status).toBe(403);
  });
});

describe('visibility', () => {
  let owner: Player;
  let viewer: Player;
  let mod: Player;
  const maps: Record<'public' | 'unlisted' | 'private', { id: string; hash: string }> = {} as never;

  beforeAll(async () => {
    owner = await registered('Owner');
    viewer = await registered('Viewer');
    mod = await moderator();
    for (const visibility of ['public', 'unlisted', 'private'] as const) {
      maps[visibility] = await publishedMap(owner, { visibility, title: `Vis ${visibility}` });
    }
  });

  it('lists public maps only, and every map to its owner', async () => {
    const ids = async (path: string, who?: Player) =>
      ((await json(await api('GET', path, who)))['items'] as { id: string }[]).map((m) => m.id);
    const listed = await ids('/api/v1/maps?limit=100');
    expect(listed).toContain(maps.public.id);
    expect(listed).not.toContain(maps.unlisted.id);
    expect(listed).not.toContain(maps.private.id);
    const byOwner = await ids(`/api/v1/maps?owner=${owner.accountId}`, viewer);
    expect(byOwner).toEqual([maps.public.id]);
    const mine = await ids('/api/v1/maps?owner=me', owner);
    expect(mine.sort()).toEqual(
      Object.values(maps)
        .map((m) => m.id)
        .sort(),
    );
    expect(await ids(`/api/v1/maps?owner=${owner.accountId}`, mod)).toHaveLength(3);
    expect((await api('GET', '/api/v1/maps?owner=me')).status).toBe(401);
  });

  it('shows unlisted maps by id to anyone, private ones to the owner and moderators', async () => {
    for (const who of [undefined, viewer]) {
      expect((await api('GET', `/api/v1/maps/${maps.public.id}`, who)).status).toBe(200);
      expect((await api('GET', `/api/v1/maps/${maps.unlisted.id}`, who)).status).toBe(200);
      expect((await api('GET', `/api/v1/maps/${maps.private.id}`, who)).status).toBe(404);
    }
    expect((await api('GET', `/api/v1/maps/${maps.private.id}`, owner)).status).toBe(200);
    const asMod = await json(await api('GET', `/api/v1/maps/${maps.private.id}`, mod));
    expect(asMod['viewer']).toMatchObject({ owner: false, moderator: true });
  });

  it('applies the same rules to files, previews and blobs by hash', async () => {
    const file = (m: { id: string; hash: string }) =>
      `/api/v1/maps/${m.id}/versions/${m.hash}/file`;
    const preview = (m: { id: string; hash: string }) =>
      `/api/v1/maps/${m.id}/versions/${m.hash}/preview.png`;
    const blob = (m: { hash: string }) => `/api/v1/blobs/maps/${m.hash}`;
    for (const path of [
      file(maps.unlisted),
      preview(maps.public),
      blob(maps.public),
      blob(maps.unlisted),
    ]) {
      expect((await api('GET', path)).status, path).toBe(200);
    }
    expect((await api('GET', file(maps.private), viewer)).status).toBe(404);
    expect((await api('GET', preview(maps.private), viewer)).status).toBe(404);
    expect((await api('GET', blob(maps.private))).status).toBe(401);
    expect((await api('GET', blob(maps.private), viewer)).status).toBe(404);
    expect((await api('GET', blob(maps.private), owner)).status).toBe(200);
    expect((await api('GET', blob(maps.private), mod)).status).toBe(200);
    expect((await api('GET', file(maps.private), owner)).status).toBe(200);
  });

  it('hides a moderated map from everyone but its owner and moderators', async () => {
    const target = await publishedMap(owner, { visibility: 'public', title: 'Hidden soon' });
    expect(
      (await api('POST', `/api/v1/admin/maps/${target.id}/hide`, viewer, { reason: 'x' })).status,
    ).toBe(403);
    const hidden = await api('POST', `/api/v1/admin/maps/${target.id}/hide`, mod, {
      reason: 'Offensive name',
    });
    expect(hidden.status).toBe(200);
    expect(await json(hidden)).toMatchObject({ hidden: true, hiddenReason: 'Offensive name' });
    expect((await api('GET', `/api/v1/maps/${target.id}`, viewer)).status).toBe(404);
    expect((await api('GET', `/api/v1/blobs/maps/${target.hash}`)).status).toBe(401);
    const forOwner = await json(await api('GET', `/api/v1/maps/${target.id}`, owner));
    expect(forOwner['map']).toMatchObject({ hidden: true, hiddenReason: 'Offensive name' });
    const listed = await json(await api('GET', '/api/v1/maps?limit=100'));
    expect((listed['items'] as { id: string }[]).some((m) => m.id === target.id)).toBe(false);
    // Rooms cannot choose it.
    const refused = await owner.client.call('room.create', {
      name: 'Hidden map',
      visibility: 'link',
      map: { kind: 'catalog', hash: target.hash },
    });
    expect(refused.error?.code).toBe('bad_request');

    const unhidden = await json(await api('POST', `/api/v1/admin/maps/${target.id}/unhide`, mod));
    expect(unhidden).toMatchObject({ hidden: false });
    expect(unhidden['hiddenReason']).toBeUndefined();
    expect((await api('GET', `/api/v1/maps/${target.id}`, viewer)).status).toBe(200);
    const audit = await harness.database.db
      .selectFrom('admin_audit_log')
      .select(['action', 'actor_account_id'])
      .where('target_type', '=', 'map')
      .where('target_id', '=', target.id)
      .orderBy('id')
      .execute();
    expect(audit).toEqual([
      { action: 'map.hide', actor_account_id: mod.accountId },
      { action: 'map.unhide', actor_account_id: mod.accountId },
    ]);
  });
});

describe('browsing', () => {
  it('filters by teams, size, how a map was made and title, and pages by sort order', async () => {
    const owner = await registered('Browser');
    const tag = `Zq${Date.now() % 100000}`;
    const made: string[] = [];
    for (let i = 0; i < 5; i++) {
      const map = await createMap(owner, {
        title: `${tag} ${i}`,
        visibility: 'public',
        ...(i % 2 === 0
          ? {
              generator: {
                generatorId: 'even-ground',
                revision: 2,
                params: { width: 7, height: 7, teams: 2 },
                seed: i,
                candidates: 5,
                startingUnitLevel: 0,
              },
            }
          : {}),
      });
      await upload(owner, map.id, fakeMapBytes(i < 3 ? 2 : 4, 9000 + i));
      made.push(map.id);
    }
    await engine.runPending();
    // Likes give the likes order something to sort.
    const fans = [await registered('Fan'), await registered('Fan')];
    for (const fan of fans)
      expect((await api('PUT', `/api/v1/maps/${made[4]}/like`, fan)).status).toBe(200);
    expect((await api('PUT', `/api/v1/maps/${made[1]}/like`, fans[0])).status).toBe(200);

    const titles = async (query: string) =>
      (
        (await json(await api('GET', `/api/v1/maps?q=${tag}&${query}`)))['items'] as {
          title: string;
        }[]
      ).map((m) => m.title);
    expect((await titles('teams=4')).sort()).toEqual([`${tag} 3`, `${tag} 4`]);
    expect((await titles('madeWith=generator')).sort()).toEqual([
      `${tag} 0`,
      `${tag} 2`,
      `${tag} 4`,
    ]);
    expect(await titles('minSide=65')).toEqual([]);
    expect(await titles('maxSide=64')).toHaveLength(5);
    expect((await titles('sort=likes')).slice(0, 2)).toEqual([`${tag} 4`, `${tag} 1`]);
    expect((await api('GET', '/api/v1/maps?sort=best')).status).toBe(400);
    expect((await api('GET', '/api/v1/maps?teams=13')).status).toBe(400);

    // Paging covers every map exactly once, in each sort order.
    for (const sort of ['recent', 'likes']) {
      const seen: string[] = [];
      let cursor: string | undefined;
      do {
        const page = await json(
          await api(
            'GET',
            `/api/v1/maps?q=${tag}&sort=${sort}&limit=2${cursor ? `&cursor=${cursor}` : ''}`,
          ),
        );
        expect(check('MapList', page).stage).toBe('ok');
        seen.push(...(page['items'] as { id: string }[]).map((m) => m.id));
        cursor = page['nextCursor'] as string | undefined;
      } while (cursor);
      expect(seen.sort()).toEqual([...made].sort());
    }
    expect((await api('GET', '/api/v1/maps?cursor=nonsense')).status).toBe(400);
  });
});

describe('likes, reports and moderation', () => {
  it('counts likes of registered accounts once each', async () => {
    const owner = await registered();
    const map = await publishedMap(owner, { visibility: 'public' });
    const fan = await registered('Fan');
    const liked = await json(await api('PUT', `/api/v1/maps/${map.id}/like`, fan));
    expect(check('MapLikeResult', liked).stage).toBe('ok');
    expect(liked).toEqual({ liked: true, likes: 1 });
    expect(await json(await api('PUT', `/api/v1/maps/${map.id}/like`, fan, undefined, b))).toEqual({
      liked: true,
      likes: 1,
    });
    expect((await api('PUT', `/api/v1/maps/${map.id}/like`, await guest())).status).toBe(403);
    expect((await api('PUT', `/api/v1/maps/${map.id}/like`)).status).toBe(401);
    const detail = await json(await api('GET', `/api/v1/maps/${map.id}`, fan));
    expect(detail['viewer']).toMatchObject({ liked: true });
    expect(await json(await api('DELETE', `/api/v1/maps/${map.id}/like`, fan))).toEqual({
      liked: false,
      likes: 0,
    });
    expect(await json(await api('DELETE', `/api/v1/maps/${map.id}/like`, fan))).toEqual({
      liked: false,
      likes: 0,
    });
  });

  it('takes reports, lists them for moderators and resolves them, hiding the map if asked', async () => {
    const owner = await registered();
    const map = await publishedMap(owner, { visibility: 'public', title: 'Reported' });
    const reporter = await guest();
    const first = await api('POST', `/api/v1/maps/${map.id}/reports`, reporter, {
      reason: 'offensive',
      details: 'Rude words in the description.',
    });
    expect(first.status).toBe(201);
    const receipt = await json(first);
    expect(check('MapReportReceipt', receipt).stage).toBe('ok');
    // One open report per reporter: a repeat answers the open one.
    const repeat = await api('POST', `/api/v1/maps/${map.id}/reports`, reporter, {
      reason: 'other',
      details: '',
    });
    expect(repeat.status).toBe(200);
    expect((await json(repeat))['id']).toBe(receipt['id']);
    expect(
      (
        await api('POST', `/api/v1/maps/${map.id}/reports`, reporter, {
          reason: 'spam',
          details: '',
        })
      ).status,
    ).toBe(400);

    const user = await registered('Nosy');
    expect((await api('GET', '/api/v1/admin/map-reports', user)).status).toBe(403);
    expect((await api('GET', '/api/v1/admin/map-reports')).status).toBe(401);
    const mod = await moderator();
    const open = await json(await api('GET', `/api/v1/admin/map-reports?mapId=${map.id}`, mod));
    expect(check('MapReportList', open).stage).toBe('ok');
    expect(open['items']).toHaveLength(1);
    expect((open['items'] as Record<string, unknown>[])[0]).toMatchObject({
      id: receipt['id'],
      reason: 'offensive',
      status: 'open',
      reporter: { id: reporter.accountId },
      map: { id: map.id },
    });

    const resolved = await api('POST', `/api/v1/admin/map-reports/${receipt['id']}/resolve`, mod, {
      status: 'resolved',
      note: 'Description is abusive.',
      hideMap: true,
    });
    expect(resolved.status).toBe(200);
    const report = await json(resolved);
    expect(check('MapReportInfo', report).stage).toBe('ok');
    expect(report).toMatchObject({
      status: 'resolved',
      note: 'Description is abusive.',
      resolvedBy: { id: mod.accountId },
      map: { hidden: true, hiddenReason: 'Description is abusive.' },
    });
    const stillOpen = await json(
      await api('GET', `/api/v1/admin/map-reports?mapId=${map.id}`, mod),
    );
    expect(stillOpen['items']).toEqual([]);
    const all = await json(
      await api('GET', `/api/v1/admin/map-reports?mapId=${map.id}&status=all`, mod),
    );
    expect(all['items']).toHaveLength(1);
    // The reporter may report again once the first report is closed.
    expect(
      (
        await api('POST', `/api/v1/maps/${map.id}/reports`, owner, {
          reason: 'broken',
          details: '',
        })
      ).status,
    ).toBe(201);
  });

  it('lets owners edit and delete their maps, and administrators but not moderators delete others', async () => {
    const owner = await registered();
    const map = await publishedMap(owner, { visibility: 'unlisted' });
    const stranger = await registered('Stranger');
    expect(
      (await api('PATCH', `/api/v1/maps/${map.id}`, stranger, { title: 'Mine now' })).status,
    ).toBe(403);
    const edited = await json(
      await api('PATCH', `/api/v1/maps/${map.id}`, owner, {
        title: 'Renamed',
        description: 'New text',
        visibility: 'public',
      }),
    );
    expect(edited).toMatchObject({
      title: 'Renamed',
      description: 'New text',
      visibility: 'public',
    });
    expect((await api('PATCH', `/api/v1/maps/${map.id}`, owner, { owner: 'x' })).status).toBe(400);

    const mod = await moderator();
    expect((await api('DELETE', `/api/v1/maps/${map.id}`, mod)).status).toBe(403);
    const admin = await moderator('admin');
    expect((await api('DELETE', `/api/v1/maps/${map.id}`, admin)).status).toBe(204);
    expect((await api('GET', `/api/v1/maps/${map.id}`, owner)).status).toBe(404);
    // The bytes stay for matches and rooms that used them.
    expect((await api('GET', `/api/v1/blobs/maps/${map.hash}`, owner)).status).toBe(200);

    const mine = await publishedMap(owner);
    expect(
      (await api('DELETE', `/api/v1/maps/${mine.id}/versions/${mine.hash}`, owner)).status,
    ).toBe(204);
    const empty = await json(await api('GET', `/api/v1/maps/${mine.id}`, owner));
    expect(empty['versions']).toEqual([]);
    expect((empty['map'] as Record<string, unknown>)['latestVersion']).toBeUndefined();
    expect((await api('DELETE', `/api/v1/maps/${mine.id}`, owner)).status).toBe(204);
  });
});

describe('counts and rooms', () => {
  it('counts downloads once per downloader and day, never the owner', async () => {
    const owner = await registered();
    const map = await publishedMap(owner, { visibility: 'public' });
    const path = `/api/v1/maps/${map.id}/versions/${map.hash}/file`;
    const fan = await registered('Fan');
    for (const who of [owner, owner, fan, fan, undefined, undefined]) {
      expect((await api('GET', path, who)).status).toBe(200);
    }
    const info = (await json(await api('GET', `/api/v1/maps/${map.id}`)))['map'] as {
      stats: { downloads: number };
    };
    // The fan once, and anonymous once (by address).
    expect(info.stats.downloads).toBe(2);
  });

  it('lets rooms use a catalog version by hash and counts the play when the match ends', async () => {
    await registerRelay(a, 'relay-catalog');
    const owner = await registered('Host');
    const map = await publishedMap(owner, { visibility: 'unlisted', teams: 2 });
    const friend = await guest(b);
    // Anyone can host on an unlisted map they have the hash of.
    const created = await friend.client.ok('room.create', {
      name: 'Catalog game',
      visibility: 'link',
      map: { kind: 'catalog', hash: map.hash, mapId: map.id },
    });
    const room = created['room'] as {
      id: string;
      code: string;
      mapStatus: string;
      seats: unknown[];
    };
    expect(room.mapStatus).toBe('ready');
    expect(room.seats).toHaveLength(2);
    await owner.client.ok('room.join', { code: room.code });
    await owner.client.ok('room.setSeat', { roomId: room.id, seat: 1, occupant: { kind: 'self' } });
    await owner.client.ok('room.setReady', { roomId: room.id, ready: true });
    const started = await friend.client.ok('room.start', { roomId: room.id });
    const matchId = started['matchId'] as string;
    const setup = await json(await relayCall(a, 'GET', `/matches/${matchId}/setup`));
    expect(setup['map']).toEqual({ kind: 'catalog', hash: map.hash, mapId: map.id });

    const record = new Uint8Array(Buffer.from('G2MR catalog record'));
    const receipt = await json(await relayCall(a, 'PUT', `/matches/${matchId}/record`, record));
    const report = {
      matchId,
      relayId: 'relay-catalog',
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
    };
    expect((await relayCall(a, 'POST', `/matches/${matchId}/end`, report)).status).toBe(200);
    // A repeated end report does not count twice.
    await relayCall(a, 'POST', `/matches/${matchId}/end`, report);
    const info = (await json(await api('GET', `/api/v1/maps/${map.id}`)))['map'] as {
      stats: { plays: number };
    };
    expect(info.stats.plays).toBe(1);
  });

  it('refuses private maps of others and versions saved by a newer engine', async () => {
    const owner = await registered();
    const secret = await publishedMap(owner, { visibility: 'private' });
    const other = await guest();
    const refused = await other.client.call('room.create', {
      name: 'Not yours',
      visibility: 'link',
      map: { kind: 'catalog', hash: secret.hash },
    });
    expect(refused.error?.code).toBe('bad_request');
    const own = await owner.client.call('room.create', {
      name: 'Mine',
      visibility: 'link',
      map: { kind: 'catalog', hash: secret.hash },
    });
    expect(own.ok).toBe(true);

    const newer = await publishedMap(owner, { visibility: 'public' });
    await harness.database.db
      .updateTable('map_versions')
      .set({ min_version_minor: SIM.versionMinor + 1 })
      .where('hash', '=', newer.hash)
      .execute();
    const tooNew = await other.client.call('room.create', {
      name: 'Too new',
      visibility: 'link',
      map: { kind: 'catalog', hash: newer.hash },
    });
    expect(tooNew.error?.code).toBe('bad_request');
  });
});
