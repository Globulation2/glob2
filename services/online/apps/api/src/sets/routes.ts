import { resolveReport } from '../admin/moderation.ts';
import { createHash } from 'node:crypto';
import { sql, type Selectable } from 'kysely';
import type { Account, Database } from '@glob2/db';
import { contentKey, putContent, submitEngineJob } from '@glob2/core';
import { insertBlob } from '@glob2/play';
import {
  simVersionKey,
  SetPackage,
  SaveSetDraftRequest,
  PublishSetRequest,
  UpdateSetRequest,
  MapReportRequest,
  MapHideRequest,
  ResolveMapReportRequest,
  SET_PACKAGE_MAX_BYTES,
  SET_VALIDATION_SUITE,
  parseSimVersionKey,
  type SetDraft,
  type SetCredit,
  type SetInfo,
  type SetList,
} from '@glob2/protocol';
import { Type } from 'typebox';
import type { FastifyInstance, FastifyRequest, FastifyReply } from 'fastify';
import { authenticate, requireAccount, requireRole, type Identity } from '../identity.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { apiError } from '../errors.ts';
import { TerrainStudio } from '@glob2/terrain-studio';
import { HiveError } from '@glob2/billing';
import { UUID, canModerate } from '../maps/catalog.ts';

export async function setLibraryRoutes(app: FastifyInstance, identity: Identity) {
  const { db, blobs } = app.services;
  const changes = new SharedLimit(db, 'set-change', 120, 3600000);
  const checks = new SharedLimit(db, 'set-check', 20, 3600000);
  const reports = new SharedLimit(db, 'set-report', 10, 3600000);
  const caller = async (r: FastifyRequest) => (await authenticate(identity, r))?.account;
  const signed = async (r: FastifyRequest) => {
    const a = (await requireAccount(identity, r)).account;
    if (a.kind !== 'registered')
      throw apiError('forbidden', 'Register an account to create and share sets.');
    return a;
  };
  const moderator = (a?: Account) => canModerate(a ? { account: a } : undefined);
  const uuid = (value: string) => {
    if (!UUID.test(value)) throw apiError('not_found', 'Set not found.');
    return value;
  };
  // Ownership-only operations exclude moderators; moderation uses separate audited routes.
  async function visible(id: string, a?: Account, own = false) {
    const row = await db
      .selectFrom('asset_sets')
      .selectAll()
      .where('id', '=', uuid(id))
      .executeTakeFirst();
    if (
      !row ||
      (row.owner_account_id !== a?.id &&
        (own || (!moderator(a) && (row.hidden || row.visibility === 'private'))))
    )
      throw apiError('not_found', 'Set not found.');
    return row;
  }
  async function ownedDraft(id: string, a: Account) {
    const draft = await db
      .selectFrom('set_drafts')
      .selectAll()
      .where('id', '=', uuid(id))
      .executeTakeFirst();
    if (!draft) throw apiError('not_found', 'Draft not found.');
    await visible(draft.set_id, a, true);
    return draft;
  }
  function draftView(d: Selectable<Database['set_drafts']>): SetDraft {
    return {
      id: d.id,
      revision: d.revision,
      package: d.document,
      publishedVersionId: d.published_version_id,
      validation:
        d.status && d.hash && d.sim_version
          ? {
              status: d.status,
              hash: d.hash,
              simVersion: d.sim_version,
              suite: SET_VALIDATION_SUITE,
              report: d.report,
              error: d.error,
            }
          : null,
    };
  }
  async function info(
    row: Selectable<Database['asset_sets']>,
    a?: Account,
    summary = false,
  ): Promise<SetInfo> {
    const [owner, versions, likes, downloads, liked] = await Promise.all([
      db
        .selectFrom('accounts')
        .select(['id', 'display_name'])
        .where('id', '=', row.owner_account_id)
        .executeTakeFirstOrThrow(),
      db
        .selectFrom('set_versions')
        .select([
          'id',
          'hash',
          'label',
          'license',
          'sim_version',
          'min_version_minor',
          'report',
          'preview_hash',
          'created_at',
        ])
        .select([
          sql<SetCredit[]>`CASE WHEN ${summary} THEN '[]'::jsonb ELSE credits END`.as('credits'),
          sql<string>`CASE WHEN ${summary} THEN '' ELSE notes END`.as('notes'),
        ])
        .where('set_id', '=', row.id)
        .orderBy('created_at', 'desc')
        .orderBy('id', 'desc')
        .execute(),
      db
        .selectFrom('set_likes')
        .select(sql<number>`count(*)::int`.as('n'))
        .where('set_id', '=', row.id)
        .executeTakeFirstOrThrow(),
      db
        .selectFrom('set_downloads as d')
        .innerJoin('set_versions as v', 'v.id', 'd.version_id')
        .select(sql<number>`count(*)::int`.as('n'))
        .where('v.set_id', '=', row.id)
        .executeTakeFirstOrThrow(),
      a
        ? db
            .selectFrom('set_likes')
            .select('set_id')
            .where('set_id', '=', row.id)
            .where('account_id', '=', a.id)
            .executeTakeFirst()
        : undefined,
    ]);
    return {
      id: row.id,
      title: row.title,
      description: row.description,
      tags: row.tags,
      owner: { id: owner.id, displayName: owner.display_name },
      visibility: row.visibility,
      hidden: row.hidden,
      ...(row.hidden_reason && (moderator(a) || a?.id === row.owner_account_id)
        ? { hiddenReason: row.hidden_reason }
        : {}),
      likes: likes.n,
      liked: !!liked,
      downloads: downloads.n,
      createdAt: row.created_at.toISOString(),
      updatedAt: row.updated_at.toISOString(),
      versions: versions.map((v) => ({
        id: v.id,
        hash: v.hash,
        label: v.label,
        notes: v.notes,
        license: v.license,
        credits: v.credits,
        simVersion: v.sim_version,
        minVersionMinor: v.min_version_minor,
        terrainCount: v.report.terrainCount,
        resourceCount: v.report.resourceCount,
        createdAt: v.created_at.toISOString(),
      })),
    };
  }
  app.get<{
    Querystring: {
      q?: string;
      tags?: string;
      owner?: string;
      sort?: string;
      cursor?: string;
      limit?: string;
      kind?: string;
      license?: string;
    };
  }>('/api/v1/sets', async (r): Promise<SetList> => {
    const a = await caller(r),
      q = r.query;
    const limit = Number(q.limit ?? 24);
    if (!Number.isInteger(limit) || limit < 1 || limit > 100)
      throw apiError('bad_request', 'Invalid pagination.');
    let query = db
      .selectFrom('asset_sets as s')
      .innerJoin('accounts as a', 'a.id', 's.owner_account_id')
      .selectAll('s');
    if (q.owner === 'me') {
      if (!a) throw apiError('unauthenticated', 'Sign in to see your sets.');
      query = query.where('s.owner_account_id', '=', a.id);
    } else {
      query = query
        .where('s.visibility', '=', 'public')
        .where('s.hidden', '=', false)
        .where(sql<boolean>`EXISTS(SELECT 1 FROM set_versions v WHERE v.set_id=s.id)`);
      if (q.owner) query = query.where('s.owner_account_id', '=', uuid(q.owner));
    }
    if (q.q)
      query = query.where(
        sql<boolean>`concat_ws(' ',s.title,s.description,a.display_name) ILIKE ${'%' + q.q.slice(0, 128).replace(/[\\%_]/g, '\\$&') + '%'}`,
      );
    for (const tag of (q.tags ?? '').split(',').filter(Boolean).slice(0, 8))
      query = query.where(sql<boolean>`${tag}=ANY(s.tags)`);
    if (q.license) {
      if (!['CC0-1.0', 'CC-BY-4.0'].includes(q.license))
        throw apiError('bad_request', 'Unknown license.');
      query = query.where(
        sql<boolean>`EXISTS(SELECT 1 FROM set_versions v WHERE v.set_id=s.id AND v.id=(SELECT id FROM set_versions WHERE set_id=s.id ORDER BY created_at DESC,id DESC LIMIT 1) AND v.license=${q.license})`,
      );
    }
    if (q.kind) {
      if (!['terrain', 'resource', 'both'].includes(q.kind))
        throw apiError('bad_request', 'Unknown set kind.');
      if (q.kind !== 'resource')
        query = query.where(
          sql<boolean>`EXISTS(SELECT 1 FROM set_versions v WHERE v.set_id=s.id AND v.id=(SELECT id FROM set_versions WHERE set_id=s.id ORDER BY created_at DESC,id DESC LIMIT 1) AND (v.report->>'terrainCount')::int>0)`,
        );
      if (q.kind !== 'terrain')
        query = query.where(
          sql<boolean>`EXISTS(SELECT 1 FROM set_versions v WHERE v.set_id=s.id AND v.id=(SELECT id FROM set_versions WHERE set_id=s.id ORDER BY created_at DESC,id DESC LIMIT 1) AND (v.report->>'resourceCount')::int>0)`,
        );
    }
    const sort = q.sort ?? 'newest';
    const score =
      sort === 'likes'
        ? sql<number>`(SELECT count(*)::int FROM set_likes l WHERE l.set_id=s.id)`
        : sort === 'downloads'
          ? sql<number>`(SELECT count(*)::int FROM set_downloads d JOIN set_versions v ON v.id=d.version_id WHERE v.set_id=s.id)`
          : sql<number>`0`;
    if (q.cursor) {
      try {
        if (q.cursor.length > 1024) throw Error();
        const c = JSON.parse(Buffer.from(q.cursor, 'base64url').toString()) as {
          sort: string;
          id: string;
          created: string;
          updated: string;
          score: number;
        };
        if (
          c.sort !== sort ||
          !UUID.test(c.id) ||
          !Number.isSafeInteger(c.score) ||
          !Number.isFinite(Date.parse(c.created)) ||
          !Number.isFinite(Date.parse(c.updated))
        )
          throw Error();
        const created = new Date(c.created),
          updated = new Date(c.updated);
        if (sort === 'likes' || sort === 'downloads')
          query = query.where(
            sql<boolean>`(${score},s.created_at,s.id)<(${c.score},${created},${c.id}::uuid)`,
          );
        else if (sort === 'updated')
          query = query.where(
            sql<boolean>`(s.updated_at,s.created_at,s.id)<(${updated},${created},${c.id}::uuid)`,
          );
        else query = query.where(sql<boolean>`(s.created_at,s.id)<(${created},${c.id}::uuid)`);
      } catch {
        throw apiError('bad_request', 'Invalid pagination cursor.');
      }
    }
    if (sort === 'likes')
      query = query.orderBy(sql`(SELECT count(*) FROM set_likes l WHERE l.set_id=s.id)`, 'desc');
    else if (sort === 'downloads')
      query = query.orderBy(
        sql`(SELECT count(*) FROM set_downloads d JOIN set_versions v ON v.id=d.version_id WHERE v.set_id=s.id)`,
        'desc',
      );
    else if (sort === 'updated') query = query.orderBy('s.updated_at', 'desc');
    else if (sort !== 'newest') throw apiError('bad_request', 'Unknown sort.');
    const rows = await query
      .select(score.as('score'))
      .orderBy('s.created_at', 'desc')
      .orderBy('s.id', 'desc')
      .limit(limit + 1)
      .execute();
    const last = rows[limit - 1];
    return {
      items: await Promise.all(rows.slice(0, limit).map((row) => info(row, a, true))),
      ...(rows.length > limit && last
        ? {
            nextCursor: Buffer.from(
              JSON.stringify({
                sort,
                id: last.id,
                created: last.created_at.toISOString(),
                updated: last.updated_at.toISOString(),
                score: last.score,
              }),
            ).toString('base64url'),
          }
        : {}),
    };
  });
  app.get<{ Params: { id: string } }>('/api/v1/sets/:id', async (r) =>
    info(await visible(r.params.id, await caller(r)), await caller(r)),
  );
  app.post('/api/v1/set-drafts', { bodyLimit: SET_PACKAGE_MAX_BYTES }, async (r, reply) => {
    const a = await signed(r);
    await enforce(changes, a.id, reply, 'Too many set changes.');
    const pack = body(SetPackage, r.body);
    const d = await db.transaction().execute(async (trx) => {
      await trx
        .insertInto('asset_sets')
        .values({
          id: pack.setId,
          owner_account_id: a.id,
          title: pack.title,
          description: pack.description,
          tags: pack.tags,
          hidden_reason: null,
        })
        .onConflict((oc) => oc.column('id').doNothing())
        .execute();
      const parent = await trx
        .selectFrom('asset_sets')
        .selectAll()
        .where('id', '=', pack.setId)
        .forUpdate()
        .executeTakeFirstOrThrow();
      if (parent.owner_account_id !== a.id) throw apiError('not_found', 'Set not found.');
      const count = await trx
        .selectFrom('set_drafts')
        .select(sql<number>`count(*)::int`.as('n'))
        .where('set_id', '=', parent.id)
        .executeTakeFirstOrThrow();
      if (count.n >= 100)
        throw apiError(
          'conflict',
          'This set has reached its 100-draft limit. Delete unused drafts first.',
        );
      const draft = await trx
        .insertInto('set_drafts')
        .values({
          id: pack.versionId,
          set_id: pack.setId,
          document: JSON.stringify(pack),
          hash: null,
          validation_job_id: null,
          sim_version: null,
          report: null,
          status: null,
          error: null,
          published_version_id: null,
        })
        .onConflict((oc) => oc.column('id').doNothing())
        .returningAll()
        .executeTakeFirst();
      if (!draft) throw apiError('conflict', 'This draft already exists. Open it to continue.');
      return draft;
    });
    return reply.status(201).send(draftView(d));
  });
  app.get<{ Querystring: { setId?: string; limit?: string; cursor?: string } }>(
    '/api/v1/set-drafts',
    async (r) => {
      const a = await signed(r);
      const limit = Number(r.query.limit ?? 100);
      if (!Number.isInteger(limit) || limit < 1 || limit > 100)
        throw apiError('bad_request', 'Invalid pagination.');
      let query = db
        .selectFrom('set_drafts as d')
        .innerJoin('asset_sets as s', 's.id', 'd.set_id')
        .select(['d.id', 'd.updated_at', 'd.published_version_id'])
        .select(sql<string>`d.document->>'title'`.as('title'))
        // Keep PostgreSQL microseconds in cursors; JS Date would skip near-equal rows.
        .select(
          sql<string>`to_char(d.updated_at AT TIME ZONE 'UTC','YYYY-MM-DD"T"HH24:MI:SS.US"Z"')`.as(
            'cursor_updated_at',
          ),
        )
        .where('s.owner_account_id', '=', a.id);
      if (r.query.setId) query = query.where('s.id', '=', uuid(r.query.setId));
      if (r.query.cursor) {
        try {
          if (r.query.cursor.length > 1024) throw Error();
          const c = JSON.parse(Buffer.from(r.query.cursor, 'base64url').toString()) as {
            updatedAt: string;
            id: string;
          };
          if (!UUID.test(c.id) || !Number.isFinite(Date.parse(c.updatedAt))) throw Error();
          query = query.where(
            sql<boolean>`(d.updated_at,d.id)<(${c.updatedAt}::timestamptz,${c.id}::uuid)`,
          );
        } catch {
          throw apiError('bad_request', 'Invalid pagination cursor.');
        }
      }
      const rows = await query
        .orderBy('d.updated_at', 'desc')
        .orderBy('d.id', 'desc')
        .limit(limit + 1)
        .execute();
      const last = rows[limit - 1];
      return {
        ...(rows.length > limit && last
          ? {
              nextCursor: Buffer.from(
                JSON.stringify({ updatedAt: last.cursor_updated_at, id: last.id }),
              ).toString('base64url'),
            }
          : {}),
        items: rows.slice(0, limit).map((d) => ({
          id: d.id,
          title: d.title,
          updatedAt: d.updated_at.toISOString(),
          publishedVersionId: d.published_version_id,
        })),
      };
    },
  );
  app.get<{ Params: { id: string } }>('/api/v1/set-drafts/:id', async (r) =>
    draftView(await ownedDraft(r.params.id, await signed(r))),
  );
  app.put<{ Params: { id: string } }>(
    '/api/v1/set-drafts/:id',
    { bodyLimit: SET_PACKAGE_MAX_BYTES },
    async (r, reply) => {
      const a = await signed(r);
      await enforce(changes, a.id, reply, 'Too many set changes.');
      const input = body(SaveSetDraftRequest, r.body),
        current = await ownedDraft(r.params.id, a);
      if (input.package.setId !== current.set_id || input.package.versionId !== current.id)
        throw apiError('bad_request', 'Draft identities cannot change.');
      if (Buffer.byteLength(JSON.stringify(input.package)) > SET_PACKAGE_MAX_BYTES)
        throw apiError('bad_request', 'Set exceeds 16 MiB.');
      const row = await db
        .updateTable('set_drafts')
        .set({
          document: JSON.stringify(input.package),
          revision: input.revision + 1,
          hash: null,
          validation_job_id: null,
          sim_version: null,
          report: null,
          status: null,
          error: null,
          updated_at: new Date(),
        })
        .where('id', '=', current.id)
        .where('revision', '=', input.revision)
        .where('published_version_id', 'is', null)
        .returningAll()
        .executeTakeFirst();
      if (!row)
        throw apiError('conflict', 'This draft changed or was published. Reload it before saving.');
      return draftView(row);
    },
  );
  const Revision = Type.Object(
    { revision: Type.Integer({ minimum: 1 }) },
    { additionalProperties: false },
  );
  app.post<{ Params: { id: string } }>('/api/v1/set-drafts/:id/validate', async (r, reply) => {
    const a = await signed(r);
    await enforce(checks, a.id, reply, 'Too many set checks.');
    const input = body(Revision, r.body),
      current = await ownedDraft(r.params.id, a);
    const agent = await db
      .selectFrom('engine_agents')
      .select('sim_version')
      .where('last_seen_at', '>', new Date(Date.now() - 120000))
      .where(sql<boolean>`'validate-set'=ANY(kinds)`)
      .orderBy('last_seen_at', 'desc')
      .executeTakeFirst();
    const sim = agent ? parseSimVersionKey(agent.sim_version) : undefined;
    if (!sim)
      throw apiError(
        'unavailable',
        'No set validator is available. Your draft is saved; try checks later.',
      );
    const bytes = Buffer.from(JSON.stringify(current.document));
    if (bytes.length > SET_PACKAGE_MAX_BYTES) throw apiError('bad_request', 'Set exceeds 16 MiB.');
    const stored = await putContent(blobs, bytes);
    const row = await db.transaction().execute(async (trx) => {
      const draft = await trx
        .selectFrom('set_drafts')
        .selectAll()
        .where('id', '=', current.id)
        .forUpdate()
        .executeTakeFirstOrThrow();
      if (
        draft.revision !== input.revision ||
        draft.published_version_id ||
        draft.revision !== current.revision
      )
        throw apiError('conflict', 'This draft changed. Save and check its current revision.');
      if (
        draft.hash === stored.sha256 &&
        (draft.status === 'pending' || draft.status === 'valid' || draft.status === 'invalid')
      )
        return draft;
      await insertBlob(trx, stored.sha256, stored.size, 'application/json', 'private', a.id);
      const job = await submitEngineJob(trx, {
        kind: 'validate-set',
        simVersion: sim,
        payload: { blobHash: stored.sha256, suite: 1 },
      });
      return trx
        .updateTable('set_drafts')
        .set({
          hash: stored.sha256,
          validation_job_id: job,
          sim_version: simVersionKey(sim),
          report: null,
          status: 'pending',
          error: null,
        })
        .where('id', '=', draft.id)
        .returningAll()
        .executeTakeFirstOrThrow();
    });
    return draftView(row);
  });
  app.post<{ Params: { id: string } }>('/api/v1/set-drafts/:id/publish', async (r) => {
    const a = await signed(r),
      input = body(PublishSetRequest, r.body),
      current = await ownedDraft(r.params.id, a);
    await db.transaction().execute(async (trx) => {
      const parent = await trx
        .selectFrom('asset_sets')
        .selectAll()
        .where('id', '=', current.set_id)
        .forUpdate()
        .executeTakeFirstOrThrow();
      if (parent.hidden) throw apiError('forbidden', 'A hidden set cannot publish new releases.');
      const draft = await trx
        .selectFrom('set_drafts')
        .selectAll()
        .where('id', '=', current.id)
        .forUpdate()
        .executeTakeFirstOrThrow();
      if (draft.revision !== input.revision)
        throw apiError('conflict', 'This draft changed. Check it again.');
      if (draft.published_version_id) return;
      if (
        draft.status !== 'valid' ||
        !draft.hash ||
        !draft.sim_version ||
        !draft.report?.valid ||
        draft.report.hash !== draft.hash
      )
        throw apiError('conflict', 'Complete passing checks before publishing.');
      const count = await trx
        .selectFrom('set_versions')
        .select(sql<number>`count(*)::int`.as('n'))
        .where('set_id', '=', draft.set_id)
        .executeTakeFirstOrThrow();
      if (count.n >= 50) throw apiError('conflict', 'This set has reached its 50-release limit.');
      const duplicate = await trx
        .selectFrom('set_versions')
        .select('id')
        .where('set_id', '=', draft.set_id)
        .where((eb) => eb.or([eb('label', '=', input.label), eb('hash', '=', draft.hash ?? '')]))
        .executeTakeFirst();
      if (duplicate)
        throw apiError('conflict', 'This release label or package is already published.');
      await trx
        .insertInto('set_versions')
        .values({
          id: draft.id,
          set_id: draft.set_id,
          hash: draft.hash,
          label: input.label,
          notes: input.notes,
          license: draft.document.license,
          credits: JSON.stringify(draft.document.credits),
          sim_version: draft.sim_version,
          min_version_minor: draft.report.minVersionMinor,
          report: JSON.stringify(draft.report),
          preview_hash: draft.report.previewHash ?? null,
        })
        .execute();
      await trx
        .updateTable('set_drafts')
        .set({ published_version_id: draft.id })
        .where('id', '=', draft.id)
        .execute();
      await trx
        .updateTable('asset_sets')
        .set({
          title: draft.document.title,
          description: draft.document.description,
          tags: draft.document.tags,
          visibility: input.visibility,
          updated_at: new Date(),
        })
        .where('id', '=', draft.set_id)
        .execute();
    });
    return info(await visible(current.set_id, a), a);
  });
  app.patch<{ Params: { id: string } }>('/api/v1/sets/:id', async (r) => {
    const a = await signed(r),
      row = await visible(r.params.id, a, true),
      input = body(UpdateSetRequest, r.body);
    await db
      .updateTable('asset_sets')
      .set({ ...input, updated_at: new Date() })
      .where('id', '=', row.id)
      .execute();
    return info(await visible(row.id, a), a);
  });
  app.delete<{ Params: { id: string } }>('/api/v1/sets/:id', async (r, reply) => {
    const a = await signed(r),
      row = await visible(r.params.id, a, true);
    await removeSet(a.id, row.id);
    return reply.status(204).send();
  });
  app.delete<{ Params: { id: string } }>('/api/v1/set-drafts/:id', async (r, reply) => {
    const account = await signed(r),
      draft = await ownedDraft(r.params.id, account);
    await removeSet(account.id, draft.set_id, draft.id);
    return reply.status(204).send();
  });
  async function removeSet(account: string, set: string, draft?: string) {
    try {
      await new TerrainStudio(db).removeSet(account, set, draft);
    } catch (error) {
      if (error instanceof HiveError)
        throw apiError(error.code === 'not_found' ? 'not_found' : 'conflict', error.message);
      throw error;
    }
  }
  async function file(hash: string, reply: FastifyReply, type: string) {
    const stream = await blobs.get(contentKey(hash));
    if (!stream) throw apiError('not_found', 'Set file not found.');
    return reply.header('Cache-Control', 'private, no-store').type(type).send(stream);
  }
  app.get<{ Params: { id: string; versionId: string } }>(
    '/api/v1/sets/:id/versions/:versionId/file',
    async (r, reply) => {
      const a = await caller(r),
        parent = await visible(r.params.id, a);
      if (parent.hidden) throw apiError('not_found', 'Set unavailable.');
      const version = await db
        .selectFrom('set_versions')
        .selectAll()
        .where('set_id', '=', parent.id)
        .where('id', '=', uuid(r.params.versionId))
        .executeTakeFirst();
      if (!version) throw apiError('not_found', 'Release not found.');
      const downloader = a ? `a:${a.id}` : createHash('sha256').update(r.ip).digest('hex');
      await db
        .insertInto('set_downloads')
        .values({ version_id: version.id, downloader })
        .onConflict((oc) => oc.columns(['version_id', 'downloader', 'day']).doNothing())
        .execute();
      reply.header('Content-Disposition', `attachment; filename="set-${version.id}.json"`);
      return file(version.hash, reply, 'application/json');
    },
  );
  app.get<{ Params: { id: string; versionId: string } }>(
    '/api/v1/sets/:id/versions/:versionId/preview',
    async (r, reply) => {
      const parent = await visible(r.params.id, await caller(r));
      if (parent.hidden) throw apiError('not_found', 'Set unavailable.');
      const version = await db
        .selectFrom('set_versions')
        .select('preview_hash')
        .where('set_id', '=', parent.id)
        .where('id', '=', uuid(r.params.versionId))
        .executeTakeFirst();
      if (!version?.preview_hash) throw apiError('not_found', 'Preview not found.');
      return file(version.preview_hash, reply, 'image/png');
    },
  );
  app.get<{ Params: { id: string } }>('/api/v1/set-drafts/:id/preview', async (r, reply) => {
    const d = await ownedDraft(r.params.id, await signed(r));
    if (!d.report?.previewHash) throw apiError('not_found', 'Run checks to create a preview.');
    return file(d.report.previewHash, reply, 'image/png');
  });
  for (const method of ['put', 'delete'] as const)
    app[method]<{ Params: { id: string } }>('/api/v1/sets/:id/like', async (r, reply) => {
      const a = await signed(r),
        row = await visible(r.params.id, a);
      if (row.hidden) throw apiError('not_found', 'Set unavailable.');
      if (method === 'put')
        await db
          .insertInto('set_likes')
          .values({ set_id: row.id, account_id: a.id })
          .onConflict((oc) => oc.columns(['set_id', 'account_id']).doNothing())
          .execute();
      else
        await db
          .deleteFrom('set_likes')
          .where('set_id', '=', row.id)
          .where('account_id', '=', a.id)
          .execute();
      return reply.status(204).send();
    });
  app.post<{ Params: { id: string } }>('/api/v1/sets/:id/reports', async (r, reply) => {
    const a = await signed(r),
      row = await visible(r.params.id, a),
      input = body(MapReportRequest, r.body);
    await enforce(reports, a.id, reply, 'Too many reports.');
    await db
      .insertInto('set_reports')
      .values({
        set_id: row.id,
        reporter_account_id: a.id,
        reason: input.reason,
        details: input.details ?? '',
        resolution: null,
      })
      .execute();
    return reply.status(201).send({ reported: true });
  });
  app.get('/api/v1/admin/sets/reports', async (r) => {
    await requireRole(identity, r, 'moderator');
    return {
      items: await db
        .selectFrom('set_reports')
        .selectAll()
        .where('resolved', '=', false)
        .orderBy('created_at', 'asc')
        .limit(100)
        .execute(),
    };
  });
  for (const action of ['hide', 'unhide'] as const)
    app.post<{ Params: { id: string } }>(`/api/v1/admin/sets/:id/${action}`, async (r, reply) => {
      const a = (await requireRole(identity, r, 'moderator')).account;
      const reason = action === 'hide' ? body(MapHideRequest, r.body).reason : null;
      await db.transaction().execute(async (trx) => {
        await trx
          .updateTable('asset_sets')
          .set({ hidden: action === 'hide', hidden_reason: reason })
          .where('id', '=', uuid(r.params.id))
          .execute();
        await trx
          .insertInto('admin_audit_log')
          .values({
            actor_account_id: a.id,
            action: 'set:' + action,
            target_type: 'set',
            target_id: r.params.id,
            details: JSON.stringify({ reason }),
          })
          .execute();
      });
      return reply.status(204).send();
    });
  app.post<{ Params: { id: string } }>(
    '/api/v1/admin/sets/reports/:id/resolve',
    async (r, reply) => {
      const a = (await requireRole(identity, r, 'moderator')).account,
        input = body(ResolveMapReportRequest, r.body);
      await resolveReport(
        db,
        'sets',
        uuid(r.params.id),
        {
          resolution: input.status,
          reason: input.hideReason ?? input.note ?? 'Reviewed through set moderation',
          hide: input.hideMap,
        },
        a.id,
        { action: 'set:resolve-report', targetType: 'set-report', reportTarget: true },
      );
      return reply.status(204).send();
    },
  );
}
