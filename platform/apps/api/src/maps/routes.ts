import { ScriptGeneratorDescriptor, schemaIssues } from '@glob2/protocol';
import { resolveReport } from '../admin/moderation.ts';
// REST for the map catalog (/api/v1/maps) and its moderation
// (/api/v1/admin/maps, /api/v1/admin/map-reports). Rules and views are in
// catalog.ts; engine-job results are applied by the worker (play/catalog.ts).
import { webpRendition } from '../http/webpRendition.ts';
import { randomUUID } from 'node:crypto';
import type { FastifyInstance, FastifyReply, FastifyRequest } from 'fastify';
import { sql } from 'kysely';
import { putContent, submitEngineJob } from '@glob2/core';
import type { Account } from '@glob2/db';
import {
  CreateMapRequest,
  MapHideRequest,
  MapReportRequest,
  ResolveMapReportRequest,
  UpdateMapRequest,
  parseSimVersionKey,
  sameSimVersion,
  simVersionKey,
  type MapDetail,
  type MapInfo,
  type MapLikeResult,
  type MapList,
  type MapReportInfo,
  type MapReportList,
  type MapReportReceipt,
  type MapVersionInfo,
  type SimVersion,
} from '@glob2/protocol';
import { MAP_CONTENT_TYPE, insertBlob, refreshLatestVersions } from '@glob2/play';
import { supportedSimVersions } from '../app.ts';
import { apiError } from '../errors.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { authenticate, requireAccount, requireRole, type Identity } from '../identity.ts';
import { checkedUpload, newestSimVersion } from './upload.ts';
import {
  CATALOG_RULES,
  SHA256,
  UUID,
  canModerate,
  countDownload,
  isOwner,
  listMaps,
  mapView,
  mapViews,
  ownedMap,
  versionView,
  versionsOf,
  visibleMap,
  type MapRow,
  type MapSort,
  type VersionRow,
  type Viewer,
} from './catalog.ts';

const SORTS: readonly MapSort[] = ['recent', 'likes', 'plays', 'downloads'];

function intParam(value: string | undefined, name: string, min: number, max: number) {
  if (value === undefined || value === '') return undefined;
  const n = Number(value);
  if (!Number.isInteger(n) || n < min || n > max) {
    throw apiError('bad_request', `${name} must be an integer from ${min} to ${max}.`);
  }
  return n;
}

function fileName(title: string): string {
  const base =
    title
      .replace(/[^A-Za-z0-9 ._-]+/g, '')
      .trim()
      .slice(0, 64) || 'map';
  return `${base}.map`;
}

type ReportRow = {
  id: string;
  map_id: string;
  reason: MapReportInfo['reason'];
  details: string;
  status: MapReportInfo['status'];
  created_at: Date;
  resolved_at: Date | null;
  resolution_note: string | null;
  reporter_id: string;
  reporter_name: string;
  reporter_kind: 'guest' | 'registered';
  reporter_created_at: Date;
  resolver_id: string | null;
  resolver_name: string | null;
  resolver_kind: 'guest' | 'registered' | null;
  resolver_created_at: Date | null;
};

