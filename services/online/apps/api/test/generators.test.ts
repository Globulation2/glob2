import { beforeAll, beforeEach, afterAll, it, expect } from 'vitest';
import { createHash } from 'node:crypto';
import { sql } from 'kysely';
import {
  simVersionKey,
  pendingGeneratorReport,
  type GeneratorInfo,
  type GeneratorUpload,
  type GeneratorList,
} from '@glob2/protocol';
import { handleEngineJobResult } from '@glob2/play';
import { putContent, maintainGeneratorLibrary } from '@glob2/core';
import { RealtimeClient } from './support.ts';
import { createHarness, SIM, type Harness, type Instance } from './support.ts';
import {
  registeredPlayer,
  guestPlayer,
  serveSim,
  roomState,
  waitUntil,
  fakeMapBytes,
  type Player,
} from './playSupport.ts';
let harness: Harness, app: Instance, owner: Player, other: Player, guest: Player;
const example = {
  seed: 19,
  params: { width: 7, height: 7, teams: 4, workers: 4 },
  candidates: 1,
  startingUnitLevel: 0,
};
beforeAll(async () => {
  harness = await createHarness();
  await serveSim(harness.database.db);
  await harness.database.db
    .updateTable('engine_agents')
    .set({ kinds: ['validate-generator', 'generate-script-map'] })
    .execute();
  app = await harness.start({
    origin: 'http://generators.test',
    instance: {
      auth: { providers: [], local: { enabled: true } },
      limits: { authPerMinute: 1000, guestsPerHour: 1000 },
    },
  });
  owner = await registeredPlayer(app, 'GeneratorAuthor');
  other = await registeredPlayer(app, 'GeneratorFan');
  guest = await guestPlayer(app);
});
beforeEach(async () => {
  await harness.database.db
    .deleteFrom('rate_limits')
    .where('bucket', 'in', ['generator-upload', 'generator-publish', 'generator-report'])
    .execute();
});
afterAll(async () => {
  owner?.client.close();
  other?.client.close();
  guest?.client.close();
  await harness?.close();
});
async function call(method: string, path: string, p?: Player, data?: unknown) {
  return fetch(app.url + path, {
    method,
    headers: {
      ...(p ? { authorization: 'Bearer ' + p.accessToken } : {}),
      ...(data === undefined
        ? {}
        : {
            'content-type':
              data instanceof Uint8Array ? 'application/octet-stream' : 'application/json',
          }),
    },
    ...(data === undefined
      ? {}
      : { body: data instanceof Uint8Array ? Buffer.from(data) : JSON.stringify(data) }),
  });
}
let next = 0;
async function upload(
  id = `author:landscape-${next++}`,
  revision = 1,
  valid = true,
  person = owner,
) {
  const bytes = Buffer.from(
    JSON.stringify({
      formatVersion: 1,
      manifest: {
        id,
        name: 'Landscape',
        apiVersion: 1,
        revision,
        entry: 'generator.js',
        tags: ['terrain:natural', 'style:wide-open'],
        controls: [],
      },
      modules: { 'generator.js': 'export function generate(c) {}' },
    }),
  );
  const response = await call(
    'POST',
    '/api/v1/generator-uploads?example=' + encodeURIComponent(JSON.stringify(example)),
    person,
    bytes,
  );
  expect(response.status, await response.clone().text()).toBe(201);
  const u = (await response.json()) as GeneratorUpload;
  const row = await harness.database.db
    .selectFrom('generator_validations')
    .selectAll()
    .where('hash', '=', u.sourceHash)
    .executeTakeFirstOrThrow();
  const hash = createHash('sha256').update(bytes).digest('hex');
  const report = {
    ...pendingGeneratorReport(hash, simVersionKey(SIM)),
    valid,
    packageHash: hash,
    fileHash: hash,
    metadata: {
      id,
      name: 'Landscape',
      description: '',
      revision,
      apiVersion: 1,
      toolkitVersion: 1,
      editorOnly: false,
      tags: ['terrain:natural', 'style:wide-open'],
      controls: [],
    },
    samples: [{ settings: example, status: valid ? ('passed' as const) : ('failed' as const) }],
  };
  await handleEngineJobResult(harness.database.db, {
    jobId: row.job_id!,
    kind: 'validate-generator',
    agent: 'fake',
    ok: true,
    result: report,
  });
  return { u, bytes, id };
}
const publication = (uploadId: string, extra = {}) => ({
  uploadId,
  name: 'Landscape',
  description: 'River country',
  visibility: 'unlisted',
  version: '1.0',
  notes: 'First release',
  ...extra,
});
async function publish(extra = {}) {
  const staged = await upload();
  const r = await call('POST', '/api/v1/generators', owner, publication(staged.u.id, extra));
  expect(r.status, await r.clone().text()).toBe(200);
  return { ...staged, g: (await r.json()) as GeneratorInfo };
}
it('keeps staged uploads private and consumes a passing upload exactly once under concurrent retries', async () => {
  const { u } = await upload();
  expect((await call('GET', '/api/v1/generator-uploads/' + u.id, other)).status).toBe(404);
  const rs = await Promise.all([
    call('POST', '/api/v1/generators', owner, publication(u.id)),
    call('POST', '/api/v1/generators', owner, publication(u.id)),
  ]);
  expect(rs.map((r) => r.status)).toEqual([200, 200]);
  expect(((await rs[0]!.json()) as GeneratorInfo).id).toBe(
    ((await rs[1]!.json()) as GeneratorInfo).id,
  );
});
it('enforces visibility, exact downloads and immutable releases', async () => {
  const { g, bytes } = await publish({ visibility: 'private' });
  const file = `/api/v1/generators/${g.id}/versions/${g.latestVersion.id}/file`;
  expect((await call('GET', file, other)).status).toBe(404);
  expect(Buffer.from(await (await call('GET', file, owner)).arrayBuffer())).toEqual(bytes);
  expect(
    (await call('PATCH', '/api/v1/generators/' + g.id, owner, { visibility: 'public' })).status,
  ).toBe(200);
  expect((await call('GET', file)).status).toBe(200);
  await expect(
    harness.database.db
      .updateTable('generator_versions')
      .set({ notes: 'changed' })
      .where('id', '=', g.latestVersion.id)
      .execute(),
  ).rejects.toThrow(/immutable/);
});
it('reserves package IDs after deletion and requires increasing revisions', async () => {
  const { g, id } = await publish();
  const same = await upload(id, 1);
  expect(
    (
      await call(
        'POST',
        `/api/v1/generators/${g.id}/versions`,
        owner,
        publication(same.u.id, { version: '2' }),
      )
    ).status,
  ).toBe(409);
  const newer = await upload(id, 2);
  expect(
    (
      await call(
        'POST',
        `/api/v1/generators/${g.id}/versions`,
        owner,
        publication(newer.u.id, { version: '2' }),
      )
    ).status,
  ).toBe(200);
  expect((await call('DELETE', '/api/v1/generators/' + g.id, owner)).status).toBe(204);
  const fork = await upload(id, 3);
  expect((await call('POST', '/api/v1/generators', owner, publication(fork.u.id))).status).toBe(
    409,
  );
  expect((await call('GET', '/api/v1/generators/' + g.id, owner)).status).toBe(404);
});
it('rejects invalid packages and guest public publication', async () => {
  const { u } = await upload(undefined, 1, false);
  expect((await call('POST', '/api/v1/generators', owner, publication(u.id))).status).toBe(409);
  expect(
    (await call('POST', '/api/v1/generators', guest, publication(u.id, { visibility: 'public' })))
      .status,
  ).toBe(403);
});
it('binds validation to example settings instead of coalescing different requests', async () => {
  const { bytes, u } = await upload();
  const r = await call(
    'POST',
    '/api/v1/generator-uploads?example=' +
      encodeURIComponent(JSON.stringify({ ...example, seed: 91 })),
    owner,
    bytes,
  );
  expect(r.status).toBe(201);
  expect(((await r.json()) as GeneratorUpload).status).toBe('pending');
  expect((await call('GET', '/api/v1/generator-uploads/' + u.id, owner)).status).toBe(200);
});
it('filters public discovery and applies idempotent social actions', async () => {
  const { g } = await publish({ visibility: 'public' });
  const liked = await call('PUT', `/api/v1/generators/${g.id}/like`, other);
  expect(liked.status).toBe(200);
  expect(
    (
      (await (await call('PUT', `/api/v1/generators/${g.id}/like`, other)).json()) as {
        likes: number;
      }
    ).likes,
  ).toBe(1);
  const list = (await (
    await call('GET', '/api/v1/generators?tags=feature:lakes')
  ).json()) as GeneratorList;
  expect(list.items.some((v: GeneratorInfo) => v.id === g.id)).toBe(false);
  const compatible = (await (
    await call('GET', '/api/v1/generators?simVersion=' + simVersionKey(SIM))
  ).json()) as GeneratorList;
  expect(compatible.items.some((v: GeneratorInfo) => v.id === g.id)).toBe(true);
  const count = await sql<{
    n: number;
  }>`SELECT count(*)::int n FROM generator_versions WHERE generator_id=${g.id}::uuid`.execute(
    harness.database.db,
  );
  expect(count.rows[0]!.n).toBe(1);
});

