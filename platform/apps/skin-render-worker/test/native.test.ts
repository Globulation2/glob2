import { it, expect } from 'vitest';
import { readFile, mkdtemp, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createTestDatabase } from '@glob2/db/testing';
import {
  createLogger,
  FsBlobStore,
  putContent,
  prepareJobQueue,
  enqueueSkinSprites,
} from '@glob2/core';
import { runProcess } from '@glob2/engine/process';
import { renderSkin } from '../src/process.ts';

// Opt-in integration: run Vitest under Xvfb with the built native client.
it.skipIf(!process.env['GLOB2_SKIN_RENDER_TEST_BINARY'])(
  'the deployed renderer adapter bakes and atomically stores a real skin',
  async () => {
    const binary = process.env['GLOB2_SKIN_RENDER_TEST_BINARY']!;
    const cwd = fileURLToPath(new URL('../../../../', import.meta.url));
    const probe = await runProcess({
      binary,
      cwd,
      args: ['--skin-render-info'],
      limits: { timeoutMs: 10000 },
    });
    expect(probe.code).toBe(0);
    const recipe = JSON.parse(await readFile(join(cwd, 'tools/image_encoding.json'), 'utf8')) as {
      recipe: string;
      webpVersion: string;
    };
    const capability = JSON.parse(probe.stdout) as { renderRevision: string };
    expect(capability).toMatchObject({
      format: 'colony-sprites-v1',
      encoding: recipe.recipe,
      webpVersion: recipe.webpVersion,
    });
    const revision = capability.renderRevision;
    const fixture = JSON.parse(
      await readFile(join(cwd, 'test/fixtures/skins/authorization.json'), 'utf8'),
    ) as {
      textureHex: string;
      materialHex: string;
      claims: {
        version: { id: string; skinId: string; manifestSha256: string; buildingColor: number };
      };
    };
    const database = await createTestDatabase({ role: 'worker' }),
      work = await mkdtemp(join(tmpdir(), 'skin-native-'));
    const logger = createLogger('skin-native', 'silent'),
      blobs = new FsBlobStore(work),
      db = database.db;
    try {
      await prepareJobQueue(database.pool, logger);
      await db.insertInto('skin_render_revisions').values({ revision }).execute();
      const hashes: string[] = [];
      for (const hex of [fixture.textureHex, fixture.materialHex]) {
        const stored = await putContent(blobs, Buffer.from(hex, 'hex'));
        hashes.push(stored.sha256);
        await db
          .insertInto('blobs')
          .values({
            sha256: stored.sha256,
            size: stored.size,
            storage_key: stored.key,
            content_type: 'image/png',
          })
          .execute();
      }
      const version = fixture.claims.version;
      await db
        .insertInto('colony_skins')
        .values({ id: version.skinId, kind: 'preset', name: 'Native', entitlement: 'skins:test' })
        .execute();
      await db
        .insertInto('colony_skin_versions')
        .values({
          id: version.id,
          skin_id: version.skinId,
          texture_sha256: hashes[0]!,
          material_sha256: hashes[1]!,
          manifest_sha256: version.manifestSha256,
          building_color: version.buildingColor,
          layout: 'colony-v2',
          swarm_mesh: 'classic',
        })
        .execute();
      await enqueueSkinSprites(db, version.id, revision);
      const derivative = await db
        .selectFrom('colony_skin_sprites')
        .selectAll()
        .executeTakeFirstOrThrow();
      const began = Date.now();
      await renderSkin(db, blobs, { binary, cwd, revision }, derivative.id, logger);
      const ready = await db
        .selectFrom('colony_skin_sprites')
        .selectAll()
        .executeTakeFirstOrThrow();
      expect(ready.status).toBe('ready');
      expect(ready.manifest_sha256).toMatch(/^[0-9a-f]{64}$/);
      const pages = await db
        .selectFrom('colony_skin_sprite_pages')
        .innerJoin('blobs', 'blobs.sha256', 'colony_skin_sprite_pages.sha256')
        .select(['blobs.size', 'blobs.content_type'])
        .execute();
      expect(pages).toHaveLength(29);
      expect(pages.every((p) => p.content_type === 'image/webp')).toBe(true);
      console.log(
        JSON.stringify({
          revision,
          pages: pages.length,
          bytes: pages.reduce((sum, p) => sum + Number(p.size), 0),
          ms: Date.now() - began,
        }),
      );
    } finally {
      await database.drop();
      await rm(work, { recursive: true, force: true });
    }
  },
  300000,
);

