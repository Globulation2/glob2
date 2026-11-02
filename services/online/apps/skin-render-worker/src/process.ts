import { readFile, stat, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { sql, type Kysely } from 'kysely';
import sharp from 'sharp';
import { putContent, sha256Hex, type BlobStore, type Logger } from '@glob2/core';
import type { Database } from '@glob2/db';
import { AgentBlobs } from '@glob2/engine/blobs';
import { runProcess, withScratchDir, outputTail } from '@glob2/engine/process';
import { SWARM_MESHES } from '@glob2/protocol';
import { validateBundle } from './bundle.ts';
export interface Renderer {
  binary: string;
  cwd: string;
  revision: string;
  signal?: AbortSignal;
  attempt?: number;
}
async function readBounded(path: string, limit: number) {
  const info = await stat(path);
  if (!info.isFile() || info.size > limit) throw new Error('Invalid renderer output file');
  return readFile(path);
}
export async function renderSkin(
  db: Kysely<Database>,
  blobs: BlobStore,
  renderer: Renderer,
  id: string,
  logger: Logger,
  finalAttempt = false,
) {
  if (!/^[0-9a-f-]{36}$/.test(id)) throw new Error('Invalid sprite job');
  const began = Date.now();
  await db.transaction().execute(async (lock) => {
    const acquired = await sql<{
      acquired: boolean;
    }>`SELECT pg_try_advisory_xact_lock(hashtextextended(${`skin:${id}`},0)) AS acquired`.execute(
      lock,
    );
    if (!acquired.rows[0]?.acquired) throw new Error('Skin render already running');
    const row = await db
      .selectFrom('colony_skin_sprites as d')
      .innerJoin('colony_skin_versions as v', 'v.id', 'd.version_id')
      .innerJoin('colony_skins as s', 's.id', 'v.skin_id')
      .select([
        'd.status',
        'd.render_revision',
        'v.skin_id',
        'v.texture_sha256',
        'v.material_sha256',
        'v.manifest_sha256',
        'v.layout',
        'v.building_color',
        'v.swarm_mesh',
        'v.swarm_view_angle',
        's.disabled_at',
      ])
      .where('d.id', '=', id)
      .executeTakeFirst();
    if (!row || row.status !== 'pending') return;
    if (row.render_revision !== renderer.revision) throw new Error('Wrong renderer revision');
    try {
      if (row.disabled_at) throw new Error('Skin is disabled');
      if (!SWARM_MESHES.includes(row.swarm_mesh as (typeof SWARM_MESHES)[number]))
        throw new Error('Unsupported swarm');
      await withScratchDir(undefined, async (work) => {
        const input = new AgentBlobs(blobs, db);
        await writeFile(
          join(work, 'texture.png'),
          await input.read(row.texture_sha256, 1024 * 1024),
        );
        await writeFile(
          join(work, 'material.png'),
          await input.read(row.material_sha256, 256 * 1024),
        );
        await writeFile(
          join(work, 'input.json'),
          JSON.stringify({
            skinId: row.skin_id,
            textureSha256: row.texture_sha256,
            materialSha256: row.material_sha256,
            manifestSha256: row.manifest_sha256,
            layout: row.layout,
            buildingColor: row.building_color,
            swarmMesh: row.swarm_mesh,
            swarmViewAngle: row.swarm_view_angle,
          }),
        );
        const output = join(work, 'output');
        const result = await runProcess({
          binary: renderer.binary,
          cwd: renderer.cwd,
          args: [
            'assets',
            'render-skin',
            '--manifest',
            join(work, 'input.json'),
            '--texture',
            join(work, 'texture.png'),
            '--material',
            join(work, 'material.png'),
            '--output-dir',
            output,
          ],
          signal: renderer.signal,
          env: {
            ...(process.env['XAUTHORITY'] ? { XAUTHORITY: process.env['XAUTHORITY'] } : {}),
            SDL_VIDEODRIVER: 'x11',
            GLOB2_USER_DATA_DIR: join(work, 'profile'),
            SDL_AUDIODRIVER: 'dummy',
            LIBGL_ALWAYS_SOFTWARE: '1',
            LP_NUM_THREADS: '2',
          },
          limits: { timeoutMs: 300000, cpuSeconds: 240, memoryMb: 2048, fileSizeMb: 8 },
        });
        if (result.code !== 0 || result.timedOut)
          throw new Error(`Skin renderer failed: ${outputTail(result)}`);
        const manifest = await readBounded(join(output, 'manifest.json'), 65536),
          bundle = validateBundle(manifest, row, renderer.revision);
        const stored: Awaited<ReturnType<typeof putContent>>[] = [];
        for (const p of bundle.pages) {
          const bytes = await readBounded(join(output, p.sha256 + '.webp'), 2 * 1024 * 1024);
          if (bytes.length !== p.bytes || sha256Hex(bytes) !== p.sha256)
            throw new Error('Sprite page hash mismatch');
          const meta = await sharp(bytes, {
            limitInputPixels: 1024 * 1024,
            failOn: 'warning',
          }).metadata();
          if (
            meta.format !== 'webp' ||
            meta.width !== p.width ||
            meta.height !== p.height ||
            (meta.pages ?? 1) !== 1 ||
            (!bytes.includes(Buffer.from('VP8 ')) && !bytes.includes(Buffer.from('VP8L')))
          )
            throw new Error('Invalid sprite page image');
          // Decode fully: metadata alone does not detect truncated image payloads.
          await sharp(bytes, { limitInputPixels: 1024 * 1024, failOn: 'warning' })
            .raw()
            .toBuffer();
          stored.push(await putContent(blobs, bytes));
        }
        const storedManifest = await putContent(blobs, manifest);
        await db.transaction().execute(async (trx) => {
          for (const blob of [...stored, storedManifest])
            await trx
              .insertInto('blobs')
              .values({
                sha256: blob.sha256,
                size: blob.size,
                storage_key: blob.key,
                content_type: blob === storedManifest ? 'application/json' : 'image/webp',
                visibility: 'private',
              })
              .onConflict((oc) => oc.column('sha256').doNothing())
              .execute();
          for (const blob of stored)
            await trx
              .insertInto('colony_skin_sprite_pages')
              .values({ sprites_id: id, sha256: blob.sha256 })
              .onConflict((oc) => oc.columns(['sprites_id', 'sha256']).doNothing())
              .execute();
          await trx
            .updateTable('colony_skin_sprites')
            .set({ status: 'ready', manifest_sha256: storedManifest.sha256, error: null })
            .where('id', '=', id)
            .execute();
        });
        logger.info(
          {
            id,
            revision: renderer.revision,
            attempt: renderer.attempt ?? 1,
            ms: Date.now() - began,
            bytes: stored.reduce((sum, b) => sum + b.size, manifest.length),
          },
          'Skin sprites ready',
        );
      });
    } catch (error) {
      logger.warn(
        { id, attempt: renderer.attempt ?? 1, ms: Date.now() - began, finalAttempt, err: error },
        'Skin sprite generation failed',
      );
      if (finalAttempt || row.disabled_at) {
        await db
          .updateTable('colony_skin_sprites')
          .set({ status: 'failed', error: String(error).slice(0, 1500) })
          .where('id', '=', id)
          .execute();
        return;
      }
      throw error;
    }
  });
}