async function modern(p: Player) {
  p.client.close();
  p.client = await RealtimeClient.connect(app.url);
  await p.client.hello(p.accessToken, SIM, true);
}
function descriptor(g: GeneratorInfo, seed = example.seed) {
  const v = g.latestVersion;
  return {
    ...example,
    seed,
    libraryId: g.id,
    versionId: v.id,
    fileHash: v.hash,
    packageHash: v.packageHash,
    generatorId: v.metadata.id,
    revision: v.metadata.revision,
  };
}
async function finishGeneration(seed: number, ok = true) {
  const jobs = await harness.database.db
    .selectFrom('engine_jobs')
    .selectAll()
    .where('kind', '=', 'generate-script-map')
    .where('status', '=', 'queued')
    .execute();
  const job = jobs.find(
    (j) => (j.payload as { generator: { seed: number } }).generator.seed === seed,
  )!;
  expect(job).toBeDefined();
  const packageHash = (job.payload as { generator: { packageHash: string } }).generator.packageHash;
  const stored = await putContent(harness.blobs, fakeMapBytes(4, seed));
  await handleEngineJobResult(harness.database.db, {
    jobId: job.id,
    kind: 'generate-script-map',
    agent: 'fake',
    ...(ok
      ? {
          ok: true,
          result: {
            mapHash: stored.sha256,
            size: stored.size,
            packageHash,
            chosenSeed: seed,
            map: { width: 128, height: 128, teamCount: 4 },
          },
        }
      : { ok: false, error: { code: 'bad_request', message: 'Unsupported settings' } }),
  });
  return stored.sha256;
}
it('pins room releases, rejects older clients and ignores stale generation results', async () => {
  await modern(owner);
  await modern(other);
  const { g, id } = await publish({ visibility: 'private' });
  const generator = descriptor(g, 711);
  const forbidden = await other.client.call('room.create', {
    name: 'Denied',
    visibility: 'link',
    map: { kind: 'scripted', generator },
  });
  expect(forbidden.error?.code).toBe('bad_request');
  const created = await owner.client.ok('room.create', {
    name: 'Generator room',
    visibility: 'link',
    map: { kind: 'scripted', generator },
  });
  let room = created.room as {
    id: string;
    code: string;
    revision: number;
    mapStatus: string;
    map: { generator: typeof generator; hash?: string };
    seats: { occupant: { ready?: boolean } }[];
  };
  expect(room.mapStatus).toBe('pending');
  expect((await guest.client.call('room.join', { code: room.code })).error?.code).toBe(
    'update_required',
  );
  room = (await other.client.ok('room.join', { code: room.code })).room as typeof room;
  const updated = await owner.client.ok('room.update', {
    roomId: room.id,
    revision: room.revision,
    changes: { map: { kind: 'scripted', generator: { ...generator, seed: 712 } } },
  });
  room = updated.room as typeof room;
  await finishGeneration(711);
  const current = await harness.database.db
    .selectFrom('rooms')
    .select('settings')
    .where('id', '=', room.id)
    .executeTakeFirstOrThrow();
  expect(
    (current.settings as { map: { generator: { seed: number }; hash?: string } }).map,
  ).toMatchObject({ generator: { seed: 712 } });
  expect((current.settings as { map: { hash?: string } }).map.hash).toBeUndefined();
  const mapHash = await finishGeneration(712);
  room = (await roomState(
    owner.client,
    (r) => r.id === room.id && r.mapStatus === 'ready',
  )) as typeof room;
  expect(room.map.hash).toBe(mapHash);
  expect((await call('GET', '/api/v1/blobs/maps/' + mapHash)).status).toBe(401);
  expect((await call('GET', '/api/v1/blobs/maps/' + mapHash, other)).status).toBe(200);
  const newer = await upload(id, 2);
  expect(
    (
      await call(
        'POST',
        `/api/v1/generators/${g.id}/versions`,
        owner,
        publication(newer.u.id, { version: '2' }),
      )
    ).status,
  ).toBe(200);
  expect(room.map.generator.versionId).toBe(g.latestVersion.id);
  await other.client.ok('room.setReady', { roomId: room.id, ready: true });
  room = (await roomState(
    owner.client,
    (r) =>
      r.id === room.id &&
      (r.seats as { occupant: { ready?: boolean } }[]).some((s) => s.occupant.ready),
  )) as typeof room;
  room = (
    await owner.client.ok('room.update', {
      roomId: room.id,
      revision: room.revision,
      changes: { map: { kind: 'scripted', generator: { ...generator, seed: 713 } } },
    })
  ).room as typeof room;
  expect(room.seats.every((s) => !s.occupant.ready)).toBe(true);
  await finishGeneration(713, false);
  const failed = await roomState(owner.client, (r) => r.id === room.id && r.mapStatus === 'failed');
  expect(JSON.stringify(failed)).toContain('Unsupported settings');
  await other.client.ok('room.leave', { roomId: room.id });
  await owner.client.ok('room.leave', { roomId: room.id });
});
it('reports unavailable workers and refuses hidden or editor-only releases', async () => {
  const { g } = await publish();
  await harness.database.db
    .updateTable('engine_agents')
    .set({ kinds: ['validate-generator'] })
    .execute();
  const params = {
    name: 'No worker',
    visibility: 'link',
    map: { kind: 'scripted', generator: descriptor(g, 714) },
  };
  expect((await owner.client.call('room.create', params)).error?.code).toBe('unavailable');
  await harness.database.db
    .updateTable('engine_agents')
    .set({ kinds: ['validate-generator', 'generate-script-map'] })
    .execute();
  await harness.database.db
    .updateTable('generators')
    .set({ hidden: true })
    .where('id', '=', g.id)
    .execute();
  expect((await owner.client.call('room.create', params)).error?.code).toBe('bad_request');
});
it('lets exactly one successful publisher claim an ID across different staged uploads', async () => {
  const first = await upload();
  const secondResponse = await call(
    'POST',
    '/api/v1/generator-uploads?example=' + encodeURIComponent(JSON.stringify(example)),
    other,
    first.bytes,
  );
  expect(secondResponse.status).toBe(201);
  const second = (await secondResponse.json()) as GeneratorUpload;
  const results = await Promise.all([
    call('POST', '/api/v1/generators', owner, publication(first.u.id)),
    call('POST', '/api/v1/generators', other, publication(second.id)),
  ]);
  expect(results.map((r) => r.status).sort()).toEqual([200, 409]);
  const count = await harness.database.db
    .selectFrom('generator_ids')
    .select(sql<number>`count(*)::int`.as('n'))
    .where('manifest_id', '=', first.id)
    .executeTakeFirstOrThrow();
  expect(count.n).toBe(1);
});
it('labels supplied map provenance as a claim and verifies recognized generated bytes independently', async () => {
  const { g } = await publish();
  const generator = descriptor(g, 712);
  const create = async (title: string) => {
    const r = await call('POST', '/api/v1/maps', owner, {
      title,
      visibility: 'unlisted',
      madeWith: 'hand',
    });
    expect(r.status).toBe(201);
    return ((await r.json()) as { id: string }).id;
  };
  const claimMap = await create('Claimed generator map');
  const claim = await call(
    'POST',
    `/api/v1/maps/${claimMap}/versions?generator=${encodeURIComponent(JSON.stringify(generator))}`,
    owner,
    fakeMapBytes(4, 900),
  );
  expect(claim.status, await claim.clone().text()).toBe(201);
  expect(
    ((await claim.json()) as { generatorProvenance: { verified: boolean } }).generatorProvenance
      .verified,
  ).toBe(false);
  const generatedMap = await create('Verified generator map');
  const verified = await call(
    'POST',
    `/api/v1/maps/${generatedMap}/versions`,
    owner,
    fakeMapBytes(4, 712),
  );
  expect(verified.status, await verified.clone().text()).toBe(201);
  expect(
    ((await verified.json()) as { generatorProvenance: { verified: boolean } }).generatorProvenance
      .verified,
  ).toBe(true);
});

