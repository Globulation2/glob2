import { beforeAll, afterAll, expect, it } from 'vitest';
import { sql } from 'kysely';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import {
  createLogger,
  FsBlobStore,
  putContent,
  prepareJobQueue,
  enqueueSkinSprites,
  sha256Hex,
} from '@glob2/core';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { collectBlobs } from '../../worker/src/blobGc.ts';
import { renderSkin, validateBundle } from '../src/process.ts';
let database: TestDatabase, work: string, blobs: FsBlobStore;
const logger = createLogger('skin-test', 'silent'),
  revision = 'd'.repeat(64);
beforeAll(async () => {
  database = await createTestDatabase({ role: 'worker' });
  work = await mkdtemp(join(tmpdir(), 'skin-worker-test-'));
  blobs = new FsBlobStore(join(work, 'blobs'));
  await prepareJobQueue(database.pool, logger);
  await database.db.insertInto('skin_render_revisions').values({ revision }).execute();
});
afterAll(async () => {
  await database?.drop();
  if (work) await rm(work, { recursive: true, force: true });
});
async function input() {
  const db = database.db;
  const stored = await putContent(blobs, Buffer.from('source ' + Math.random()));
  await db
    .insertInto('blobs')
    .values({
      sha256: stored.sha256,
      size: stored.size,
      storage_key: stored.key,
      content_type: 'image/png',
    })
    .execute();
  const skin = await db
    .insertInto('colony_skins')
    .values({ kind: 'preset', name: 'Test', entitlement: 'skins:test' })
    .returning('id')
    .executeTakeFirstOrThrow();
  const version = await db
    .insertInto('colony_skin_versions')
    .values({
      skin_id: skin.id,
      texture_sha256: stored.sha256,
      material_sha256: stored.sha256,
      layout: 'colony-v2',
      building_color: 123,
      manifest_sha256: sha256Hex(Buffer.from(skin.id)),
      swarm_mesh: 'classic',
    })
    .returningAll()
    .executeTakeFirstOrThrow();
  return version;
}
it('publication enqueue is transactional and duplicate publication does not create another job', async () => {
  const version = await input(),
    db = database.db;
  await expect(
    db.transaction().execute(async (trx) => {
      await enqueueSkinSprites(trx, version.id);
      throw new Error('rollback');
    }),
  ).rejects.toThrow('rollback');
  expect(
    await db
      .selectFrom('colony_skin_sprites')
      .selectAll()
      .where('version_id', '=', version.id)
      .execute(),
  ).toEqual([]);
  await db.transaction().execute(async (trx) => {
    await enqueueSkinSprites(trx, version.id);
    await enqueueSkinSprites(trx, version.id);
  });
  const rows = await db
    .selectFrom('colony_skin_sprites')
    .selectAll()
    .where('version_id', '=', version.id)
    .execute();
  expect(rows).toHaveLength(1);
  const jobs = await sql<{
    count: string;
  }>`SELECT count(*)::text AS count FROM graphile_worker._private_jobs WHERE key=${`skin:render:${rows[0]?.id}`}`.execute(
    db,
  );
  expect(jobs.rows[0]?.count).toBe('1');
});
it('final failure marks the derivative failed while preserving published content', async () => {
  const version = await input(),
    db = database.db;
  await enqueueSkinSprites(db, version.id);
  const derivative = await db
    .selectFrom('colony_skin_sprites')
    .selectAll()
    .where('version_id', '=', version.id)
    .executeTakeFirstOrThrow();
  await renderSkin(
    db,
    blobs,
    { binary: '/bin/false', cwd: work, revision },
    derivative.id,
    logger,
    true,
  );
  const failed = await db
    .selectFrom('colony_skin_sprites')
    .selectAll()
    .where('id', '=', derivative.id)
    .executeTakeFirstOrThrow();
  expect(failed.status).toBe('failed');
  expect(failed.manifest_sha256).toBeNull();
  expect(
    await db
      .selectFrom('colony_skin_sprite_pages')
      .selectAll()
      .where('sprites_id', '=', derivative.id)
      .execute(),
  ).toEqual([]);
  expect(
    await db
      .selectFrom('colony_skin_versions')
      .selectAll()
      .where('id', '=', version.id)
      .executeTakeFirstOrThrow(),
  ).toEqual(version);
});
it('valid output publishes all references atomically and an already-ready retry is a no-op', async () => {
  const version = await input(),
    db = database.db;
  await enqueueSkinSprites(db, version.id);
  const derivative = await db
    .selectFrom('colony_skin_sprites')
    .selectAll()
    .where('version_id', '=', version.id)
    .executeTakeFirstOrThrow();
  const fake = join(work, 'renderer');
  // Resolve the actual workspace dependency rather than hardcoding node_modules layout.
  const { createRequire } = await import('node:module');
  const require = createRequire(import.meta.url);
  const module = require.resolve('sharp');
  await writeFile(
    fake,
    `#!/usr/bin/env node
const fs=require('node:fs');const path=require('node:path');const sharp=require(${JSON.stringify(module)});
(async()=>{
const args=process.argv.slice(2),source=JSON.parse(fs.readFileSync(args[args.indexOf('--manifest')+1]));
const out=args[args.indexOf('--output-dir')+1];fs.mkdirSync(out);
const clips=['worker-walk','worker-swim','worker-harvest','warrior-walk','warrior-swim','warrior-fight','explorer-fly','swarm'];
const doc={format:'colony-sprites-v1',renderRevision:${JSON.stringify(revision)},sourceManifestSha256:source.manifestSha256,textureSha256:source.textureSha256,materialSha256:source.materialSha256,swarmMesh:source.swarmMesh,swarmViewAngle:source.swarmViewAngle,frameMapping:{directions:8,phases:32,phaseShift:3,direction8Shift:5},tileSize:128,padding:1.25,logicalSizes:[38,38,38,40,40,40,32,96],encoding:'bundled-images-v2-smallest-webp',pages:[]};
const images={};for(const size of [1024,128]) images[size]=await sharp({create:{width:size,height:size,channels:4,background:{r:200,g:50,b:20,alpha:0.5}}}).webp({quality:90,effort:6}).toBuffer();
for(let i=0;i<29;i++){let size=i<28?1024:128;const b=images[size];const hash=require('node:crypto').createHash('sha256').update(b).digest('hex');fs.writeFileSync(path.join(out,hash+'.webp'),b);doc.pages.push({clip:clips[i<28?Math.floor(i/4):7],first:i<28?i%4*64:0,frames:i<28?64:1,width:size,height:size,sha256:hash,bytes:b.length});}
fs.writeFileSync(path.join(out,'manifest.json'),JSON.stringify(doc));
})().catch(e=>{console.error(e);process.exit(1)});
`,
    { mode: 0o755 },
  );
  await renderSkin(db, blobs, { binary: fake, cwd: work, revision }, derivative.id, logger);
  const ready = await db
    .selectFrom('colony_skin_sprites')
    .selectAll()
    .where('id', '=', derivative.id)
    .executeTakeFirstOrThrow();
  expect(ready.status).toBe('ready');
  expect(ready.manifest_sha256).toMatch(/^[a-f0-9]{64}$/);
  expect(
    await db
      .selectFrom('colony_skin_sprite_pages')
      .selectAll()
      .where('sprites_id', '=', derivative.id)
      .execute(),
  ).toHaveLength(2);
  await db
    .updateTable('blobs')
    .set({ created_at: new Date(0) })
    .execute();
  expect((await collectBlobs(db, blobs)).deletedBlobs).toBe(0);
  await renderSkin(db, blobs, { binary: '/bin/false', cwd: work, revision }, derivative.id, logger);
  await expect(
    db
      .updateTable('colony_skin_sprites')
      .set({ manifest_sha256: version.texture_sha256 })
      .where('id', '=', derivative.id)
      .execute(),
  ).rejects.toThrow('immutable');
  await expect(
    db.deleteFrom('colony_skin_sprite_pages').where('sprites_id', '=', derivative.id).execute(),
  ).rejects.toThrow('immutable');
});
it('native manifest validation rejects incomplete and mismatched derivatives', async () => {
  const source = await input();
  expect(() => validateBundle(Buffer.from('{}'), source, revision)).toThrow('identity mismatch');
  expect(() => validateBundle(Buffer.alloc(65537), source, revision)).toThrow('Oversized');
});

it('new publications choose a live renderer after an older deployment is restored', async () => {
  const db = database.db;
  await db
    .insertInto('skin_render_revisions')
    .values({ revision: 'f'.repeat(64), last_seen_at: new Date(0) })
    .execute();
  const version = await input();
  await enqueueSkinSprites(db, version.id);
  const row = await db
    .selectFrom('colony_skin_sprites')
    .selectAll()
    .where('version_id', '=', version.id)
    .executeTakeFirstOrThrow();
  expect(row.render_revision).toBe(revision);
});
