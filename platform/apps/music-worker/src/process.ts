import { spawn } from 'node:child_process';
import { createWriteStream } from 'node:fs';
import { mkdtemp, readdir, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { pipeline } from 'node:stream/promises';
import { sql, type Kysely } from 'kysely';
import { putContent, type BlobStore } from '@glob2/core';
import type { Database } from '@glob2/db';
import type { ConversionResult } from '@glob2/music';

class TerminalMusicError extends Error {}

const root = resolve(fileURLToPath(new URL('../../../..', import.meta.url)));
async function python(
  request: string,
  output: string,
  inspect: boolean,
  signal: AbortSignal,
): Promise<void> {
  await new Promise<void>((accept, reject) => {
    // A private process group lets timeout/shutdown kill FFmpeg children as well.
    const child = spawn(
      process.env['MUSIC_PYTHON'] ?? 'python3',
      ['-m', 'glob2music.community', request, output, ...(inspect ? ['--inspect'] : [])],
      {
        detached: true,
        env: {
          ...process.env,
          PYTHONPATH: join(root, 'tools/music'),
          TMPDIR: dirname(request),
          OPENBLAS_NUM_THREADS: '1',
          OMP_NUM_THREADS: '1',
        },
        stdio: ['ignore', 'ignore', 'pipe'],
      },
    );
    const cancel = () => {
      if (child.pid)
        try {
          process.kill(-child.pid, 'SIGKILL');
        } catch {
          /* exited */
        }
    };
    signal.addEventListener('abort', cancel, { once: true });
    if (signal.aborted) cancel();
    let error = '';
    child.stderr.on('data', (chunk: Buffer) => {
      error = (error + chunk.toString()).slice(-4000);
    });
    let timedOut = false;
    const timer = setTimeout(() => {
      timedOut = true;
      if (child.pid)
        try {
          process.kill(-child.pid, 'SIGKILL');
        } catch {
          /* already stopped */
        }
    }, 30 * 60_000);
    child.on('error', (e) => {
      clearTimeout(timer);
      signal.removeEventListener('abort', cancel);
      reject(e);
    });
    child.on('exit', (code, exitSignal) => {
      clearTimeout(timer);
      signal.removeEventListener('abort', cancel);
      if (code === 0) accept();
      else if (signal.aborted) reject(signal.reason);
      else if (timedOut)
        reject(new TerminalMusicError('Music processing exceeded its resource limit.'));
      // A decoder exit can mean corrupt input or an infrastructure failure
      // such as ENOSPC/missing libraries. Preserve inputs until retries exhaust.
      else
        reject(
          new Error(
            error || (exitSignal ? 'Music processor was interrupted.' : 'Music processing failed.'),
          ),
        );
    });
  });
}

export async function processMusic(
  db: Kysely<Database>,
  blobs: BlobStore,
  origin: string,
  id: string,
  inspect: boolean,
  finalAttempt = false,
): Promise<void> {
  if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(id))
    throw new Error('Invalid music job');
  // Transaction advisory lock prevents concurrent duplicate attempts without
  // locking the release row while a creator cancels or a moderator hides it.
  await db.transaction().execute(async (lock) => {
    const acquired = await sql<{
      acquired: boolean;
    }>`SELECT pg_try_advisory_xact_lock(hashtextextended(${id}, 0)) AS acquired`.execute(lock);
    if (!acquired.rows[0]?.acquired) throw new Error('Music job already running');
    const expected = inspect ? 'inspecting' : 'converting';
    const row = await db
      .selectFrom('music_releases')
      .selectAll()
      .where('id', '=', id)
      .executeTakeFirst();
    if (!row || row.status !== expected) return;
    const prefix = `glob2-music-${id}-`;
    let work: string | undefined;
    const cancellation = new AbortController();
    const check = setInterval(() => {
      void db
        .selectFrom('music_releases')
        .select('status')
        .where('id', '=', id)
        .executeTakeFirst()
        .then((active) => {
          if (active?.status !== expected)
            cancellation.abort(new TerminalMusicError('Music processing was cancelled.'));
        })
        .catch((error: unknown) => cancellation.abort(error));
    }, 1000);
    // Preserve retry inputs unless a terminal state is durably recorded.
    let retainSources = true;
    try {
      // An interrupted attempt may have left PCM behind. The release advisory
      // lock proves no other live attempt for this release owns these directories.
      for (const name of await readdir(tmpdir()))
        if (name.startsWith(prefix))
          await rm(join(tmpdir(), name), { recursive: true, force: true });
      work = await mkdtemp(join(tmpdir(), prefix));
      const sources: Record<string, string> = {};
      let cover: string | undefined;
      for (const [kind, key] of Object.entries(row.sources)) {
        if (
          !['calm', 'building', 'combat', 'cover'].includes(kind) ||
          !key.startsWith(`music-uploads/${id}/`)
        )
          throw new TerminalMusicError('Invalid source reference');
        const stream = await blobs.get(key);
        if (!stream) throw new TerminalMusicError('Source upload expired. Upload the files again.');
        const path = join(work, kind);
        await pipeline(stream, createWriteStream(path, { flags: 'wx' }));
        if (kind === 'cover') cover = path;
        else sources[kind] = path;
      }
      const request = join(work, 'request.json');
      await writeFile(
        request,
        JSON.stringify({
          sources,
          cover,
          metadata: { ...row.metadata, id, origin },
          options: row.options ?? {},
        }),
      );
      const output = join(work, inspect ? 'inspection.json' : 'output');
      await python(request, output, inspect, cancellation.signal);
      if (inspect) {
        const inspection: unknown = JSON.parse(await readFile(output, 'utf8'));
        const changed = await db
          .updateTable('music_releases')
          .set({
            status: 'inspected',
            inspection: JSON.stringify(inspection),
            updated_at: new Date(),
          })
          .where('id', '=', id)
          .where('status', '=', expected)
          .executeTakeFirst();
        retainSources = changed.numUpdatedRows > 0n;
      } else {
        const result = JSON.parse(
          await readFile(join(output, 'result.json'), 'utf8'),
        ) as ConversionResult;
        await writeFile(
          join(output, 'waveforms.json'),
          JSON.stringify(result.tracks.map((t) => ({ mood: t.mood, waveform: t.waveform }))),
        );
        const files = [
          { kind: 'waveforms', file: 'waveforms.json', type: 'application/json' },
          ...result.tracks.map((t, i) => ({
            kind: t.mood,
            file: `a${i + 1}.opus`,
            type: 'audio/ogg',
          })),
          { kind: 'zip', file: 'set.zip', type: 'application/zip' },
          ...(cover ? [{ kind: 'cover', file: 'cover.jpg', type: 'image/jpeg' }] : []),
        ];
        const assets: {
          kind: string;
          file: string;
          type: string;
          sha256: string;
          size: number;
          key: string;
        }[] = [];
        for (const file of files)
          assets.push({
            ...file,
            ...(await putContent(blobs, await readFile(join(output, file.file)))),
          });
        await db.transaction().execute(async (trx) => {
          const active = await trx
            .selectFrom('music_releases')
            .select('status')
            .where('id', '=', id)
            .forUpdate()
            .executeTakeFirst();
          if (active?.status !== expected) return;
          for (const asset of assets) {
            await trx
              .insertInto('blobs')
              .values({
                sha256: asset.sha256,
                size: asset.size,
                storage_key: asset.key,
                content_type: asset.type,
                visibility: 'private',
                owner_account_id: row.owner_id,
              })
              .onConflict((c) => c.doNothing())
              .execute();
            await trx
              .insertInto('music_assets')
              .values({ release_id: id, kind: asset.kind, sha256: asset.sha256 })
              .onConflict((c) =>
                c.columns(['release_id', 'kind']).doUpdateSet({ sha256: asset.sha256 }),
              )
              .execute();
          }
          await trx
            .updateTable('music_releases')
            .set({
              result: JSON.stringify(result),
              status: 'ready',
              sources: '{}',
              updated_at: new Date(),
            })
            .where('id', '=', id)
            .execute();
        });
        retainSources = false;
      }
    } catch (error) {
      if (!(error instanceof TerminalMusicError) && !finalAttempt) throw error;
      await db
        .updateTable('music_releases')
        .set({
          status: 'failed',
          error: error instanceof Error ? error.message.slice(-2000) : 'Music processing failed.',
          sources: '{}',
          updated_at: new Date(),
        })
        .where('id', '=', id)
        .where('status', '=', expected)
        .execute();
      retainSources = false;
    } finally {
      clearInterval(check);
      if (work) await rm(work, { recursive: true, force: true });
      // Failed cleanup is retried by the source sweeper; it must not undo a
      // completed conversion or turn an inspection into a failed job.
      if (!retainSources)
        await Promise.allSettled(Object.values(row.sources).map((key) => blobs.delete(key)));
    }
  });
}