it('removes deleted-account uploads and social data while permanently retaining ID reservations', async () => {
  const victim = await registeredPlayer(app, 'DeletedGeneratorAuthor');
  try {
    const staged = await upload('deleted:reserved', 1, true, victim);
    const response = await call('POST', '/api/v1/generators', victim, publication(staged.u.id));
    expect(response.status).toBe(200);
    const published = (await response.json()) as GeneratorInfo;
    expect((await call('PUT', '/api/v1/generators/' + published.id + '/like', victim)).status).toBe(
      200,
    );
    const target = await harness.database.db
      .selectFrom('accounts')
      .selectAll()
      .where('id', '=', victim.accountId)
      .executeTakeFirstOrThrow();
    await app.app.identity.admin.deleteAccount(undefined, target, undefined, { self: true });
    expect(
      await harness.database.db
        .selectFrom('generator_uploads')
        .selectAll()
        .where('owner_account_id', '=', victim.accountId)
        .execute(),
    ).toEqual([]);
    expect(
      await harness.database.db
        .selectFrom('generators')
        .selectAll()
        .where('owner_account_id', '=', victim.accountId)
        .execute(),
    ).toEqual([]);
    expect(
      await harness.database.db
        .selectFrom('generator_likes')
        .selectAll()
        .where('account_id', '=', victim.accountId)
        .execute(),
    ).toEqual([]);
    const reservation = await harness.database.db
      .selectFrom('generator_ids')
      .selectAll()
      .where('manifest_id', '=', staged.id)
      .executeTakeFirstOrThrow();
    expect(reservation.generator_id).toBeNull();
    const replacement = await upload(staged.id, 2);
    expect(
      (await call('POST', '/api/v1/generators', owner, publication(replacement.u.id))).status,
    ).toBe(409);
  } finally {
    victim.client.close();
  }
});

