import { afterAll, beforeAll, expect, it } from 'vitest';
import { putContent } from '@glob2/core';
import { registeredPlayer, guestPlayer, type Player } from './playSupport.ts';
import { createHarness, type Harness, type Instance } from './support.ts';
let harness: Harness, instance: Instance, owner: Player, other: Player, guest: Player;
const metadata = {
  title: 'Moss lantern',
  artist: 'Composer',
  description: 'Forest trio',
  license: 'CC0-1.0',
  credits: 'Composer',
  sources: [],
  tags: ['forest'],
  aiGenerated: false,
};
beforeAll(async () => {
  harness = await createHarness();
  instance = await harness.start({
    origin: 'http://music.test',
    instance: { auth: { providers: [], local: { enabled: true } } },
  });
  owner = await registeredPlayer(instance, 'MusicAuthor');
  other = await registeredPlayer(instance, 'MusicListener');
  guest = await guestPlayer(instance);
});
afterAll(async () => {
  owner?.client.close();
  other?.client.close();
  guest?.client.close();
  await harness?.close();
});
async function call(method: string, path: string, player?: Player, value?: unknown) {
  const headers: Record<string, string> = {};
  if (player) headers['authorization'] = `Bearer ${player.accessToken}`;
  if (value !== undefined)
    headers['content-type'] =
      value instanceof Uint8Array ? 'application/octet-stream' : 'application/json';
  return fetch(instance.url + path, {
    method,
    headers,
    body:
      value instanceof Uint8Array ? value : value === undefined ? undefined : JSON.stringify(value),
  });
}
it('requires a registered creator, keeps drafts private, and protects upload ownership', async () => {
  expect((await call('POST', '/api/v1/music', guest, metadata)).status).toBe(403);
  const created = await call('POST', '/api/v1/music', owner, metadata);
  expect(created.status).toBe(201);
  const release = (await created.json()) as { id: string };
  expect((await call('GET', `/api/v1/music/${release.id}`)).status).toBe(404);
  expect(
    (await call('PUT', `/api/v1/music/${release.id}/uploads/calm`, other, new Uint8Array([1])))
      .status,
  ).toBe(404);
  expect((await call('POST', `/api/v1/music/${release.id}/publish`, owner)).status).toBe(400);
  expect((await call('POST', `/api/v1/music/${release.id}/inspect`, owner)).status).toBe(400);
  expect((await call('DELETE', `/api/v1/music/${release.id}`, owner)).status).toBe(200);
});
it('publishes immutable output, filters, likes idempotently, and withdraws every download route', async () => {
  const created = await call('POST', '/api/v1/music', owner, metadata);
  const release = (await created.json()) as { id: string };
  const stored = await putContent(harness.blobs, Buffer.from('fixture audio bytes'));
  await harness.database.db
    .insertInto('blobs')
    .values({
      sha256: stored.sha256,
      size: stored.size,
      storage_key: stored.key,
      content_type: 'audio/ogg',
      visibility: 'private',
    })
    .execute();
  for (const kind of ['calm', 'building', 'combat', 'zip'])
    await harness.database.db
      .insertInto('music_assets')
      .values({ release_id: release.id, kind, sha256: stored.sha256 })
      .execute();
  await harness.database.db
    .updateTable('music_releases')
    .set({
      status: 'ready',
      result: JSON.stringify({
        frames: 480000,
        tracks: ['calm', 'building', 'combat'].map((mood) => ({
          mood,
          sha256: stored.sha256,
          bytes: stored.size,
          waveform: [0.2],
        })),
        warnings: [],
      }),
    })
    .where('id', '=', release.id)
    .execute();
  expect((await call('POST', `/api/v1/music/${release.id}/publish`, owner)).status).toBe(200);
  expect(
    (await call('PUT', `/api/v1/music/${release.id}/uploads/calm`, owner, new Uint8Array([1])))
      .status,
  ).toBe(400);
  for (let i = 0; i < 2; i++)
    expect((await call('PUT', `/api/v1/music/${release.id}/like`, other)).status).toBe(200);
  const listed = (await (
    await call('GET', '/api/v1/music?q=Moss&tag=forest&sort=likes')
  ).json()) as { items: { likes: number }[] };
  expect(listed.items).toHaveLength(1);
  expect(listed.items[0]?.likes).toBe(1);
  expect((await call('GET', `/api/v1/music/${release.id}/tracks/calm`)).status).toBe(200);
  expect(
    (await call('POST', '/api/v1/music/download', undefined, { ids: [release.id] })).status,
  ).toBe(200);
  expect((await call('DELETE', `/api/v1/music/${release.id}`, owner)).status).toBe(200);
  expect((await call('GET', `/api/v1/music/${release.id}/tracks/calm`)).status).toBe(404);
  expect((await call('GET', `/api/v1/music/${release.id}/download`, owner)).status).toBe(404);
});

