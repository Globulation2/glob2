// REST for rooms and maps: public room list, invite lookups, uploads of
// private maps and saves, and map downloads by hash.
import type { FastifyInstance } from 'fastify';
import { sql } from 'kysely';
import { putContent, submitEngineJob } from '@glob2/core';
import {
  parseSimVersionKey,
  sameSimVersion,
  simVersionKey,
  type InviteInfo,
  type MapUpload,
  type RelayRegionList,
  type RoomList,
  type SavedPlayer,
  type SimVersion,
} from '@glob2/protocol';
import {
  MAP_CONTENT_TYPE,
  SAVE_CONTENT_TYPE,
  insertBlob,
  relayRegions,
  storedSimVersion,
} from '@glob2/play';
import { supportedSimVersions } from '../app.ts';
import { apiError } from '../errors.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { authenticate, requireAccount, type Identity } from '../identity.ts';
import { mapUrl } from '../play/assignments.ts';
import type { RoomService } from '../play/rooms.ts';
import { catalogAllowsMapBlob } from '../maps/catalog.ts';
import { checkedUpload, newestSimVersion } from '../maps/upload.ts';

const SHA256 = /^[0-9a-f]{64}$/;
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
/** Uploads per account per hour (per replica). */
const UPLOADS_PER_HOUR = 30;

type UploadRow = {
  id: string;
  format: 'map' | 'save';
  blob_sha256: string;
  sim_version: string;
  status: 'pending' | 'valid' | 'invalid';
  file_name: string | null;
  width: number | null;
  height: number | null;
  team_count: number | null;
  version_minor: number | null;
  title: string | null;
  players: unknown;
  failure: string | null;
  created_at: Date;
};

function cursorOf(value: { updatedAt: Date; id: string }): string {
  return Buffer.from(JSON.stringify([value.updatedAt.toISOString(), value.id])).toString(
    'base64url',
  );
}

function parseCursor(text: string | undefined): { updatedAt: Date; id: string } | undefined {
  if (!text) return undefined;
  try {
    const [at, id] = JSON.parse(Buffer.from(text, 'base64url').toString()) as [string, string];
    const updatedAt = new Date(at);
    if (Number.isNaN(updatedAt.getTime()) || !UUID.test(id)) throw new Error('bad cursor');
    return { updatedAt, id };
  } catch {
    throw apiError('bad_request', 'Invalid cursor.');
  }
}

