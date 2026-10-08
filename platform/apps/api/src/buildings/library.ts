import { resolveReport } from '../admin/moderation.ts';
import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import { putContent, contentKey, submitEngineJob } from '@glob2/core';
import { insertBlob } from '@glob2/play';
import {
  PublishBuildingRequest,
  UpdateBuildingFamilyRequest,
  parseSimVersionKey,
  forkBuildingPackage,
  type BuildingFamily,
  type BuildingRelease,
  type EngineJobOutput,
} from '@glob2/protocol';
import {
  readBuildingArchive,
  writeBuildingArchive,
  writeBuildingArtworkBundle,
  canonicalBuildingJson,
  buildingAssetHash,
} from '@glob2/protocol/node';
import { authenticate, requireAccount, requireRole, type Identity } from '../identity.ts';
import { hasRole } from '../auth/admin.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { apiError } from '../errors.ts';
import { UUID } from '../maps/catalog.ts';

export async function buildingLibraryRoutes(app: FastifyInstance, identity: Identity) {
  const { db, blobs } = app.services;
  const publishes = new SharedLimit(db, 'building-publish', 20, 3600000),
    reports = new SharedLimit(db, 'building-report', 10, 3600000);
  const caller = async (r: FastifyRequest) => (await authenticate(identity, r))?.account;
  const signed = async (r: FastifyRequest) => {
    const a = (await requireAccount(identity, r)).account;
    if (a.kind !== 'registered' || a.status !== 'active')
      throw apiError('forbidden', 'Sign in with an active registered account.');
    return a;
  };
  async function audit(
    trx: typeof db,
    actorId: string,
    action: string,
    familyId: string,
    details: object,
  ) {
    await trx
      .insertInto('admin_audit_log')
      .values({
        actor_account_id: actorId,
        action,
        target_type: 'building',
        target_id: familyId,
        details: JSON.stringify(details),
      })
      .execute();
  }
  async function visible(id: string, a: Awaited<ReturnType<typeof caller>>) {
    const row = UUID.test(id)
      ? await db.selectFrom('building_families').selectAll().where('id', '=', id).executeTakeFirst()
      : undefined;
    if (
      !row ||
      (row.owner_account_id !== a?.id &&
        !(a && hasRole(a, 'moderator')) &&
        (row.hidden || row.visibility === 'private'))
    )
      throw apiError('not_found', 'No such building family.');
    return row;
  }
  async function view(
    row: Awaited<ReturnType<typeof visible>>,
    a: Awaited<ReturnType<typeof caller>>,
  ): Promise<BuildingFamily> {
    const owner = await db
      .selectFrom('accounts')
      .select(['id', 'display_name'])
      .where('id', '=', row.owner_account_id)
      .executeTakeFirstOrThrow();
    const versions = await db
      .selectFrom('building_releases as v')
      .innerJoin('engine_jobs as j', 'j.id', 'v.job_id')
      .selectAll('v')
      .select(['j.status', 'j.error'])
      // Native results contain an up-to-8 MiB snapshot. Project it out in SQL:
      // a catalogue page needs only the release verdict and content hashes.
      .select(sql<unknown>`j.result #- '{catalog,snapshot}'`.as('result'))
      .where('v.family_id', '=', row.id)
      .orderBy('v.created_at', 'desc')
      .orderBy('v.id', 'desc')
      .limit(50)
      .execute();
    const releases: BuildingRelease[] = versions.map((v) => {
      const report = v.result as EngineJobOutput<'validate-buildings'> | null;
      const valid =
        v.status === 'succeeded' &&
        report?.valid &&
        report.archiveHash === v.archive_hash &&
        report.baseHash === v.base_hash &&
        report.suite === v.suite;
      return {
        id: v.id,
        archiveHash: v.archive_hash,
        baseHash: v.base_hash,
        simVersion: v.sim_version,
        createdAt: v.created_at.toISOString(),
        status: valid
          ? 'valid'
          : v.status === 'succeeded'
            ? 'invalid'
            : v.status === 'failed'
              ? 'error'
              : 'pending',
        ...(valid
          ? {
              catalogHash: report.catalog.hash,
              ...(report.artworkHash ? { artworkHash: report.artworkHash } : {}),
            }
          : {
              error:
                report && !report.valid
                  ? report.reason
                  : ((v.error as { message?: string } | null)?.message ??
                    (v.status === 'succeeded'
                      ? 'Validation result does not match this release.'
                      : undefined)),
            }),
      };
    });
    const likes = await db
      .selectFrom('building_likes')
      .select(sql<string>`count(*)::bigint`.as('count'))
      .where('family_id', '=', row.id)
      .executeTakeFirstOrThrow();
    const liked = a
      ? await db
          .selectFrom('building_likes')
          .select('family_id')
          .where('family_id', '=', row.id)
          .where('account_id', '=', a.id)
          .executeTakeFirst()
      : undefined;
    const favourite = a
      ? await db
          .selectFrom('building_favourites')
          .select('family_id')
          .where('family_id', '=', row.id)
          .where('account_id', '=', a.id)
          .executeTakeFirst()
      : undefined;
    return {
      id: row.id,
      namespace: row.namespace,
      name: row.name,
      description: row.description,
      visibility: row.visibility,
      owner: { id: owner.id, displayName: owner.display_name },
      hidden: row.hidden,
      downloads: row.download_count,
      likes: Number(likes.count),
      liked: !!liked,
      favourite: !!favourite,
      releases,
      updatedAt: row.updated_at.toISOString(),
    };
  }
  app.get<{
    Querystring: {
      q?: string;
      sort?: string;
      owner?: string;
      favourites?: string;
      cursor?: string;
      limit?: string;
    };
  }>('/api/v1/buildings', async (r) => {
    const a = await caller(r),
      q = r.query,
      limit = Number(q.limit ?? 24);
    if (!Number.isInteger(limit) || limit < 1 || limit > 100)
      throw apiError('bad_request', 'Invalid page size.');
    if ((q.owner === 'me' || q.favourites === 'true') && !a)
      throw apiError('unauthenticated', 'Sign in to see your library.');
    const sort = q.sort ?? 'updated';
    if (!['newest', 'updated', 'likes', 'downloads'].includes(sort))
      throw apiError('bad_request', 'Unknown building sort.');
    const ranking =
      sort === 'likes'
        ? sql<string>`(SELECT count(*) FROM building_likes l WHERE l.family_id=b.id)`
        : sort === 'downloads'
          ? sql<string>`b.download_count`
          : sql<string>`extract(epoch from ${sql.ref(sort === 'newest' ? 'b.created_at' : 'b.updated_at')})`;
    let query = db.selectFrom('building_families as b').selectAll('b').select(ranking.as('rank'));
    if (q.owner === 'me' && a) query = query.where('b.owner_account_id', '=', a.id);
    else query = query.where('b.visibility', '=', 'public').where('b.hidden', '=', false);
    if (q.favourites === 'true' && a)
      query = query.where((eb) =>
        eb.exists(
          eb
            .selectFrom('building_favourites as f')
            .select('f.family_id')
            .whereRef('f.family_id', '=', 'b.id')
            .where('f.account_id', '=', a.id),
        ),
      );
    if (q.q)
      query = query.where(
        sql<boolean>`concat_ws(' ',b.name,b.description) ILIKE ${'%' + q.q.slice(0, 128).replace(/[\\%_]/g, '\\$&') + '%'}`,
      );
    if (q.cursor) {
      try {
        if (q.cursor.length > 512) throw Error();
        const cursor = JSON.parse(Buffer.from(q.cursor, 'base64url').toString()) as {
          sort: string;
          rank: string;
          id: string;
        };
        if (cursor.sort !== sort || !UUID.test(cursor.id) || !/^\d+(\.\d+)?$/.test(cursor.rank))
          throw Error();
        query = query.where(
          sql<boolean>`(${ranking},b.id)<(${cursor.rank}::numeric,${cursor.id}::uuid)`,
        );
      } catch {
        throw apiError('bad_request', 'Invalid cursor.');
      }
    }
    const rows = await query
      .orderBy('rank', 'desc')
      .orderBy('b.id', 'desc')
      .limit(limit + 1)
      .execute();
    const last = rows.slice(0, limit).at(-1);
    return {
      items: await Promise.all(rows.slice(0, limit).map((row) => view(row, a))),
      ...(rows.length > limit && last
        ? {
            nextCursor: Buffer.from(
              JSON.stringify({ sort, rank: String(last.rank), id: last.id }),
            ).toString('base64url'),
          }
        : {}),
    };
  });
  app.get<{ Params: { id: string } }>('/api/v1/buildings/:id', async (r) => {
    const a = await caller(r);
    return view(await visible(r.params.id, a), a);
  });
  app.patch<{ Params: { id: string } }>('/api/v1/buildings/:id', async (r) => {
    const a = await signed(r),
      input = body(UpdateBuildingFamilyRequest, r.body);
    if (input.name !== undefined && !input.name.trim())
      throw apiError('bad_request', 'Choose a family name.');
    const family = await db.transaction().execute(async (trx) => {
      // The same account lock fences publication and account deletion.
      const account = await trx
        .selectFrom('accounts')
        .select(['kind', 'status'])
        .where('id', '=', a.id)
        .forUpdate()
        .executeTakeFirstOrThrow();
      if (account.kind !== 'registered' || account.status !== 'active')
        throw apiError('forbidden', 'Account is unavailable.');
      const row = UUID.test(r.params.id)
        ? await trx
            .updateTable('building_families')
            .set({
              ...(input.name === undefined ? {} : { name: input.name.trim() }),
              ...(input.description === undefined ? {} : { description: input.description }),
              ...(input.visibility === undefined ? {} : { visibility: input.visibility }),
              updated_at: new Date(),
            })
            .where('id', '=', r.params.id)
            .where('owner_account_id', '=', a.id)
            .returningAll()
            .executeTakeFirst()
        : undefined;
      if (!row) throw apiError('not_found', 'No owned building family.');
      await audit(trx, a.id, 'building.update', row.id, input);
      return row;
    });
    return view(family, a);
  });
  app.delete<{ Params: { id: string } }>('/api/v1/buildings/:id', async (r, reply) => {
    const a = await signed(r);
    await db.transaction().execute(async (trx) => {
      const account = await trx
        .selectFrom('accounts')
        .select(['kind', 'status'])
        .where('id', '=', a.id)
        .forUpdate()
        .executeTakeFirstOrThrow();
      if (account.kind !== 'registered' || account.status !== 'active')
        throw apiError('forbidden', 'Account is unavailable.');
      const row = UUID.test(r.params.id)
        ? await trx
            .deleteFrom('building_families')
            .where('id', '=', r.params.id)
            .where('owner_account_id', '=', a.id)
            .returning('id')
            .executeTakeFirst()
        : undefined;
      if (!row) throw apiError('not_found', 'No owned building family.');
      // Maps/saves carry their own snapshots and artwork. Only catalogue
      // releases/social rows cascade; the account's authoring draft remains.
      await audit(trx, a.id, 'building.delete', row.id, {});
    });
    return reply.status(204).send();
  });
  app.post<{ Params: { id: string } }>('/api/v1/building-drafts/:id/publish', async (r, reply) => {
    const a = await signed(r),
      input = body(PublishBuildingRequest, r.body);
    await enforce(publishes, a.id, reply, 'Too many building releases.');
    const draft = UUID.test(r.params.id)
      ? await db
          .selectFrom('building_drafts')
          .selectAll()
          .where('id', '=', r.params.id)
          .where('owner_account_id', '=', a.id)
          .executeTakeFirst()
      : undefined;
    if (!draft) throw apiError('not_found', 'No such draft.');
    if (draft.revision !== input.revision)
      throw apiError('conflict', 'Reload the current saved draft before publishing.');
    const pkg = readBuildingArchive(draft.archive).package;
    const agent = await db
      .selectFrom('engine_agents')
      .select(['sim_version', 'building_catalog_hash'])
      .where(sql<boolean>`'validate-buildings'=ANY(kinds)`)
      .where(sql<boolean>`last_seen_at>now()-interval '2 minutes'`)
      .where('building_catalog_hash', 'is not', null)
      .orderBy('last_seen_at', 'desc')
      .executeTakeFirst();
    const sim = agent && parseSimVersionKey(agent.sim_version);
    if (!agent || !sim || !agent.building_catalog_hash)
      throw apiError('conflict', 'No building validation engine is available. Try again later.');
    const baseHash = agent.building_catalog_hash;
    const content = await putContent(blobs, draft.archive);
    const family = await db.transaction().execute(async (trx) => {
      const account = await trx
        .selectFrom('accounts')
        .select(['kind', 'status'])
        .where('id', '=', a.id)
        .forUpdate()
        .executeTakeFirstOrThrow();
      if (account.kind !== 'registered' || account.status !== 'active')
        throw apiError('forbidden', 'Account is unavailable.');
      const current = await trx
        .selectFrom('building_drafts')
        .select('revision')
        .where('id', '=', draft.id)
        .where('owner_account_id', '=', a.id)
        .forUpdate()
        .executeTakeFirst();
      if (current?.revision !== input.revision)
        throw apiError('conflict', 'The draft changed during publication. Reload it.');
      let row = await trx
        .selectFrom('building_families')
        .selectAll()
        .where('namespace', '=', pkg.namespace)
        .forUpdate()
        .executeTakeFirst();
      if (row && row.owner_account_id !== a.id)
        throw apiError(
          'conflict',
          'This namespace belongs to another author. Fork the family first.',
        );
      if (!row) {
        row = await trx
          .insertInto('building_families')
          .values({
            namespace: pkg.namespace,
            owner_account_id: a.id,
            name: draft.name,
            description: input.description,
            visibility: input.visibility,
          })
          .onConflict((oc) => oc.column('namespace').doNothing())
          .returningAll()
          .executeTakeFirst();
        // Different author locks do not serialize the namespace's first
        // publication. The uniqueness constraint picks the winning owner.
        if (!row)
          throw apiError(
            'conflict',
            'This namespace was claimed by another author. Fork it first.',
          );
      } else
        row = await trx
          .updateTable('building_families')
          .set({
            name: draft.name,
            description: input.description,
            visibility: input.visibility,
            updated_at: new Date(),
          })
          .where('id', '=', row.id)
          .returningAll()
          .executeTakeFirstOrThrow();
      const existing = await trx
        .selectFrom('building_releases')
        .select('id')
        .where('family_id', '=', row.id)
        .where('archive_hash', '=', content.sha256)
        .where('sim_version', '=', agent.sim_version)
        .where('base_hash', '=', baseHash)
        .where('suite', '=', 1)
        .executeTakeFirst();
      if (existing) {
        const release = await trx
          .selectFrom('building_releases as v')
          .innerJoin('engine_jobs as j', 'j.id', 'v.job_id')
          .select('j.status')
          .where('v.id', '=', existing.id)
          .executeTakeFirstOrThrow();
        if (release.status === 'failed') {
          const job = await submitEngineJob(trx, {
            kind: 'validate-buildings',
            simVersion: sim,
            payload: { blobHash: content.sha256, baseHash, suite: 1 },
          });
          await trx
            .updateTable('building_releases')
            .set({ job_id: job })
            .where('id', '=', existing.id)
            .execute();
        }
      }
      if (!existing) {
        const count = await trx
          .selectFrom('building_releases')
          .select('id')
          .where('family_id', '=', row.id)
          .limit(50)
          .execute();
        if (count.length >= 50) throw apiError('conflict', 'This family has 50 releases.');
        await insertBlob(trx, content.sha256, content.size, 'application/octet-stream', 'private');
        const job = await submitEngineJob(trx, {
          kind: 'validate-buildings',
          simVersion: sim,
          payload: { blobHash: content.sha256, baseHash: baseHash, suite: 1 },
        });
        await trx
          .insertInto('building_releases')
          .values({
            family_id: row.id,
            archive_hash: content.sha256,
            job_id: job,
            sim_version: agent.sim_version,
            base_hash: baseHash,
            suite: 1,
          })
          .execute();
      }
      return row;
    });
    return reply.status(202).send(await view(family, a));
  });
  async function release(
    familyId: string,
    releaseId: string,
    a: Awaited<ReturnType<typeof caller>>,
  ) {
    const family = await visible(familyId, a),
      detail = await view(family, a),
      version = detail.releases.find((v) => v.id === releaseId);
    if (family.hidden || !version || version.status !== 'valid')
      throw apiError('not_found', 'No validated building release.');
    const stream = await blobs.get(contentKey(version.archiveHash));
    if (!stream) throw apiError('not_found', 'Release archive is unavailable.');
    return { family, version, stream };
  }
  async function runtime(id: string, version: string, a: Awaited<ReturnType<typeof caller>>) {
    const releaseData = await release(id, version, a);
    const chunks: Buffer[] = [];
    let size = 0;
    for await (const chunk of releaseData.stream) {
      const bytes = Buffer.from(chunk as Uint8Array);
      size += bytes.length;
      if (size > 32 * 1024 * 1024) throw apiError('conflict', 'Release exceeds package limits.');
      chunks.push(bytes);
    }
    const bytes = Buffer.concat(chunks);
    if (buildingAssetHash(bytes) !== releaseData.version.archiveHash)
      throw apiError('conflict', 'Release archive failed its integrity check.');
    const archive = readBuildingArchive(bytes),
      artwork = writeBuildingArtworkBundle([archive.package], archive.assets);
    if (
      (artwork.length ? buildingAssetHash(artwork) : undefined) !== releaseData.version.artworkHash
    )
      throw apiError('conflict', 'Release artwork failed its integrity check.');
    return { ...releaseData, archive, artwork };
  }
  app.get<{ Params: { id: string; release: string } }>(
    '/api/v1/buildings/:id/releases/:release/thumbnail',
    async (r, reply) => {
      const data = await runtime(r.params.id, r.params.release, await caller(r));
      const hash = data.archive.package.sprites[0]?.frames[0]?.imageHash;
      const asset = hash && data.archive.assets.get(hash);
      if (!asset) throw apiError('not_found', 'This family uses stock artwork.');
      return reply
        .header('content-type', 'image/webp')
        .header('cache-control', 'private, no-store')
        .header('x-content-type-options', 'nosniff')
        .send(asset);
    },
  );
  app.get<{ Params: { id: string; release: string } }>(
    '/api/v1/buildings/:id/releases/:release/runtime',
    async (r, reply) => {
      const data = await runtime(r.params.id, r.params.release, await caller(r));
      await db
        .updateTable('building_families')
        .set({ download_count: sql<number>`least(download_count+1,2147483646)` })
        .where('id', '=', data.family.id)
        .execute();
      reply.header('cache-control', 'private, no-store');
      return {
        schemaVersion: 1,
        name: data.family.name,
        namespace: data.family.namespace,
        archiveHash: data.version.archiveHash,
        baseHash: data.version.baseHash,
        catalogHash: data.version.catalogHash,
        simVersion: data.version.simVersion,
        packageJson: canonicalBuildingJson(data.archive.package),
        packageHash: buildingAssetHash(Buffer.from(canonicalBuildingJson(data.archive.package))),
        ...(data.version.artworkHash ? { artworkHash: data.version.artworkHash } : {}),
      };
    },
  );
  app.get<{ Params: { id: string; release: string } }>(
    '/api/v1/buildings/:id/releases/:release/artwork',
    async (r, reply) => {
      const data = await runtime(r.params.id, r.params.release, await caller(r));
      return reply
        .header('content-type', 'application/octet-stream')
        .header('cache-control', 'private, no-store')
        .header('x-content-type-options', 'nosniff')
        .send(data.artwork);
    },
  );
  app.get<{ Params: { id: string; release: string } }>(
    '/api/v1/buildings/:id/releases/:release/archive',
    async (r, reply) => {
      const { family, version, stream } = await release(
        r.params.id,
        r.params.release,
        await caller(r),
      );
      await db
        .updateTable('building_families')
        .set({ download_count: sql<number>`least(download_count+1,2147483646)` })
        .where('id', '=', family.id)
        .execute();
      return reply
        .header('content-type', 'application/octet-stream')
        .header('content-disposition', 'attachment; filename="building-family.zip"')
        .header('x-content-type-options', 'nosniff')
        .header('cache-control', 'private, no-store')
        .header('x-content-sha256', version.archiveHash)
        .send(stream);
    },
  );
  app.post<{ Params: { id: string; release: string } }>(
    '/api/v1/buildings/:id/releases/:release/fork',
    async (r, reply) => {
      const a = await signed(r);
      await enforce(publishes, a.id, reply, 'Too many building forks.');
      const { family, stream } = await release(r.params.id, r.params.release, a);
      const parts: Buffer[] = [];
      let size = 0;
      for await (const part of stream) {
        const b = Buffer.from(part as Uint8Array);
        size += b.length;
        if (size > 32 * 1024 * 1024) throw apiError('conflict', 'Release archive is too large.');
        parts.push(b);
      }
      const original = readBuildingArchive(Buffer.concat(parts));
      const pkg = forkBuildingPackage(original.package, randomUUID());
      const archive = writeBuildingArchive(pkg, original.assets);
      const draft = await db.transaction().execute(async (trx) => {
        const account = await trx
          .selectFrom('accounts')
          .select('status')
          .where('id', '=', a.id)
          .forUpdate()
          .executeTakeFirstOrThrow();
        if (account.status !== 'active') throw apiError('forbidden', 'Account is unavailable.');
        const usage = await trx
          .selectFrom('building_drafts')
          .select(sql<string>`coalesce(sum(octet_length(archive)),0)::bigint`.as('bytes'))
          .select(sql<string>`count(*)::bigint`.as('count'))
          .where('owner_account_id', '=', a.id)
          .executeTakeFirstOrThrow();
        if (Number(usage.count) >= 100 || Number(usage.bytes) + archive.length > 64 * 1024 * 1024)
          throw apiError('conflict', 'Your building workspace is full.');
        return trx
          .insertInto('building_drafts')
          .values({
            owner_account_id: a.id,
            name: ('Fork of ' + family.name).slice(0, 128),
            archive,
          })
          .returning(['id'])
          .executeTakeFirstOrThrow();
      });
      return reply.status(201).send(draft);
    },
  );
  for (const [action, table] of [
    ['like', 'building_likes'],
    ['favourite', 'building_favourites'],
  ] as const) {
    app.put<{ Params: { id: string } }>('/api/v1/buildings/:id/' + action, async (r, reply) => {
      const a = await signed(r),
        family = await visible(r.params.id, a);
      if (family.hidden) throw apiError('not_found', 'No such building family.');
      await db
        .insertInto(table)
        .values({ family_id: family.id, account_id: a.id })
        .onConflict((oc) => oc.columns(['family_id', 'account_id']).doNothing())
        .execute();
      return reply.status(204).send();
    });
    app.delete<{ Params: { id: string } }>('/api/v1/buildings/:id/' + action, async (r, reply) => {
      const a = await signed(r),
        family = await visible(r.params.id, a);
      await db
        .deleteFrom(table)
        .where('family_id', '=', family.id)
        .where('account_id', '=', a.id)
        .execute();
      return reply.status(204).send();
    });
  }
  app.post<{ Params: { id: string } }>('/api/v1/buildings/:id/reports', async (r, reply) => {
    const a = await signed(r),
      family = await visible(r.params.id, a);
    await enforce(reports, a.id, reply, 'Too many reports.');
    const reason = (r.body as { reason?: unknown })?.reason;
    if (typeof reason !== 'string' || !reason.trim() || reason.length > 2000)
      throw apiError('bad_request', 'Give a report reason of at most 2000 characters.');
    await db
      .insertInto('building_reports')
      .values({ family_id: family.id, reporter_account_id: a.id, reason: reason.trim() })
      .execute();
    return reply.status(201).send({ ok: true });
  });
  app.get('/api/v1/building-reports', async (r) => {
    await requireRole(identity, r, 'moderator');
    const items = await db
      .selectFrom('building_reports as r')
      .innerJoin('building_families as f', 'f.id', 'r.family_id')
      .selectAll('r')
      .select('f.name')
      .where('r.resolved', '=', false)
      .orderBy('r.created_at', 'desc')
      .limit(100)
      .execute();
    return { items: items.map((row) => ({ ...row, created_at: row.created_at.toISOString() })) };
  });
  app.put<{ Params: { id: string } }>('/api/v1/building-reports/:id', async (r, reply) => {
    const actor = (await requireRole(identity, r, 'moderator')).account;
    if (!UUID.test(r.params.id) || (r.body as { resolved?: unknown })?.resolved !== true)
      throw apiError('bad_request', 'Choose an existing report to resolve.');
    await resolveReport(
      db,
      'buildings',
      r.params.id,
      { resolution: 'resolved', reason: 'Reviewed through building moderation' },
      actor.id,
      { action: 'building.report.resolve', targetType: 'building', reportTarget: false },
    );
    return reply.status(204).send();
  });
  app.put<{ Params: { id: string } }>('/api/v1/buildings/:id/moderation', async (r, reply) => {
    const actor = (await requireRole(identity, r, 'moderator')).account;
    const input = r.body as { hidden?: unknown; reason?: unknown };
    if (
      typeof input?.hidden !== 'boolean' ||
      typeof input.reason !== 'string' ||
      input.reason.length > 2000 ||
      (input.hidden && !input.reason.trim())
    )
      throw apiError('bad_request', 'Choose visibility and a moderation reason.');
    if (!UUID.test(r.params.id)) throw apiError('not_found', 'No such building family.');
    const hidden = input.hidden;
    const reason = input.reason.trim();
    await db.transaction().execute(async (trx) => {
      const row = await trx
        .updateTable('building_families')
        .set({ hidden, hidden_reason: hidden ? reason : null })
        .where('id', '=', r.params.id)
        .returning('id')
        .executeTakeFirst();
      if (!row) throw apiError('not_found', 'No such building family.');
      await audit(trx, actor.id, hidden ? 'building.hide' : 'building.unhide', row.id, { reason });
    });
    return reply.status(204).send();
  });
}