it('processes real uploads, retains sources for inspection, cleans them after conversion, and retries idempotently', async () => {
  const { processMusic } = await import('../../music-worker/src/process.ts');
  const created = await call('POST', '/api/v1/music', owner, metadata);
  expect(created.status).toBe(201);
  const { id } = (await created.json()) as { id: string };
  // Ten seconds of 24 kHz mono PCM exercises resampling and channel conversion.
  const wav = Buffer.alloc(44 + 24000 * 10 * 2);
  wav.write('RIFF');
  wav.writeUInt32LE(wav.length - 8, 4);
  wav.write('WAVEfmt ', 8);
  wav.writeUInt32LE(16, 16);
  wav.writeUInt16LE(1, 20);
  wav.writeUInt16LE(1, 22);
  wav.writeUInt32LE(24000, 24);
  wav.writeUInt32LE(48000, 28);
  wav.writeUInt16LE(2, 32);
  wav.writeUInt16LE(16, 34);
  wav.write('data', 36);
  wav.writeUInt32LE(wav.length - 44, 40);
  for (let i = 0; i < 240000; i++)
    wav.writeInt16LE(Math.round(3000 * Math.sin((i * Math.PI) / 60)), 44 + i * 2);
  for (const mood of ['calm', 'building', 'combat'])
    expect((await call('PUT', `/api/v1/music/${id}/uploads/${mood}`, owner, wav)).status).toBe(200);
  expect((await call('POST', `/api/v1/music/${id}/inspect`, owner)).status).toBe(200);
  await processMusic(harness.database.db, harness.blobs, 'http://music.test', id, true);
  let row = await harness.database.db
    .selectFrom('music_releases')
    .selectAll()
    .where('id', '=', id)
    .executeTakeFirstOrThrow();
  expect(row.error).toBeNull();
  expect(row.status).toBe('inspected');
  const keys = Object.values(row.sources);
  expect(keys).toHaveLength(3);
  expect(row.inspection?.frames).toEqual([480000, 480000, 480000]);
  expect(
    (await call('POST', `/api/v1/music/${id}/convert`, owner, { repair: 'none', master: false }))
      .status,
  ).toBe(200);
  await processMusic(harness.database.db, harness.blobs, 'http://music.test', id, false);
  row = await harness.database.db
    .selectFrom('music_releases')
    .selectAll()
    .where('id', '=', id)
    .executeTakeFirstOrThrow();
  expect(row.error).toBeNull();
  expect(row.status).toBe('ready');
  expect(row.sources).toEqual({});
  for (const key of keys) expect(await harness.blobs.size(key)).toBeUndefined();
  await processMusic(harness.database.db, harness.blobs, 'http://music.test', id, false);
  expect((await call('GET', `/api/v1/music/${id}/tracks/calm`, owner)).status).toBe(200);
  expect((await call('GET', `/api/v1/music/${id}/tracks/calm`)).status).toBe(404);
  expect((await call('POST', `/api/v1/music/${id}/publish`, owner)).status).toBe(200);
  // Moderation removes both individual and bulk delivery, while retaining audit data.
  expect(
    (await call('POST', `/api/v1/music/${id}/report`, other, { reason: 'Test report' })).status,
  ).toBe(200);
  expect(
    (await call('PUT', `/api/v1/admin/music/${id}`, other, { hidden: true, reason: 'Test' }))
      .status,
  ).toBe(403);
  await harness.database.db
    .updateTable('accounts')
    .set({ role: 'moderator' })
    .where('id', '=', other.accountId)
    .execute();
  expect(
    (await call('PUT', `/api/v1/admin/music/${id}`, other, { hidden: true, reason: 'Test' }))
      .status,
  ).toBe(200);
  expect((await call('GET', `/api/v1/music/${id}/tracks/calm`)).status).toBe(404);
  expect((await call('POST', '/api/v1/music/download', undefined, { ids: [id] })).status).toBe(404);
}, 90_000);