export async function playRoutes(
  app: FastifyInstance,
  identity: Identity,
  rooms: RoomService,
): Promise<void> {
  const { services } = app;
  const { db, blobs } = services;
  const origin = services.config.publicOrigin;
  const uploads = new SharedLimit(db, 'upload', UPLOADS_PER_HOUR, 3_600_000);

  function uploadView(row: UploadRow, size: number): MapUpload {
    const simVersion = storedSimVersion(row.sim_version);
    return {
      id: row.id,
      format: row.format,
      sha256: row.blob_sha256,
      size,
      simVersion,
      status: row.status,
      ...(row.file_name ? { fileName: row.file_name } : {}),
      ...(row.width && row.height && row.team_count
        ? { map: { width: row.width, height: row.height, teamCount: row.team_count } }
        : {}),
      ...(row.version_minor !== null ? { versionMinor: row.version_minor } : {}),
      ...(row.title ? { title: row.title } : {}),
      ...(row.players ? { players: row.players as SavedPlayer[] } : {}),
      ...(row.status === 'invalid' && row.failure ? { reason: row.failure } : {}),
      downloadUrl: mapUrl(origin, row.blob_sha256),
      createdAt: row.created_at.toISOString(),
    };
  }

  // ------------------------------------------------------------------ relays

  // Public: clients time these before queue.join and room.create (no account needed).
  app.get('/api/v1/relays/regions', async (_request, reply): Promise<RelayRegionList> => {
    reply.header('Cache-Control', 'no-store');
    return { items: await relayRegions(db) };
  });

  // ------------------------------------------------------------------ rooms

  app.get<{ Querystring: { simVersion?: string; cursor?: string; limit?: string } }>(
    '/api/v1/rooms',
    async (request): Promise<RoomList> => {
      const sim = request.query.simVersion;
      if (!sim || !parseSimVersionKey(sim)) {
        throw apiError('bad_request', 'simVersion (a sim version key) is required.');
      }
      const limit = Math.min(Math.max(Number(request.query.limit ?? 50) || 50, 1), 100);
      const page = await rooms.listPublic(sim, limit, parseCursor(request.query.cursor));
      return { items: page.items, ...(page.next ? { nextCursor: cursorOf(page.next) } : {}) };
    },
  );

  app.get<{ Params: { code: string } }>(
    '/api/v1/invites/:code',
    { config: { rateLimit: { max: 60, timeWindow: 60_000 } } },
    async (request): Promise<InviteInfo> => {
      const code = request.params.code;
      const room = /^[A-Za-z0-9]{6,16}$/.test(code) ? await rooms.byCode(code) : undefined;
      if (!room) throw apiError('not_found', 'This invite has expired or does not exist.');
      return {
        code: room.code,
        status: room.status === 'starting' ? 'open' : room.status,
        roomName: room.name,
        hostDisplayName: room.host_display_name,
        simVersion: storedSimVersion(room.sim_version),
        seatsTotal: room.seats_total ?? 0,
        seatsTaken: room.seats_taken ?? 0,
        inviteUrl: rooms.inviteUrl(room.code),
      };
    },
  );

  // ---------------------------------------------------------------- uploads

  app.post<{ Querystring: { format?: string; simVersion?: string; fileName?: string } }>(
    '/api/v1/uploads',
    { bodyLimit: services.config.uploadMaxBytes ?? 16 * 1024 * 1024 },
    async (request, reply) => {
      const { account } = await requireAccount(identity, request);
      const format = request.query.format;
      if (format !== 'map' && format !== 'save') {
        throw apiError('bad_request', 'format must be map or save.');
      }
      const served = await supportedSimVersions(db);
      let simVersion: SimVersion | undefined;
      if (request.query.simVersion !== undefined) {
        simVersion = parseSimVersionKey(request.query.simVersion);
        if (!simVersion) throw apiError('bad_request', 'simVersion must be a sim version key.');
        const asked = simVersion;
        if (!served.some((v) => sameSimVersion(v, asked))) {
          throw apiError('update_required', 'This instance cannot validate files of that version.');
        }
      } else {
        // The web app checks a file before creating a catalog map with it: the newest engine.
        simVersion = newestSimVersion(served);
        if (!simVersion)
          throw apiError('unavailable', 'No engine agent can check files right now.');
      }
      const fileName = request.query.fileName?.slice(0, 255);
      // The quota is taken before the file is unpacked, so a flood of
      // compressed files costs the sender, not the server.
      await enforce(uploads, account.id, reply, 'Too many uploads; wait a while.');
      // Unpacked when gzip (.map.gz): the stored bytes are the ones the game loads.
      const bytes = await checkedUpload(request.body, format, simVersion.versionMinor);
      const sim = simVersionKey(simVersion);
      const stored = await putContent(blobs, bytes);
      await insertBlob(
        db,
        stored.sha256,
        stored.size,
        format === 'map' ? MAP_CONTENT_TYPE : SAVE_CONTENT_TYPE,
        'private',
        account.id,
      );
      const key = { blob: stored.sha256, format, sim };
      const existing = await db
        .selectFrom('map_uploads')
        .selectAll()
        .where('owner_account_id', '=', account.id)
        .where('blob_sha256', '=', key.blob)
        .where('format', '=', format)
        .where('sim_version', '=', sim)
        .executeTakeFirst();
      if (existing) return uploadView(existing, stored.size);
      // Someone already validated (or is validating) these bytes for this version.
      const known = await db
        .selectFrom('map_uploads')
        .selectAll()
        .where('blob_sha256', '=', key.blob)
        .where('format', '=', format)
        .where('sim_version', '=', sim)
        .orderBy(sql`status = 'pending'`)
        .executeTakeFirst();
      const inserted = await db
        .insertInto('map_uploads')
        .values({
          owner_account_id: account.id,
          blob_sha256: key.blob,
          format,
          sim_version: sim,
          file_name: fileName ?? null,
          ...(known
            ? {
                status: known.status,
                job_id: known.job_id,
                width: known.width,
                height: known.height,
                team_count: known.team_count,
                version_minor: known.version_minor,
                title: known.title,
                players: known.players === null ? null : JSON.stringify(known.players),
                failure: known.failure,
                completed_at: known.completed_at,
              }
            : {}),
        })
        .onConflict((oc) =>
          oc.columns(['owner_account_id', 'blob_sha256', 'format', 'sim_version']).doNothing(),
        )
        .returningAll()
        .executeTakeFirst();
      if (!inserted) {
        const row = await db
          .selectFrom('map_uploads')
          .selectAll()
          .where('owner_account_id', '=', account.id)
          .where('blob_sha256', '=', key.blob)
          .where('format', '=', format)
          .where('sim_version', '=', sim)
          .executeTakeFirstOrThrow();
        return uploadView(row, stored.size);
      }
      let row: UploadRow = inserted;
      if (!known) {
        const jobId = await submitEngineJob(db, {
          kind: 'validate-map',
          simVersion,
          payload: { blobHash: key.blob, format },
        });
        row =
          (await db
            .updateTable('map_uploads')
            .set({ job_id: jobId })
            .where('id', '=', inserted.id)
            .returningAll()
            .executeTakeFirst()) ?? inserted;
      }
      return reply.status(201).send(uploadView(row, stored.size));
    },
  );

  app.get<{ Params: { id: string } }>('/api/v1/uploads/:id', async (request) => {
    const { account } = await requireAccount(identity, request);
    if (!UUID.test(request.params.id)) throw apiError('not_found', 'No such upload.');
    const row = await db
      .selectFrom('map_uploads as u')
      .innerJoin('blobs as b', 'b.sha256', 'u.blob_sha256')
      .selectAll('u')
      .select('b.size')
      .where('u.id', '=', request.params.id)
      .where('u.owner_account_id', '=', account.id)
      .executeTakeFirst();
    if (!row) throw apiError('not_found', 'No such upload.');
    return uploadView(row, Number(row.size));
  });

  // -------------------------------------------------------------- downloads

  /**
   * Map bytes by hash. Public blobs (generated maps) and versions of public
   * or unlisted catalog maps need no sign-in; a private upload or catalog map
   * is served to its owners (and, for catalog maps, moderators), to members
   * of a room using it and to participants of a match played on it.
   */
  app.get<{ Params: { hash: string } }>('/api/v1/blobs/maps/:hash', async (request, reply) => {
    const hash = request.params.hash;
    if (!SHA256.test(hash)) throw apiError('not_found', 'No such map.');
    const blob = await db
      .selectFrom('blobs')
      .selectAll()
      .where('sha256', '=', hash)
      .where('content_type', 'in', [MAP_CONTENT_TYPE, SAVE_CONTENT_TYPE])
      .executeTakeFirst();
    if (!blob) throw apiError('not_found', 'No such map.');
    // Catalog versions anyone may see (public and unlisted maps) need no sign-in.
    const openCatalog =
      blob.visibility !== 'public' && (await catalogAllowsMapBlob(db, hash, undefined));
    if (blob.visibility !== 'public' && !openCatalog) {
      const caller = await authenticate(identity, request);
      if (!caller) throw apiError('unauthenticated', 'Sign in to download this map.');
      const accountId = caller.account.id;
      const allowed =
        blob.owner_account_id === accountId ||
        (await catalogAllowsMapBlob(db, hash, { account: caller.account })) ||
        (await db
          .selectFrom('map_uploads')
          .select('id')
          .where('blob_sha256', '=', hash)
          .where('owner_account_id', '=', accountId)
          .executeTakeFirst()) !== undefined ||
        (await db
          .selectFrom('match_participants as p')
          .innerJoin('matches as m', 'm.id', 'p.match_id')
          .select('m.id')
          .where('p.account_id', '=', accountId)
          .where('m.map_hash', '=', hash)
          .executeTakeFirst()) !== undefined ||
        (await db
          .selectFrom('room_members as rm')
          .innerJoin('rooms as r', 'r.id', 'rm.room_id')
          .select('r.id')
          .where('rm.account_id', '=', accountId)
          .where('r.status', '!=', 'closed')
          .where(sql<string>`r.settings->'map'->>'hash'`, '=', hash)
          .executeTakeFirst()) !== undefined;
      if (!allowed) throw apiError('not_found', 'No such map.');
    }
    const stream = await blobs.get(blob.storage_key);
    if (!stream) throw apiError('not_found', 'The map file is missing from storage.');
    const size = await blobs.size(blob.storage_key);
    void reply
      .header('content-type', 'application/octet-stream')
      .header('etag', `"${hash}"`)
      .header(
        'cache-control',
        blob.visibility === 'public'
          ? 'public, max-age=31536000, immutable'
          : 'private, max-age=31536000, immutable',
      );
    if (size !== undefined) void reply.header('content-length', size);
    return reply.send(stream);
  });
}