it.skipIf(!process.env['GLOB2_SKIN_RENDER_TEST_BINARY'])(
  'CLI validation and partial-output cleanup preserve existing directories',
  async () => {
    const binary = process.env['GLOB2_SKIN_RENDER_TEST_BINARY']!;
    const cwd = fileURLToPath(new URL('../../../../', import.meta.url));
    const fixture = JSON.parse(
      await readFile(join(cwd, 'test/fixtures/skins/authorization.json'), 'utf8'),
    ) as {
      textureHex: string;
      materialHex: string;
      claims: { version: Record<string, unknown> };
    };
    const { writeFile, mkdir, access } = await import('node:fs/promises');
    const { sha256Hex } = await import('@glob2/core');
    const sharp = (await import('sharp')).default;
    const work = await mkdtemp(join(tmpdir(), 'skin-cli-'));
    const env = {
      SDL_VIDEODRIVER: 'x11',
      GLOB2_USER_DATA_DIR: join(work, 'profile'),
      LIBGL_ALWAYS_SOFTWARE: '1',
      LP_NUM_THREADS: '2',
      ...(process.env['XAUTHORITY'] ? { XAUTHORITY: process.env['XAUTHORITY'] } : {}),
    };
    let texture: Buffer = Buffer.from(fixture.textureHex, 'hex'),
      material: Buffer = Buffer.from(fixture.materialHex, 'hex');
    const args = [
      '--render-skin',
      '--manifest',
      join(work, 'input.json'),
      '--texture',
      join(work, 'texture.png'),
      '--material',
      join(work, 'material.png'),
      '--output-dir',
      join(work, 'output'),
    ];
    const save = async () => {
      const v = fixture.claims.version;
      const source = {
        skinId: v['skinId'],
        textureSha256: sha256Hex(texture),
        materialSha256: sha256Hex(material),
        layout: 'colony-v2',
        buildingColor: v['buildingColor'],
      };
      await writeFile(
        join(work, 'input.json'),
        JSON.stringify({
          ...source,
          manifestSha256: sha256Hex(Buffer.from(JSON.stringify(source))),
        }),
      );
      await writeFile(join(work, 'texture.png'), texture);
      await writeFile(join(work, 'material.png'), material);
    };
    const command = (argv: string[] = args, working = cwd, extra = {}) =>
      runProcess({
        binary,
        cwd: working,
        args: argv,
        env: { ...env, ...extra },
        limits: { timeoutMs: 15000 },
      });
    try {
      expect((await command(['--render-skin'])).code).toBe(1);
      await save();
      expect((await command([...args, '--manifest', join(work, 'input.json')])).code).toBe(1);
      texture = Buffer.from(texture);
      texture.writeUInt32BE(513, 16);
      await save();
      expect((await command()).stderr).toContain('512x512');
      texture = Buffer.from(fixture.textureHex, 'hex');
      material = await sharp({
        create: {
          width: 512,
          height: 512,
          channels: 4,
          background: { r: 1, g: 1, b: 1, alpha: 0.5 },
        },
      })
        .png()
        .toBuffer();
      await save();
      expect((await command()).stderr).toContain('invalid material');
      material = Buffer.from(fixture.materialHex, 'hex');
      await save();
      expect((await command(args, cwd, { SDL_VIDEODRIVER: 'dummy' })).code).not.toBe(0);
      await expect(access(join(work, 'output.partial'))).rejects.toThrow();
      // Correct inputs, but no installed meshes: staging must be cleaned on failure.
      const missing = await command(args, work);
      expect(missing.code).toBe(1);
      expect(missing.stderr).toContain('worker-walk.gsk');
      await expect(access(join(work, 'output.partial'))).rejects.toThrow();
      await mkdir(join(work, 'output.partial'));
      await writeFile(join(work, 'output.partial', 'sentinel'), 'preserve');
      expect((await command()).code).toBe(1);
      expect(await readFile(join(work, 'output.partial', 'sentinel'), 'utf8')).toBe('preserve');
      await mkdir(join(work, 'output'));
      await writeFile(join(work, 'output', 'sentinel'), 'preserve');
      expect((await command()).code).toBe(1);
      expect(await readFile(join(work, 'output', 'sentinel'), 'utf8')).toBe('preserve');
    } finally {
      await rm(work, { recursive: true, force: true });
    }
  },
  60000,
);