it('expires abandoned uploads and removes their source blobs', async () => {
  const { expireMusic } = await import('@glob2/music');
  const created = await call('POST', '/api/v1/music', owner, metadata);
  const { id } = (await created.json()) as { id: string };
  expect(
    (await call('PUT', `/api/v1/music/${id}/uploads/calm`, owner, new Uint8Array([1, 2, 3])))
      .status,
  ).toBe(200);
  await harness.database.db
    .updateTable('music_releases')
    .set({ updated_at: new Date(Date.now() - 25 * 3600_000) })
    .where('id', '=', id)
    .execute();
  await expireMusic(harness.database.db, harness.blobs);
  const row = await harness.database.db
    .selectFrom('music_releases')
    .selectAll()
    .where('id', '=', id)
    .executeTakeFirstOrThrow();
  expect(row.status).toBe('failed');
  expect(row.sources).toEqual({});
});

it('cleans technically failed jobs and safely ignores cancelled jobs', async () => {
  const { processMusic } = await import('../../music-worker/src/process.ts');
  for (const cancel of [false, true]) {
    const created = await call('POST', '/api/v1/music', owner, metadata);
    expect(created.status).toBe(201);
    const { id } = (await created.json()) as { id: string };
    for (const mood of ['calm', 'building', 'combat'])
      expect(
        (await call('PUT', `/api/v1/music/${id}/uploads/${mood}`, owner, new Uint8Array([1, 2, 3])))
          .status,
      ).toBe(200);
    expect((await call('POST', `/api/v1/music/${id}/inspect`, owner)).status).toBe(200);
    if (cancel) expect((await call('DELETE', `/api/v1/music/${id}`, owner)).status).toBe(200);
    await processMusic(harness.database.db, harness.blobs, 'http://music.test', id, true);
    await processMusic(harness.database.db, harness.blobs, 'http://music.test', id, true);
    const row = await harness.database.db
      .selectFrom('music_releases')
      .selectAll()
      .where('id', '=', id)
      .executeTakeFirstOrThrow();
    expect(row.status).toBe(cancel ? 'withdrawn' : 'failed');
    expect(row.sources).toEqual({});
    const sourceKeys = [];
    for await (const blob of harness.blobs.list(`music-uploads/${id}`)) sourceKeys.push(blob.key);
    expect(sourceKeys).toEqual([]);
  }
});
it('paginates tied catalogue scores without omitting or repeating releases', async () => {
  const { randomUUID } = await import('node:crypto');
  const ids = Array.from({ length: 26 }, () => randomUUID());
  for (const id of ids)
    await harness.database.db
      .insertInto('music_releases')
      .values({
        id,
        owner_id: owner.accountId,
        metadata: JSON.stringify({ ...metadata, title: 'Pagination fixture' }),
        status: 'published',
        created_at: new Date('2026-01-01T00:00:00Z'),
        result: JSON.stringify({ frames: 480000, tracks: [], warnings: [] }),
      })
      .execute();
  const first = (await (await call('GET', '/api/v1/music?q=Pagination&sort=likes')).json()) as {
    items: { id: string }[];
    next: string;
  };
  expect(first.items).toHaveLength(24);
  const second = (await (
    await call('GET', `/api/v1/music?q=Pagination&sort=likes&cursor=${first.next}`)
  ).json()) as { items: { id: string }[]; next: null };
  expect(second.items).toHaveLength(2);
  expect(second.next).toBeNull();
  expect(new Set([...first.items, ...second.items].map((x) => x.id))).toEqual(new Set(ids));
  expect((await call('GET', '/api/v1/music?cursor=bad')).status).toBe(400);
});

