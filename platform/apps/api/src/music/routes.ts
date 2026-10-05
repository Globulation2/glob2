import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import { contentKey } from '@glob2/core';
import {
  MusicMetadata,
  MusicConvert,
  MusicBatch,
  MusicReport,
  MusicModerate,
} from '@glob2/protocol';
import {
  MUSIC_CONVERT,
  MUSIC_INSPECT,
  MUSIC_UPLOAD_BYTES,
  releaseView,
  sourceKey,
  zipStream,
  type ReleaseRow,
} from '@glob2/music';
import { authenticate, requireAccount, requireRole, type Identity } from '../identity.ts';
import { apiError } from '../errors.ts';
import { body } from '../http/validate.ts';
import { enforce, SharedLimit } from '../http/rateLimits.ts';

export async function musicRoutes(app: FastifyInstance, identity: Identity): Promise<void> {
  const { db, blobs } = app.services;
  const creates = new SharedLimit(db, 'music-create', 6, 86400000);
  const uploads = new SharedLimit(db, 'music-upload', 24, 3600000);
  let receivingUpload = false;
  const reports = new SharedLimit(db, 'music-report', 10, 3600000);
  async function registered(request: FastifyRequest) {
    const caller = await requireAccount(identity, request);
    if (caller.account.kind !== 'registered')
      throw apiError('forbidden', 'Sign in with a registered account.');
    return caller.account;
  }
  async function visible(id: string, request: FastifyRequest, owner = false): Promise<ReleaseRow> {
    if (!/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(id))
      throw apiError('not_found', 'No such music release.');
    const account = (await authenticate(identity, request))?.account;
    const row = await db
      .selectFrom('music_releases')
      .selectAll()
      .where('id', '=', id)
      .executeTakeFirst();
    const owns = row && account?.id === row.owner_id;
    const moderator = account?.role === 'admin' || account?.role === 'moderator';
    if (
      !row ||
      (owner ? !owns : !(row.status === 'published' && !row.hidden) && !owns && !moderator)
    )
      throw apiError('not_found', 'No such music release.');
    return row;
  }
  async function view(row: ReleaseRow, request: FastifyRequest) {
    return releaseView(db, row, (await authenticate(identity, request))?.account.id);
  }
  type Id = { Params: { id: string } };
  app.get<{ Querystring: Record<string, string> }>('/api/v1/music', async (request) => {
    const q = request.query;
    const caller = await authenticate(identity, request);
    if (q['mine'] === '1' && !caller) throw apiError('unauthenticated', 'Sign in first.');
    const min = Number(q['min'] || 10),
      max = Number(q['max'] || 900);
    if (!Number.isFinite(min) || !Number.isFinite(max) || min < 0 || max > 900 || min > max)
      throw apiError('bad_request', 'Invalid duration filter.');
    const filters = [
      q['mine'] === '1'
        ? sql`m.owner_id = ${caller?.account.id}`
        : sql`m.status='published' AND NOT m.hidden`,
    ];
    if (q['q'])
      filters.push(
        sql`concat_ws(' ', m.metadata->>'title', m.metadata->>'artist', m.metadata->>'description', (m.metadata->'tags')::text) ILIKE ${'%' + q['q'].slice(0, 200) + '%'}`,
      );
    if (q['tag']) filters.push(sql`m.metadata->'tags' ? ${q['tag']}`);
    if (q['license']) filters.push(sql`m.metadata->>'license' = ${q['license']}`);
    if (q['ai'] === 'true' || q['ai'] === 'false')
      filters.push(sql`m.metadata->>'aiGenerated' = ${q['ai']}`);
    if (q['mine'] !== '1')
      filters.push(sql`(m.result->>'frames')::bigint BETWEEN ${min * 48000} AND ${max * 48000}`);
    const score =
      q['sort'] === 'likes'
        ? sql`(SELECT count(*)::integer FROM music_likes l WHERE l.release_id=m.id)`
        : q['sort'] === 'downloads'
          ? sql`m.downloads`
          : sql`0`;
    if (q['cursor']) {
      let c: { score: number; date: string; id: string };
      try {
        c = JSON.parse(Buffer.from(q['cursor'], 'base64url').toString()) as typeof c;
      } catch {
        throw apiError('bad_request', 'Invalid page cursor.');
      }
      if (
        !c ||
        typeof c !== 'object' ||
        !Number.isSafeInteger(c.score) ||
        !Number.isFinite(Date.parse(c.date)) ||
        !/^\d{4}-\d\d-\d\dT/.test(c.date) ||
        !/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(c.id)
      )
        throw apiError('bad_request', 'Invalid page cursor.');
      filters.push(
        sql`(${score}, m.created_at, m.id) < (${c.score}, ${c.date}::timestamptz, ${c.id}::uuid)`,
      );
    }
    const result = await sql<
      ReleaseRow & { score: number }
    >`SELECT m.*, ${score} AS score FROM music_releases m WHERE ${sql.join(filters, sql` AND `)} ORDER BY score DESC, m.created_at DESC, m.id DESC LIMIT 25`.execute(
      db,
    );
    const rows = result.rows.slice(0, 24),
      last = rows.at(-1);
    return {
      items: await Promise.all(rows.map((row) => releaseView(db, row, caller?.account.id))),
      next:
        result.rows.length > 24 && last
          ? Buffer.from(
              JSON.stringify({
                score: last.score,
                date: last.created_at.toISOString(),
                id: last.id,
              }),
            ).toString('base64url')
          : null,
    };
  });
  app.post('/api/v1/music', async (request, reply) => {
    const account = await registered(request);
    await enforce(creates, account.id, reply);
    const metadata = body(MusicMetadata, request.body);
    const row = await db.transaction().execute(async (trx) => {
      await sql`SELECT pg_advisory_xact_lock(hashtextextended(${account.id}, 1))`.execute(trx);
      const creator = await trx
        .selectFrom('accounts')
        .select('status')
        .where('id', '=', account.id)
        .forUpdate()
        .executeTakeFirst();
      if (!creator || creator.status === 'deleted')
        throw apiError('forbidden', 'This account was deleted.');
      const active = await trx
        .selectFrom('music_releases')
        .select('id')
        .where('owner_id', '=', account.id)
        .where('status', 'in', ['draft', 'inspecting', 'inspected', 'converting'])
        .execute();
      if (active.length >= 3)
        throw apiError(
          'bad_request',
          'Finish or cancel an existing upload first (maximum three active uploads).',
        );
      return await trx
        .insertInto('music_releases')
        .values({ id: randomUUID(), owner_id: account.id, metadata: JSON.stringify(metadata) })
        .returningAll()
        .executeTakeFirstOrThrow();
    });
    return reply.code(201).send(await view(row, request));
  });
  app.get<Id>('/api/v1/music/:id', async (request) =>
    view(await visible(request.params.id, request), request),
  );
  app.put<{ Params: { id: string; kind: string } }>(
    '/api/v1/music/:id/uploads/:kind',
    {
      bodyLimit: MUSIC_UPLOAD_BYTES,
      onRequest: async (request, reply) => {
        const account = await registered(request);
        await enforce(uploads, account.id, reply);
        const row = await visible(request.params.id, request, true);
        if (!['draft', 'inspected'].includes(row.status))
          throw apiError('bad_request', 'This release no longer accepts uploads.');
        if (!['calm', 'building', 'combat', 'cover'].includes(request.params.kind))
          throw apiError('bad_request', 'Unknown upload slot.');
        if (receivingUpload)
          throw apiError('unavailable', 'Another upload is in progress. Please retry shortly.');
        receivingUpload = true;
        let released = false;
        const release = () => {
          if (!released) {
            released = true;
            receivingUpload = false;
          }
        };
        reply.raw.once('finish', release);
        reply.raw.once('close', release);
      },
    },
    async (request) => {
      const { id, kind } = request.params;
      if (!['calm', 'building', 'combat', 'cover'].includes(kind))
        throw apiError('bad_request', 'Unknown upload slot.');
      await visible(id, request, true);
      if (
        !(request.body instanceof Buffer) ||
        !request.body.length ||
        (kind === 'cover' && request.body.length > 8 * 1024 * 1024)
      )
        throw apiError('bad_request', 'Upload an audio file, or cover art up to 8 MiB.');
      const key = sourceKey(id, kind, randomUUID());
      await blobs.put(key, request.body);
      let old: string | undefined;
      // Failed transactions leave their unreferenced upload for the sweeper.
      // A lost COMMIT acknowledgement may mean the key is already referenced.
      await db.transaction().execute(async (trx) => {
        const row = await trx
          .selectFrom('music_releases')
          .selectAll()
          .where('id', '=', id)
          .forUpdate()
          .executeTakeFirstOrThrow();
        if (!['draft', 'inspected'].includes(row.status))
          throw apiError('bad_request', 'This release no longer accepts uploads.');
        old = row.sources[kind];
        await trx
          .updateTable('music_releases')
          .set({
            sources: JSON.stringify({ ...row.sources, [kind]: key }),
            status: 'draft',
            inspection: null,
            updated_at: new Date(),
          })
          .where('id', '=', id)
          .execute();
      });
      // The new key is already committed. A failed obsolete-file cleanup must
      // never delete the current upload; the source sweeper also retries it.
      if (old)
        await blobs
          .delete(old)
          .catch((error: unknown) => app.log.warn({ err: error }, 'Music source cleanup failed'));
      return view(await visible(id, request, true), request);
    },
  );
  for (const action of ['inspect', 'convert'] as const)
    app.post<Id>(`/api/v1/music/:id/${action}`, async (request) => {
      const row = await visible(request.params.id, request, true);
      const options = action === 'convert' ? body(MusicConvert, request.body) : null;
      if (!['calm', 'building', 'combat'].every((m) => row.sources[m]))
        throw apiError('bad_request', 'Upload all three moods first.');
      const status = action === 'inspect' ? 'inspecting' : 'converting';
      const updated = await db.transaction().execute(async (trx) => {
        const changed = await trx
          .updateTable('music_releases')
          .set({
            status,
            options: options ? JSON.stringify(options) : null,
            error: null,
            updated_at: new Date(),
          })
          .where('id', '=', row.id)
          .where('status', '=', action === 'inspect' ? 'draft' : 'inspected')
          .returningAll()
          .executeTakeFirst();
        if (!changed) throw apiError('bad_request', 'The upload is not ready for this action.');
        // Commit the state transition and durable job together, including when
        // this API process exits before replying to the creator.
        await sql`SELECT graphile_worker.add_job(
          identifier => ${action === 'inspect' ? MUSIC_INSPECT : MUSIC_CONVERT}::text,
          payload => ${JSON.stringify({ id: row.id })}::json,
          job_key => ${`music-${row.id}-${action}`}::text,
          max_attempts => 3)`.execute(trx);
        return changed;
      });
      return view(updated, request);
    });
  app.post<Id>('/api/v1/music/:id/publish', async (request) => {
    await registered(request);
    const row = await visible(request.params.id, request, true);
    const updated = await db
      .updateTable('music_releases')
      .set({ status: 'published', updated_at: new Date() })
      .where('id', '=', row.id)
      .where('status', '=', 'ready')
      .where('hidden', '=', false)
      .returningAll()
      .executeTakeFirst();
    if (!updated)
      throw apiError('bad_request', 'Only a successfully converted release can be published.');
    return view(updated, request);
  });
  app.delete<Id>('/api/v1/music/:id', async (request) => {
    const row = await visible(request.params.id, request, true);
    await db
      .updateTable('music_releases')
      .set({ status: 'withdrawn', sources: '{}', updated_at: new Date() })
      .where('id', '=', row.id)
      .execute();
    for (const key of Object.values(row.sources)) await blobs.delete(key);
    return { ok: true };
  });
  for (const method of ['PUT', 'DELETE'] as const)
    app.route<Id>({
      method,
      url: '/api/v1/music/:id/like',
      handler: async (request) => {
        const account = await registered(request);
        const row = await visible(request.params.id, request);
        if (row.hidden || row.status !== 'published')
          throw apiError('not_found', 'No such music release.');
        if (method === 'PUT')
          await db
            .insertInto('music_likes')
            .values({ release_id: row.id, account_id: account.id })
            .onConflict((c) => c.doNothing())
            .execute();
        else
          await db
            .deleteFrom('music_likes')
            .where('release_id', '=', row.id)
            .where('account_id', '=', account.id)
            .execute();
        return view(row, request);
      },
    });
  app.post<Id>('/api/v1/music/:id/report', async (request, reply) => {
    const account = await registered(request);
    await enforce(reports, account.id, reply);
    const row = await visible(request.params.id, request);
    const input = body(MusicReport, request.body);
    await db
      .insertInto('music_reports')
      .values({ release_id: row.id, account_id: account.id, reason: input.reason })
      .execute();
    return { ok: true };
  });
  app.get('/api/v1/admin/music-status', async (request) => {
    await requireRole(identity, request, 'moderator');
    const states = await db
      .selectFrom('music_releases')
      .select([
        'status',
        sql<number>`count(*)::integer`.as('count'),
        sql<Date>`min(updated_at)`.as('oldest'),
      ])
      .groupBy('status')
      .execute();
    const assets = await db
      .selectFrom('music_assets')
      .innerJoin('blobs', 'blobs.sha256', 'music_assets.sha256')
      .select(sql<number>`coalesce(sum(blobs.size),0)::float8`.as('bytes'))
      .executeTakeFirstOrThrow();
    return { states, storedBytes: assets.bytes };
  });
  app.get('/api/v1/admin/music-reports', async (request) => {
    await requireRole(identity, request, 'moderator');
    return {
      items: await db
        .selectFrom('music_reports')
        .selectAll()
        .where('resolved', '=', false)
        .orderBy('created_at', 'asc')
        .limit(100)
        .execute(),
    };
  });
  app.put<Id>('/api/v1/admin/music/:id', async (request) => {
    const actor = await requireRole(identity, request, 'moderator');
    const input = body(MusicModerate, request.body);
    await db.transaction().execute(async (trx) => {
      await trx
        .updateTable('music_releases')
        .set({ hidden: input.hidden })
        .where('id', '=', request.params.id)
        .execute();
      await trx
        .updateTable('music_reports')
        .set({ resolved: true })
        .where('release_id', '=', request.params.id)
        .execute();
      await trx
        .insertInto('admin_audit_log')
        .values({
          actor_account_id: actor.account.id,
          action: 'music.moderate',
          target_type: 'music',
          target_id: request.params.id,
          details: JSON.stringify(input),
        })
        .execute();
    });
    return { ok: true };
  });
  async function downloadable(id: string, request: FastifyRequest) {
    const row = await visible(id, request);
    if (row.hidden || !['ready', 'published'].includes(row.status))
      throw apiError('not_found', 'Music is unavailable.');
    return row;
  }
  for (const suffix of ['tracks/:kind', 'cover', 'download'])
    app.get<{ Params: { id: string; kind?: string } }>(
      `/api/v1/music/:id/${suffix}`,
      async (request, reply) => {
        const row = await downloadable(request.params.id, request);
        const kind =
          suffix === 'cover'
            ? 'cover'
            : suffix === 'download'
              ? 'zip'
              : (request.params.kind ?? '');
        if (!['calm', 'building', 'combat', 'cover', 'zip'].includes(kind))
          throw apiError('not_found', 'No such track.');
        const asset = await db
          .selectFrom('music_assets')
          .selectAll()
          .where('release_id', '=', row.id)
          .where('kind', '=', kind)
          .executeTakeFirst();
        const stream = asset && (await blobs.get(contentKey(asset.sha256)));
        if (!asset || !stream) throw apiError('not_found', 'File unavailable.');
        // Always recheck release visibility before serving; private output must not enter a shared cache.
        reply.header('Cache-Control', 'private, no-store').header('ETag', `"${asset.sha256}"`);
        if (kind === 'zip') {
          await db
            .updateTable('music_releases')
            .set({ downloads: sql`downloads + 1` })
            .where('id', '=', row.id)
            .execute();
          reply.header('Content-Disposition', `attachment; filename="${row.id}.zip"`);
        }
        return reply
          .type(kind === 'zip' ? 'application/zip' : kind === 'cover' ? 'image/jpeg' : 'audio/ogg')
          .send(stream);
      },
    );
  app.post('/api/v1/music/download', async (request, reply) => {
    const { ids } = body(MusicBatch, request.body);
    const entries: { name: string; sha256: string }[] = [];
    for (const id of ids) {
      await downloadable(id, request);
      const assets = await db
        .selectFrom('music_assets')
        .selectAll()
        .where('release_id', '=', id)
        .where('kind', 'in', ['calm', 'building', 'combat'])
        .execute();
      if (assets.length !== 3) throw apiError('not_found', 'Incomplete music release.');
      for (const asset of assets)
        entries.push({
          name: `${id}/a${['calm', 'building', 'combat'].indexOf(asset.kind) + 1}.opus`,
          sha256: asset.sha256,
        });
    }
    const sizes = await db
      .selectFrom('blobs')
      .select('size')
      .where(
        'sha256',
        'in',
        entries.map((e) => e.sha256),
      )
      .execute();
    if (sizes.reduce((sum, asset) => sum + asset.size, 0) > 64 * 1024 * 1024 - 65536)
      throw apiError(
        'bad_request',
        'Select fewer sets: each ZIP is limited to 64 MiB so it can be imported on every game platform.',
      );
    await db
      .updateTable('music_releases')
      .set({ downloads: sql`downloads + 1` })
      .where('id', 'in', ids)
      .execute();
    return reply
      .type('application/zip')
      .header('Cache-Control', 'private, no-store')
      .header('Content-Disposition', 'attachment; filename="glob2-music.zip"')
      .send(
        zipStream(
          (async function* () {
            for (const entry of entries) {
              const stream = await blobs.get(contentKey(entry.sha256));
              if (!stream) throw new Error('Missing music asset');
              yield { name: entry.name, stream };
            }
          })(),
        ),
      );
  });
}
