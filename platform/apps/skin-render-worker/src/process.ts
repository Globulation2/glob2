import { readFile, stat, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { sql, type Kysely } from 'kysely';
import sharp from 'sharp';
import { putContent, sha256Hex, type BlobStore, type Logger } from '@glob2/core';
import type { Database } from '@glob2/db';
import { AgentBlobs } from '@glob2/engine/blobs';
import { runProcess, withScratchDir, outputTail } from '@glob2/engine/process';
import { SWARM_MESHES } from '@glob2/protocol';
const clips = [
  'worker-walk',
  'worker-swim',
  'worker-harvest',
  'warrior-walk',
  'warrior-swim',
  'warrior-fight',
  'explorer-fly',
  'swarm',
];
const sizes = [38, 38, 38, 40, 40, 40, 32, 96];
function validFrameMapping(value: unknown) {
  if (!value || typeof value !== 'object') return false;
  const mapping = value as Record<string, unknown>;
  return (
    mapping['directions'] === 8 &&
    mapping['phases'] === 32 &&
    mapping['phaseShift'] === 3 &&
    mapping['direction8Shift'] === 5
  );
}
export interface Page {
  clip: string;
  first: number;
  frames: number;
  width: number;
  height: number;
  sha256: string;
  bytes: number;
}
export interface Bundle {
  pages: Page[];
}
/** Validate native output before reading filenames or exposing any bytes. */
export function validateBundle(
  bytes: Buffer,
  source: {
    manifest_sha256: string;
    texture_sha256: string;
    material_sha256: string;
    swarm_mesh: string;
    swarm_view_angle: number;
  },
  revision: string,
): Bundle {
  if (bytes.length > 65536) throw new Error('Oversized skin manifest');
  const doc = JSON.parse(bytes.toString()) as Record<string, unknown>;
  if (
    doc['format'] !== 'colony-sprites-v1' ||
    doc['renderRevision'] !== revision ||
    doc['sourceManifestSha256'] !== source.manifest_sha256 ||
    doc['textureSha256'] !== source.texture_sha256 ||
    doc['materialSha256'] !== source.material_sha256 ||
    doc['swarmMesh'] !== source.swarm_mesh ||
    doc['swarmViewAngle'] !== source.swarm_view_angle ||
    !validFrameMapping(doc['frameMapping']) ||
    doc['tileSize'] !== 128 ||
    doc['padding'] !== 1.25 ||
    doc['encoding'] !== 'bundled-images-v2-smallest-webp' ||
    JSON.stringify(doc['logicalSizes']) !== JSON.stringify(sizes)
  )
    throw new Error('Skin output identity mismatch');
  const pages = doc['pages'];
  if (!Array.isArray(pages) || pages.length !== 29) throw new Error('Incomplete sprite bundle');
  pages.forEach((raw: unknown, index: number) => {
    if (!raw || typeof raw !== 'object') throw new Error('Invalid sprite page');
    const p = raw as Record<string, unknown>,
      clip = index < 28 ? Math.floor(index / 4) : 7,
      size = index < 28 ? 1024 : 128;
    if (
      p['clip'] !== clips[clip] ||
      p['first'] !== (index < 28 ? (index % 4) * 64 : 0) ||
      p['frames'] !== (index < 28 ? 64 : 1) ||
      p['width'] !== size ||
      p['height'] !== size ||
      typeof p['sha256'] !== 'string' ||
      !/^[0-9a-f]{64}$/.test(p['sha256']) ||
      typeof p['bytes'] !== 'number' ||
      !Number.isInteger(p['bytes']) ||
      p['bytes'] < 1 ||
      p['bytes'] > 2 * 1024 * 1024
    )
      throw new Error('Invalid sprite page');
  });
  return { pages: pages as Page[] };
}
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
            '--render-skin',
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