export async function mapCatalogRoutes(app: FastifyInstance, identity: Identity): Promise<void> {
  const { services } = app;
  const { db, blobs } = services;
  const origin = services.config.publicOrigin;
  const created = new SharedLimit(db, 'catalog-map', CATALOG_RULES.mapsPerHour, 3_600_000);
  const uploads = new SharedLimit(db, 'catalog-version', CATALOG_RULES.versionsPerHour, 3_600_000);
  const reports = new SharedLimit(db, 'catalog-report', CATALOG_RULES.reportsPerHour, 3_600_000);

  const viewerOf = async (request: FastifyRequest): Promise<Viewer | undefined> => {
    const caller = await authenticate(identity, request);
    return caller ? { account: caller.account } : undefined;
  };
  const signedIn = async (request: FastifyRequest): Promise<Viewer> => ({
    account: (await requireAccount(identity, request)).account,
  });

  const mayPublish = (account: Account) =>
    account.kind === 'registered' || CATALOG_RULES.guestsMayPublish;

  async function infoOf(id: string, viewer: Viewer | undefined): Promise<MapInfo> {
    const map = await visibleMap(db, id, viewer);
    const [info] = await mapViews(db, origin, [map], viewer);
    if (!info) throw apiError('not_found', 'No such map.');
    return info;
  }

  async function audit(actor: Account, action: string, mapId: string, details: object) {
    await db
      .insertInto('admin_audit_log')
      .values({
        actor_account_id: actor.id,
        action,
        target_type: 'map',
        target_id: mapId,
        details: JSON.stringify(details),
      })
      .execute();
  }

  async function setHidden(actor: Account, mapId: string, hidden: boolean, reason: string | null) {
    const updated = await db
      .updateTable('maps')
      .set({
        hidden,
        hidden_reason: hidden ? reason : null,
        hidden_at: hidden ? sql<Date>`now()` : null,
        hidden_by_account_id: hidden ? actor.id : null,
      })
      .where('id', '=', mapId)
      .executeTakeFirst();
    if (updated.numUpdatedRows === 0n) throw apiError('not_found', 'No such map.');
    await audit(actor, hidden ? 'map.hide' : 'map.unhide', mapId, hidden ? { reason } : {});
  }

  async function version(map: MapRow, hash: string, viewer: Viewer | undefined) {
    if (!SHA256.test(hash)) throw apiError('not_found', 'No such version.');
    const row = (await db
      .selectFrom('map_versions')
      .selectAll()
      .where('map_id', '=', map.id)
      .where('hash', '=', hash)
      .executeTakeFirst()) as VersionRow | undefined;
    // Pending and invalid versions are the owner's (and moderators') business.
    if (!row || (row.validation !== 'valid' && !isOwner(map, viewer) && !canModerate(viewer))) {
      throw apiError('not_found', 'No such version.');
    }
    return row;
  }

  async function sendBlob(
    reply: FastifyReply,
    sha256: string,
    contentType: string,
    visibleToAll: boolean,
    disposition?: string,
  ) {
    const blob = await db
      .selectFrom('blobs')
      .select(['storage_key'])
      .where('sha256', '=', sha256)
      .executeTakeFirst();
    const stream = blob ? await blobs.get(blob.storage_key) : undefined;
    if (!blob || !stream) throw apiError('not_found', 'The file is missing from storage.');
    const size = await blobs.size(blob.storage_key);
    void reply
      .header('content-type', contentType)
      .header('etag', `"${sha256}"`)
      // Bytes never change for a hash, but visibility can: public caches only
      // keep what anyone may see, and only briefly.
      .header('cache-control', visibleToAll ? 'public, max-age=300' : 'private, max-age=300')
      .header('x-content-type-options', 'nosniff');
    if (disposition) void reply.header('content-disposition', disposition);
    if (size !== undefined) void reply.header('content-length', size);
    return reply.send(stream);
  }

  // ---------------------------------------------------------------- browse

  app.get<{
    Querystring: {
      owner?: string;
      teams?: string;
      minSide?: string;
      maxSide?: string;
      madeWith?: string;
      q?: string;
      sort?: string;
      cursor?: string;
      limit?: string;
    };
  }>('/api/v1/maps', async (request): Promise<MapList> => {
    const query = request.query;
    const viewer = await viewerOf(request);
    let ownerId: string | undefined;
    if (query.owner === 'me') {
      if (!viewer) throw apiError('unauthenticated', 'Sign in to list your maps.');
      ownerId = viewer.account.id;
    } else if (query.owner !== undefined) {
      if (!UUID.test(query.owner))
        throw apiError('bad_request', 'owner must be "me" or an account id.');
      ownerId = query.owner;
    }
    const sort = (query.sort ?? 'recent') as MapSort;
    if (!SORTS.includes(sort))
      throw apiError('bad_request', `sort must be one of ${SORTS.join(', ')}.`);
    if (
      query.madeWith !== undefined &&
      query.madeWith !== 'hand' &&
      query.madeWith !== 'generator'
    ) {
      throw apiError('bad_request', 'madeWith must be hand or generator.');
    }
    const teams = intParam(query.teams, 'teams', 1, 12);
    const minSide = intParam(query.minSide, 'minSide', 1, 4096);
    const maxSide = intParam(query.maxSide, 'maxSide', 1, 4096);
    const limit =
      intParam(query.limit, 'limit', 1, CATALOG_RULES.maxPageSize) ?? CATALOG_RULES.pageSize;
    const page = await listMaps(
      db,
      {
        sort,
        limit,
        ...(ownerId ? { ownerId } : {}),
        ...(teams !== undefined ? { teams } : {}),
        ...(minSide !== undefined ? { minSide } : {}),
        ...(maxSide !== undefined ? { maxSide } : {}),
        ...(query.madeWith ? { madeWith: query.madeWith as 'hand' | 'generator' } : {}),
        ...(query.q ? { q: query.q.slice(0, 128) } : {}),
        ...(query.cursor ? { cursor: query.cursor } : {}),
      },
      viewer,
    );
    return {
      items: await mapViews(db, origin, page.rows, viewer),
      ...(page.nextCursor ? { nextCursor: page.nextCursor } : {}),
    };
  });

  app.get<{ Params: { id: string } }>('/api/v1/maps/:id', async (request): Promise<MapDetail> => {
    const viewer = await viewerOf(request);
    const map = await visibleMap(db, request.params.id, viewer);
    const privileged = isOwner(map, viewer) || canModerate(viewer);
    const versions = (await versionsOf(db, [map.id])).filter(
      (v) => privileged || v.validation === 'valid',
    );
    const latest = versions.find((v) => v.id === map.latest_version_id);
    const liked =
      viewer !== undefined &&
      (await db
        .selectFrom('map_likes')
        .select('map_id')
        .where('map_id', '=', map.id)
        .where('account_id', '=', viewer.account.id)
        .executeTakeFirst()) !== undefined;
    const reported =
      viewer !== undefined &&
      (await db
        .selectFrom('map_reports')
        .select('id')
        .where('map_id', '=', map.id)
        .where('reporter_account_id', '=', viewer.account.id)
        .where('status', '=', 'open')
        .executeTakeFirst()) !== undefined;
    return {
      map: mapView(origin, map, latest, viewer),
      versions: versions.map((v) => versionView(origin, v)),
      viewer: {
        owner: isOwner(map, viewer),
        moderator: canModerate(viewer),
        liked,
        reported,
      },
    };
  });

  // ------------------------------------------------------- owner: create

  app.post('/api/v1/maps', async (request, reply) => {
    const viewer = await signedIn(request);
    const input = body(CreateMapRequest, request.body);
    const visibility = input.visibility ?? CATALOG_RULES.defaultVisibility;
    if (visibility === 'public' && !mayPublish(viewer.account)) {
      throw apiError(
        'forbidden',
        'Sign in with an account to publish maps; guests can share unlisted maps.',
      );
    }
    if (input.generator && input.madeWith === 'hand') {
      throw apiError('bad_request', 'A hand-made map has no generator.');
    }
    await enforce(created, viewer.account.id, undefined, 'Too many new maps; wait a while.');
    const row = await db
      .insertInto('maps')
      .values({
        owner_account_id: viewer.account.id,
        title: input.title,
        description: input.description ?? '',
        visibility,
        made_with: input.generator ? 'generator' : (input.madeWith ?? 'hand'),
        generator: input.generator ? JSON.stringify(input.generator) : null,
      })
      .returning('id')
      .executeTakeFirstOrThrow();
    return reply.status(201).send(await infoOf(row.id, viewer));
  });

  app.patch<{ Params: { id: string } }>('/api/v1/maps/:id', async (request): Promise<MapInfo> => {
    const viewer = await signedIn(request);
    const map = await ownedMap(db, request.params.id, viewer);
    const input = body(UpdateMapRequest, request.body);
    if (input.visibility === 'public' && !mayPublish(viewer.account)) {
      throw apiError(
        'forbidden',
        'Sign in with an account to publish maps; guests can share unlisted maps.',
      );
    }
    if (Object.keys(input).length > 0) {
      await db
        .updateTable('maps')
        .set({
          ...(input.title !== undefined ? { title: input.title } : {}),
          ...(input.description !== undefined ? { description: input.description } : {}),
          ...(input.visibility !== undefined ? { visibility: input.visibility } : {}),
          updated_at: sql<Date>`now()`,
        })
        .where('id', '=', map.id)
        .execute();
    }
    return infoOf(map.id, viewer);
  });

  app.delete<{ Params: { id: string } }>('/api/v1/maps/:id', async (request, reply) => {
    const viewer = await signedIn(request);
    const map = await ownedMap(db, request.params.id, viewer, { moderators: 'admin' });
    // Versions, likes, reports and download counts go with the map. The bytes
    // stay: matches played on them and rooms using them still need them.
    await db.deleteFrom('maps').where('id', '=', map.id).execute();
    if (!isOwner(map, viewer))
      await audit(viewer.account, 'map.delete', map.id, { title: map.title });
    return reply.status(204).send();
  });

  // ----------------------------------------------------- owner: versions

  app.post<{
    Params: { id: string };
    Querystring: { simVersion?: string; notes?: string; generator?: string };
  }>(
    '/api/v1/maps/:id/versions',
    {
      bodyLimit: services.config.uploadMaxBytes ?? 64 * 1024 * 1024,
    },
    async (request, reply) => {
      const viewer = await signedIn(request);
      const map = await ownedMap(db, request.params.id, viewer);
      if (map.authoring?.kind === 'ai')
        throw apiError(
          'conflict',
          'AI map versions are immutable. Create a revision in AI Map Studio or upload an edited copy as a new map.',
        );
      const served = await supportedSimVersions(db);
      let simVersion: SimVersion | undefined;
      if (request.query.simVersion !== undefined) {
        const asked = parseSimVersionKey(request.query.simVersion);
        if (!asked) throw apiError('bad_request', 'simVersion must be a sim version key.');
        if (!served.some((v) => sameSimVersion(v, asked))) {
          throw apiError('update_required', 'This instance cannot validate maps of that version.');
        }
        simVersion = asked;
      } else {
        simVersion = newestSimVersion(served);
        if (!simVersion)
          throw apiError('unavailable', 'No engine agent can validate maps right now.');
      }
      const notes = (request.query.notes ?? '').slice(0, 2000);
      let claimedGenerator: ScriptGeneratorDescriptor | undefined;
      if (request.query.generator) {
        try {
          claimedGenerator = body(ScriptGeneratorDescriptor, JSON.parse(request.query.generator));
        } catch {
          throw apiError('bad_request', 'Invalid claimed generator provenance.');
        }
      }
      // The quota is taken before the file is unpacked, so a flood of
      // compressed files costs the sender, not the server.
      await enforce(uploads, viewer.account.id, reply, 'Too many uploads; wait a while.');
      // Unpacked when gzip (.map.gz): the stored bytes are the ones the game loads.
      const bytes = await checkedUpload(request.body, 'map', simVersion.versionMinor);
      const sim = simVersionKey(simVersion);
      const stored = await putContent(blobs, bytes);
      // Catalog bytes are private blobs; catalog rules decide who may fetch them.
      await insertBlob(
        db,
        stored.sha256,
        stored.size,
        MAP_CONTENT_TYPE,
        'private',
        viewer.account.id,
      );

      const existing = (await db
        .selectFrom('map_versions')
        .selectAll()
        .where('map_id', '=', map.id)
        .where('hash', '=', stored.sha256)
        .executeTakeFirst()) as VersionRow | undefined;
      if (existing) return versionView(origin, existing);
      const count = await db
        .selectFrom('map_versions')
        .select(sql<number>`count(*)::int`.as('n'))
        .where('map_id', '=', map.id)
        .executeTakeFirstOrThrow();
      if (count.n >= CATALOG_RULES.maxVersionsPerMap) {
        throw apiError(
          'conflict',
          `A map keeps at most ${CATALOG_RULES.maxVersionsPerMap} versions; delete old ones first.`,
        );
      }

      // The same bytes checked for the same sim version (any map) need no new job.
      const same = (await db
        .selectFrom('map_versions')
        .selectAll()
        .where('hash', '=', stored.sha256)
        .where('sim_version', '=', sim)
        .orderBy(sql`validation = 'pending'`)
        .orderBy(sql`preview_status = 'ready'`, 'desc')
        .orderBy('created_at', 'desc')
        .executeTakeFirst()) as VersionRow | undefined;
      // An upload checked before the map was created (the web app's upload
      // form does that) already has the engine's verdict.
      const checked = same
        ? undefined
        : await db
            .selectFrom('map_uploads')
            .selectAll()
            .where('blob_sha256', '=', stored.sha256)
            .where('format', '=', 'map')
            .where('sim_version', '=', sim)
            .where('status', '=', 'valid')
            .where('job_id', 'is not', null)
            .executeTakeFirst();
      const validateJobId = same?.validate_job_id ?? checked?.job_id ?? randomUUID();
      const reusePreview = same && same.preview_status !== 'failed';
      const previewJobId = reusePreview ? (same.preview_job_id ?? randomUUID()) : randomUUID();
      const generated = await db
        .selectFrom('generated_maps')
        .select(['descriptor', 'chosen_seed'])
        .where('map_hash', '=', stored.sha256)
        .where('sim_version', '=', sim)
        .where('status', '=', 'ready')
        .executeTakeFirst();
      const provenance =
        generated && schemaIssues(ScriptGeneratorDescriptor, generated.descriptor).length === 0
          ? {
              verified: true,
              generator: generated.descriptor,
              ...(generated.chosen_seed !== null
                ? { chosenSeed: Number(generated.chosen_seed) }
                : {}),
            }
          : claimedGenerator
            ? { verified: false, generator: claimedGenerator }
            : undefined;
      const inserted = await db
        .insertInto('map_versions')
        .values({
          map_id: map.id,
          ...(provenance ? { generator_provenance: JSON.stringify(provenance) } : {}),
          hash: stored.sha256,
          size: stored.size,
          sim_version: sim,
          uploader_account_id: viewer.account.id,
          notes,
          validate_job_id: validateJobId,
          preview_job_id: previewJobId,
          ...(same && same.validation !== 'pending'
            ? {
                validation: same.validation,
                validation_error: same.validation_error,
                width: same.width,
                height: same.height,
                team_count: same.team_count,
                min_version_minor: same.min_version_minor,
                file_title: same.file_title,
                building_catalog: same.building_catalog,
                resource_experiments: same.resource_experiments,
                required_resource_experiments: same.required_resource_experiments,
              }
            : checked
              ? {
                  validation: 'valid' as const,
                  width: checked.width,
                  height: checked.height,
                  team_count: checked.team_count,
                  min_version_minor: checked.version_minor,
                  file_title: checked.title,
                  building_catalog: checked.building_catalog,
                  resource_experiments: checked.resource_experiments,
                  required_resource_experiments: checked.required_resource_experiments,
                }
              : {}),
          ...(reusePreview && same.preview_status === 'ready'
            ? {
                preview_status: 'ready' as const,
                preview_hash: same.preview_hash,
                preview_width: same.preview_width,
                preview_height: same.preview_height,
              }
            : {}),
        })
        .onConflict((oc) => oc.columns(['map_id', 'hash']).doNothing())
        .returningAll()
        .executeTakeFirst();
      if (!inserted) {
        // A concurrent upload of the same bytes to this map won.
        const row = await db
          .selectFrom('map_versions')
          .selectAll()
          .where('map_id', '=', map.id)
          .where('hash', '=', stored.sha256)
          .executeTakeFirstOrThrow();
        return versionView(origin, row as VersionRow);
      }
      // Jobs are submitted after the row names them, so a fast result always
      // finds the version it completes.
      if (!same && !checked) {
        await submitEngineJob(db, {
          kind: 'validate-map',
          simVersion,
          payload: { blobHash: stored.sha256, format: 'map' },
          jobId: validateJobId,
        });
      }
      if (!reusePreview) {
        await submitEngineJob(db, {
          kind: 'render-preview',
          simVersion,
          payload: { mapHash: stored.sha256, maxSizePx: CATALOG_RULES.previewSizePx },
          jobId: previewJobId,
        });
      }
      if (inserted.validation === 'valid') await refreshLatestVersions(db, [map.id]);
      else
        await db
          .updateTable('maps')
          .set({ updated_at: sql<Date>`now()` })
          .where('id', '=', map.id)
          .execute();
      return reply.status(201).send(versionView(origin, inserted as VersionRow));
    },
  );

  app.get<{ Params: { id: string; hash: string } }>(
    '/api/v1/maps/:id/versions/:hash',
    async (request): Promise<MapVersionInfo> => {
      const viewer = await viewerOf(request);
      const map = await visibleMap(db, request.params.id, viewer);
      return versionView(origin, await version(map, request.params.hash, viewer));
    },
  );

  app.delete<{ Params: { id: string; hash: string } }>(
    '/api/v1/maps/:id/versions/:hash',
    async (request, reply) => {
      const viewer = await signedIn(request);
      const map = await ownedMap(db, request.params.id, viewer);
      const row = await version(map, request.params.hash, viewer);
      await db.deleteFrom('map_versions').where('id', '=', row.id).execute();
      await refreshLatestVersions(db, [map.id]);
      return reply.status(204).send();
    },
  );

  app.get<{ Params: { id: string; hash: string } }>(
    '/api/v1/maps/:id/versions/:hash/file',
    async (request, reply) => {
      const viewer = await viewerOf(request);
      const map = await visibleMap(db, request.params.id, viewer);
      const row = await version(map, request.params.hash, viewer);
      if (!isOwner(map, viewer)) {
        await countDownload(db, map.id, viewer ? `a:${viewer.account.id}` : `ip:${request.ip}`);
      }
      return sendBlob(
        reply,
        row.hash,
        'application/octet-stream',
        map.visibility !== 'private' && !map.hidden,
        `attachment; filename="${fileName(map.title)}"`,
      );
    },
  );

  app.get<{ Params: { id: string; hash: string } }>(
    '/api/v1/maps/:id/versions/:hash/preview.webp',
    async (request, reply) => {
      const viewer = await viewerOf(request);
      const map = await visibleMap(db, request.params.id, viewer);
      const row = await version(map, request.params.hash, viewer);
      if (row.preview_status !== 'ready' || !row.preview_hash) {
        throw apiError(
          'not_found',
          row.preview_status === 'pending'
            ? 'The preview is not ready yet.'
            : 'This version has no preview.',
        );
      }
      return sendBlob(
        reply,
        await webpRendition(db, blobs, row.preview_hash),
        'image/webp',
        map.visibility !== 'private' && !map.hidden,
      );
    },
  );

  // ------------------------------------------------------ likes, reports

  async function likeTarget(request: FastifyRequest<{ Params: { id: string } }>) {
    const viewer = await signedIn(request);
    if (viewer.account.kind === 'guest' && !CATALOG_RULES.guestsMayLike) {
      throw apiError('forbidden', 'Sign in with an account to like maps.');
    }
    return { viewer, map: await visibleMap(db, request.params.id, viewer) };
  }

  async function likes(mapId: string, liked: boolean): Promise<MapLikeResult> {
    const row = await db
      .selectFrom('maps')
      .select('like_count')
      .where('id', '=', mapId)
      .executeTakeFirstOrThrow();
    return { liked, likes: row.like_count };
  }

  app.put<{ Params: { id: string } }>(
    '/api/v1/maps/:id/like',
    { config: { rateLimit: { max: 60, timeWindow: 60_000 } } },
    async (request): Promise<MapLikeResult> => {
      const { viewer, map } = await likeTarget(request);
      await db.transaction().execute(async (trx) => {
        const added = await trx
          .insertInto('map_likes')
          .values({ map_id: map.id, account_id: viewer.account.id })
          .onConflict((oc) => oc.columns(['map_id', 'account_id']).doNothing())
          .executeTakeFirst();
        if ((added.numInsertedOrUpdatedRows ?? 0n) > 0n) {
          await trx
            .updateTable('maps')
            .set((eb) => ({ like_count: eb('like_count', '+', 1) }))
            .where('id', '=', map.id)
            .execute();
        }
      });
      return likes(map.id, true);
    },
  );

  app.delete<{ Params: { id: string } }>(
    '/api/v1/maps/:id/like',
    { config: { rateLimit: { max: 60, timeWindow: 60_000 } } },
    async (request): Promise<MapLikeResult> => {
      const { viewer, map } = await likeTarget(request);
      await db.transaction().execute(async (trx) => {
        const removed = await trx
          .deleteFrom('map_likes')
          .where('map_id', '=', map.id)
          .where('account_id', '=', viewer.account.id)
          .executeTakeFirst();
        if (removed.numDeletedRows > 0n) {
          await trx
            .updateTable('maps')
            .set((eb) => ({ like_count: sql<number>`greatest(${eb.ref('like_count')} - 1, 0)` }))
            .where('id', '=', map.id)
            .execute();
        }
      });
      return likes(map.id, false);
    },
  );

  app.post<{ Params: { id: string } }>('/api/v1/maps/:id/reports', async (request, reply) => {
    const viewer = await signedIn(request);
    const map = await visibleMap(db, request.params.id, viewer);
    const input = body(MapReportRequest, request.body);
    const open = await db
      .selectFrom('map_reports')
      .select(['id', 'status'])
      .where('map_id', '=', map.id)
      .where('reporter_account_id', '=', viewer.account.id)
      .where('status', '=', 'open')
      .executeTakeFirst();
    if (open) return { id: open.id, status: open.status } satisfies MapReportReceipt;
    await enforce(reports, viewer.account.id, undefined, 'Too many reports; wait a while.');
    const row = await db
      .insertInto('map_reports')
      .values({
        map_id: map.id,
        reporter_account_id: viewer.account.id,
        reason: input.reason,
        details: input.details,
      })
      .onConflict((oc) =>
        oc.columns(['map_id', 'reporter_account_id']).where('status', '=', 'open').doNothing(),
      )
      .returning(['id', 'status'])
      .executeTakeFirst();
    if (!row) throw apiError('conflict', 'You already reported this map.');
    return reply.status(201).send({ id: row.id, status: row.status } satisfies MapReportReceipt);
  });

  // ----------------------------------------------------------- moderation

  async function reportViews(rows: ReportRow[], viewer: Viewer): Promise<MapReportInfo[]> {
    const mapRows = rows.length
      ? await Promise.all(
          [...new Set(rows.map((r) => r.map_id))].map((id) => visibleMap(db, id, viewer)),
        )
      : [];
    const maps = new Map(
      (await mapViews(db, origin, mapRows, viewer)).map((info) => [info.id, info]),
    );
    return rows.flatMap((r) => {
      const map = maps.get(r.map_id);
      return map ? [{ ...reportView(r), map }] : [];
    });
  }

  function reportView(r: ReportRow): Omit<MapReportInfo, 'map'> {
    return {
      id: r.id,
      reporter: {
        id: r.reporter_id,
        displayName: r.reporter_name,
        kind: r.reporter_kind,
        createdAt: r.reporter_created_at.toISOString(),
      },
      reason: r.reason,
      details: r.details,
      status: r.status,
      createdAt: r.created_at.toISOString(),
      ...(r.resolved_at ? { resolvedAt: r.resolved_at.toISOString() } : {}),
      ...(r.resolver_id && r.resolver_name && r.resolver_kind && r.resolver_created_at
        ? {
            resolvedBy: {
              id: r.resolver_id,
              displayName: r.resolver_name,
              kind: r.resolver_kind,
              createdAt: r.resolver_created_at.toISOString(),
            },
          }
        : {}),
      ...(r.resolution_note ? { note: r.resolution_note } : {}),
    };
  }

  const reportQuery = () =>
    db
      .selectFrom('map_reports as r')
      .innerJoin('accounts as rep', 'rep.id', 'r.reporter_account_id')
      .leftJoin('accounts as res', 'res.id', 'r.resolved_by_account_id')
      .select([
        'r.id',
        'r.map_id',
        'r.reason',
        'r.details',
        'r.status',
        'r.created_at',
        'r.resolved_at',
        'r.resolution_note',
        'rep.id as reporter_id',
        'rep.display_name as reporter_name',
        'rep.kind as reporter_kind',
        'rep.created_at as reporter_created_at',
        'res.id as resolver_id',
        'res.display_name as resolver_name',
        'res.kind as resolver_kind',
        'res.created_at as resolver_created_at',
      ]);

  app.get<{ Querystring: { status?: string; mapId?: string; cursor?: string; limit?: string } }>(
    '/api/v1/admin/map-reports',
    async (request): Promise<MapReportList> => {
      const viewer: Viewer = {
        account: (await requireRole(identity, request, 'moderator')).account,
      };
      const status = request.query.status ?? 'open';
      if (!['open', 'resolved', 'dismissed', 'all'].includes(status)) {
        throw apiError('bad_request', 'status must be open, resolved, dismissed or all.');
      }
      const limit = intParam(request.query.limit, 'limit', 1, 100) ?? 50;
      let query = reportQuery();
      if (status !== 'all') query = query.where('r.status', '=', status as 'open');
      if (request.query.mapId) {
        if (!UUID.test(request.query.mapId)) throw apiError('bad_request', 'Invalid mapId.');
        query = query.where('r.map_id', '=', request.query.mapId);
      }
      if (request.query.cursor) {
        let at: Date;
        let id: string;
        try {
          [at, id] = (([a, b]: [string, string]) => [new Date(a), b] as const)(
            JSON.parse(Buffer.from(request.query.cursor, 'base64url').toString()) as [
              string,
              string,
            ],
          );
          if (Number.isNaN(at.getTime()) || !UUID.test(id)) throw new Error('bad cursor');
        } catch {
          throw apiError('bad_request', 'Invalid cursor.');
        }
        query = query.where((eb) =>
          eb.or([
            eb('r.created_at', '<', at),
            eb.and([eb('r.created_at', '=', at), eb('r.id', '<', id)]),
          ]),
        );
      }
      const rows = await query
        .orderBy('r.created_at', 'desc')
        .orderBy('r.id', 'desc')
        .limit(limit + 1)
        .execute();
      const page = rows.slice(0, limit);
      const last = page.at(-1);
      return {
        items: await reportViews(page, viewer),
        ...(rows.length > limit && last
          ? {
              nextCursor: Buffer.from(
                JSON.stringify([last.created_at.toISOString(), last.id]),
              ).toString('base64url'),
            }
          : {}),
      };
    },
  );

  app.post<{ Params: { id: string } }>(
    '/api/v1/admin/map-reports/:id/resolve',
    async (request): Promise<MapReportInfo> => {
      const actor = (await requireRole(identity, request, 'moderator')).account;
      const input = body(ResolveMapReportRequest, request.body);
      if (!UUID.test(request.params.id)) throw apiError('not_found', 'No such report.');
      await resolveReport(
        db,
        'maps',
        request.params.id,
        {
          resolution: input.status,
          reason: input.hideReason ?? input.note ?? 'Reviewed through map moderation',
          hide: input.hideMap,
        },
        actor.id,
        { action: 'map.report.' + input.status, targetType: 'map', reportTarget: false },
      );
      const row = await reportQuery()
        .where('r.id', '=', request.params.id)
        .executeTakeFirstOrThrow();
      const [view] = await reportViews([row], { account: actor });
      if (!view) throw apiError('not_found', 'No such report.');
      return view;
    },
  );

  app.post<{ Params: { id: string } }>(
    '/api/v1/admin/maps/:id/hide',
    async (request): Promise<MapInfo> => {
      const actor = (await requireRole(identity, request, 'moderator')).account;
      const input = body(MapHideRequest, request.body);
      const map = await visibleMap(db, request.params.id, { account: actor });
      await setHidden(actor, map.id, true, input.reason);
      return infoOf(map.id, { account: actor });
    },
  );

  app.post<{ Params: { id: string } }>(
    '/api/v1/admin/maps/:id/unhide',
    async (request): Promise<MapInfo> => {
      const actor = (await requireRole(identity, request, 'moderator')).account;
      const map = await visibleMap(db, request.params.id, { account: actor });
      await setHidden(actor, map.id, false, null);
      return infoOf(map.id, { account: actor });
    },
  );
}