it('cancels an active decoder and removes its nested temporary PCM', async () => {
  const { mkdtemp, writeFile, readFile, readdir, rm } = await import('node:fs/promises');
  const { tmpdir } = await import('node:os');
  const { join } = await import('node:path');
  const { processMusic } = await import('../../music-worker/src/process.ts');
  const directory = await mkdtemp(join(tmpdir(), 'music-cancel-test-'));
  const executable = join(directory, 'decoder');
  const marker = join(directory, 'started');
  await writeFile(
    executable,
    '#!/usr/bin/env python3\nimport os,tempfile,time\nfrom pathlib import Path\n' +
      'p=Path(tempfile.mkdtemp(prefix="music-convert-"))\n(p/"temporary.pcm").write_bytes(b"pcm")\n' +
      'Path(' +
      JSON.stringify(marker) +
      ').write_text(str(p))\ntime.sleep(60)\n',
    { mode: 0o700 },
  );
  const created = await call('POST', '/api/v1/music', other, metadata);
  expect(created.status).toBe(201);
  const { id } = (await created.json()) as { id: string };
  for (const mood of ['calm', 'building', 'combat'])
    expect(
      (await call('PUT', `/api/v1/music/${id}/uploads/${mood}`, other, new Uint8Array([1]))).status,
    ).toBe(200);
  await harness.database.db
    .updateTable('music_releases')
    .set({ status: 'converting' })
    .where('id', '=', id)
    .execute();
  const old = process.env['MUSIC_PYTHON'];
  process.env['MUSIC_PYTHON'] = executable;
  const processing = processMusic(
    harness.database.db,
    harness.blobs,
    'http://music.test',
    id,
    false,
  );
  try {
    await expect
      .poll(async () => readFile(marker, 'utf8').catch(() => ''))
      .toContain(`glob2-music-${id}-`);
    expect((await call('DELETE', `/api/v1/music/${id}`, other)).status).toBe(200);
    await processing;
    expect(
      (await readdir(tmpdir())).filter((name) => name.startsWith(`glob2-music-${id}-`)),
    ).toEqual([]);
    const row = await harness.database.db
      .selectFrom('music_releases')
      .selectAll()
      .where('id', '=', id)
      .executeTakeFirstOrThrow();
    expect(row.status).toBe('withdrawn');
    expect(row.sources).toEqual({});
  } finally {
    await harness.database.db
      .updateTable('music_releases')
      .set({ status: 'withdrawn' })
      .where('id', '=', id)
      .execute();
    await processing;
    if (old === undefined) delete process.env['MUSIC_PYTHON'];
    else process.env['MUSIC_PYTHON'] = old;
    await rm(directory, { recursive: true, force: true });
  }
});

it('includes music in account exports and removes owned releases on account deletion', async () => {
  const exported = (await (await call('GET', '/api/v1/accounts/me/export', owner)).json()) as {
    music: { releases: { id: string }[] };
  };
  expect(exported.music.releases.length).toBeGreaterThan(0);
  expect(
    (await call('DELETE', '/api/v1/accounts/me', owner, { confirmDisplayName: owner.displayName }))
      .status,
  ).toBe(204);
  expect(
    await harness.database.db
      .selectFrom('music_releases')
      .select('id')
      .where('owner_id', '=', owner.accountId)
      .execute(),
  ).toEqual([]);
  expect((await call('GET', `/api/v1/music/${exported.music.releases[0]?.id}`)).status).toBe(404);
});