it('expires abandoned staging evidence without removing released validation coverage', async () => {
  const kept = await publish();
  const abandoned = await upload();
  const db = harness.database.db;
  const validation = await db
    .selectFrom('generator_uploads')
    .select('validation_id')
    .where('id', '=', abandoned.u.id)
    .executeTakeFirstOrThrow();
  await db
    .updateTable('generator_uploads')
    .set({ expires_at: new Date(0) })
    .where('id', '=', abandoned.u.id)
    .execute();
  await db
    .updateTable('generator_validations')
    .set({ created_at: new Date(0) })
    .where('id', '=', validation.validation_id)
    .execute();
  await maintainGeneratorLibrary(db);
  expect(
    await db.selectFrom('generator_uploads').selectAll().where('id', '=', abandoned.u.id).execute(),
  ).toEqual([]);
  expect(
    await db
      .selectFrom('generator_validations')
      .selectAll()
      .where('id', '=', validation.validation_id)
      .execute(),
  ).toEqual([]);
  expect(
    await db
      .selectFrom('generator_validations')
      .selectAll()
      .where('hash', '=', kept.u.sourceHash)
      .execute(),
  ).toHaveLength(1);
});

it('restores member support after an older socket closes and rejects legacy room reconnects', async () => {
  await modern(owner);
  const { g } = await publish();
  let room = (await owner.client.ok('room.create', { name: 'Capability room', visibility: 'link' }))
    .room as { id: string; revision: number };
  const legacy = await RealtimeClient.connect(app.url);
  try {
    await legacy.hello(owner.accessToken);
    const support = async () =>
      (
        await harness.database.db
          .selectFrom('room_members')
          .select('generator_support')
          .where('room_id', '=', room.id)
          .where('account_id', '=', owner.accountId)
          .executeTakeFirstOrThrow()
      ).generator_support;
    await waitUntil(async () => !(await support()));
    const params = () => ({
      roomId: room.id,
      revision: room.revision,
      changes: { map: { kind: 'scripted', generator: descriptor(g, 799) } },
    });
    expect((await owner.client.call('room.update', params())).error?.code).toBe('update_required');
    legacy.close();
    await legacy.waitClosed();
    await waitUntil(support);
    room = (await harness.database.db
      .selectFrom('rooms')
      .select(['id', 'revision'])
      .where('id', '=', room.id)
      .executeTakeFirstOrThrow()) as typeof room;
    const selected = await owner.client.ok('room.update', params());
    expect(selected.room).toMatchObject({ map: { kind: 'scripted' } });
    const reconnect = await RealtimeClient.connect(app.url);
    try {
      const denied = await reconnect.call('session.hello', {
        protocol: 1,
        accessToken: owner.accessToken,
        client: { platform: 'desktop', version: 'old', simVersion: SIM, generatorSharing: false },
      });
      expect(denied.error?.code).toBe('update_required');
      expect(reconnect.frames.some((f) => f.event === 'room.state')).toBe(false);
    } finally {
      reconnect.close();
    }
  } finally {
    legacy.close();
    await owner.client.ok('room.leave', { roomId: room.id });
  }
});
