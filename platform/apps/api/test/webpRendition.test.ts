import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import sharp from 'sharp';
import { FsBlobStore, putContent } from '@glob2/core';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { afterAll, beforeAll, expect, it } from 'vitest';
import { webpRendition } from '../src/http/webpRendition.ts';
import { skinManifestSha256, webpSkinVersion } from '../src/skins/manifest.ts';
let database: TestDatabase;
let directory: string;
let blobs: FsBlobStore;
beforeAll(async () => {
  database = await createTestDatabase();
  directory = await mkdtemp(join(tmpdir(), 'glob2-webp-renditions-'));
  blobs = new FsBlobStore(directory);
});
afterAll(async () => {
  await database?.drop();
  if (directory) await rm(directory, { recursive: true });
});
it('keeps old published sources immutable and deduplicates persistent lossless wire renditions', async () => {
  const pixels = Buffer.alloc(512 * 512 * 3);
  for (let i = 0; i < 512 * 512; ++i) pixels.fill(i % 4, i * 3, i * 3 + 3);
  const png = await sharp(pixels, { raw: { width: 512, height: 512, channels: 3 } })
    .png()
    .toBuffer();
  const source = await putContent(blobs, png);
  const db = database.db;
  await db
    .insertInto('blobs')
    .values({
      sha256: source.sha256,
      storage_key: source.key,
      size: source.size,
      content_type: 'image/png',
      visibility: 'private',
    })
    .execute();
  // Separate store objects model replicas; the third caller shares the local
  // in-flight request. Every caller must use the persisted winning hash.
  const [first, second, third] = await Promise.all([
    webpRendition(db, blobs, source.sha256),
    webpRendition(db, new FsBlobStore(directory), source.sha256),
    webpRendition(db, blobs, source.sha256),
  ]);
  expect(first).toBe(second);
  expect(first).toBe(third);
  expect(first).not.toBe(source.sha256);
  expect(await webpRendition(db, blobs, source.sha256)).toBe(first);
  const saved = await db
    .selectFrom('blobs')
    .selectAll()
    .where('sha256', '=', first)
    .executeTakeFirstOrThrow();
  expect(saved.content_type).toBe('image/webp');
  expect(saved.visibility).toBe('private');
  const stream = (await blobs.get(saved.storage_key))!;
  const chunks: Buffer[] = [];
  for await (const chunk of stream) chunks.push(Buffer.from(chunk));
  const webp = Buffer.concat(chunks);
  expect((await sharp(webp).metadata()).format).toBe('webp');
  expect(await sharp(webp).removeAlpha().raw().toBuffer()).toEqual(pixels);
  expect(
    (
      await db
        .selectFrom('blobs')
        .select('content_type')
        .where('sha256', '=', source.sha256)
        .executeTakeFirstOrThrow()
    ).content_type,
  ).toBe('image/png');
  expect((await db.selectFrom('image_webp_renditions').selectAll().execute()).length).toBe(1);
  const version = {
    id: '11111111-1111-4111-8111-111111111111',
    skinId: '22222222-2222-4222-8222-222222222222',
    textureSha256: source.sha256,
    materialSha256: source.sha256,
    manifestSha256: '',
    layout: 'colony-v2' as const,
    buildingColor: 0x123456,
    swarmMesh: 'classic' as const,
    swarmViewAngle: 0,
  };
  version.manifestSha256 = skinManifestSha256(version);
  const wire = await webpSkinVersion({ db, blobs }, version);
  expect(wire.id).toBe(version.id);
  expect(wire.textureSha256).toBe(first);
  expect(wire.manifestSha256).toBe(skinManifestSha256(wire));
  expect(version.textureSha256).toBe(source.sha256);
});
