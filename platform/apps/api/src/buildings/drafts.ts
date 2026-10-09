import { BuildingAiStudio } from '@glob2/building-studio';
import { HiveError } from '@glob2/billing';
import { randomUUID } from 'node:crypto';
import { sql } from 'kysely';
import type { FastifyInstance } from 'fastify';
import sharp from 'sharp';
import {
  BUILDING_PACKAGE_LIMITS,
  SaveBuildingDraftRequest,
  buildingNamespacePrefix,
  checkBuildingPackage,
  type BuildingDraft,
  type BuildingDraftList,
} from '@glob2/protocol';
import {
  buildingAssetHash,
  buildingPackageAssetHashes,
  readBuildingArchive,
  writeBuildingArchive,
} from '@glob2/protocol/node';
import { authenticate, requireAccount, type Identity } from '../identity.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce } from '../http/rateLimits.ts';
import { apiError } from '../errors.ts';
import { normalizeBuildingArtwork } from './images.ts';
import { UUID } from '../maps/catalog.ts';

/** Private authoring workspace. Engine validation is a separate publication boundary. */
export async function buildingDraftRoutes(app: FastifyInstance, identity: Identity) {
  const { db } = app.services;
  const saves = new SharedLimit(db, 'building-draft', 60, 3600000);
  const signed = async (request: Parameters<typeof requireAccount>[1]) => {
    const { account } = await requireAccount(identity, request);
    if (account.kind !== 'registered' || account.status !== 'active')
      throw apiError('forbidden', 'Sign in with an active account to save building drafts.');
    return account;
  };
  const checked = <T>(operation: () => T): T => {
    try {
      return operation();
    } catch (error) {
      throw apiError(
        'bad_request',
        error instanceof Error ? error.message : 'Invalid building package.',
      );
    }
  };
  async function owned(id: string, accountId: string) {
    if (!UUID.test(id)) throw apiError('not_found', 'No such draft.');
    const row = await db
      .selectFrom('building_drafts')
      .selectAll()
      .where('id', '=', id)
      .where('owner_account_id', '=', accountId)
      .executeTakeFirst();
    if (!row) throw apiError('not_found', 'No such draft.');
    return row;
  }
  const view = (row: Awaited<ReturnType<typeof owned>>): BuildingDraft => ({
    id: row.id,
    revision: row.revision,
    name: row.name,
    updatedAt: row.updated_at.toISOString(),
    package: readBuildingArchive(row.archive).package,
  });
  async function replace(
    row: Awaited<ReturnType<typeof owned>>,
    revision: string,
    archive: Buffer,
    name = row.name,
  ) {
    return db.transaction().execute(async (trx) => {
      const account = await trx
        .selectFrom('accounts')
        .select(['kind', 'status'])
        .where('id', '=', row.owner_account_id)
        .forUpdate()
        .executeTakeFirstOrThrow();
      if (account.kind !== 'registered' || account.status !== 'active')
        throw apiError('forbidden', 'Sign in with an active account to save building drafts.');
      const current = await trx
        .selectFrom('building_drafts')
        .select('revision')
        .select(sql<number>`octet_length(archive)`.as('bytes'))
        .where('id', '=', row.id)
        .where('owner_account_id', '=', row.owner_account_id)
        .forUpdate()
        .executeTakeFirst();
      if (!current || current.revision !== revision)
        throw apiError('conflict', 'This draft changed. Reload it before saving again.');
      const used = await trx
        .selectFrom('building_drafts')
        .select(sql<string>`coalesce(sum(octet_length(archive)),0)::bigint`.as('bytes'))
        .where('owner_account_id', '=', row.owner_account_id)
        .executeTakeFirstOrThrow();
      if (Number(used.bytes) - current.bytes + archive.length > 64 * 1024 * 1024)
        throw apiError(
          'conflict',
          'Your building workspace is limited to 64 MiB. Export and delete an old draft first.',
        );
      const updated = await trx
        .updateTable('building_drafts')
        .set({ revision: randomUUID(), archive, name, updated_at: new Date() })
        .where('id', '=', row.id)
        .where('owner_account_id', '=', row.owner_account_id)
        .where('revision', '=', revision)
        .returningAll()
        .executeTakeFirstOrThrow();
      return view(updated);
    });
  }
  app.get('/api/v1/building-drafts', async (r, reply): Promise<BuildingDraftList> => {
    const account = await signed(r);
    reply.header('cache-control', 'private, no-store');
    const rows = await db
      .selectFrom('building_drafts')
      .select(['id', 'revision', 'name', 'updated_at'])
      .where('owner_account_id', '=', account.id)
      .orderBy('updated_at', 'desc')
      .limit(100)
      .execute();
    return {
      items: rows.map((row) => ({
        id: row.id,
        revision: row.revision,
        name: row.name,
        updatedAt: row.updated_at.toISOString(),
      })),
    };
  });
  app.post('/api/v1/building-drafts', async (r, reply) => {
    const account = await signed(r);
    await enforce(saves, account.id, reply, 'Too many draft saves.');
    // The account lock also serializes first saves and the workspace quota.
    const row = await db.transaction().execute(async (trx) => {
      const eligibility = await trx
        .selectFrom('accounts')
        .select(['id', 'kind', 'status'])
        .where('id', '=', account.id)
        .forUpdate()
        .executeTakeFirstOrThrow();
      if (eligibility.kind !== 'registered' || eligibility.status !== 'active')
        throw apiError('forbidden', 'Sign in with an active account to save building drafts.');
      const drafts = await trx
        .selectFrom('building_drafts')
        .select('id')
        .where('owner_account_id', '=', account.id)
        .limit(100)
        .execute();
      if (drafts.length >= 100)
        throw apiError('conflict', 'Your workspace has 100 drafts. Delete an old draft first.');
      const namespace = randomUUID();
      const pkg = {
        schemaVersion: 1,
        namespace,
        experiments: [],
        sprites: [],
        variants: [
          {
            key: buildingNamespacePrefix(namespace) + 'building',
            properties: {
              width: 2,
              height: 2,
              hpInit: 200,
              hpMax: 200,
              gameSprite: 'data/gfx/inn0b',
              miniSprite: 'data/gfx/miniinn0b',
            },
            semantics: { placeable: true, instantPlacement: true },
          },
        ],
      };
      const archive = writeBuildingArchive(pkg, new Map());
      const used = await trx
        .selectFrom('building_drafts')
        .select(sql<string>`coalesce(sum(octet_length(archive)),0)::bigint`.as('bytes'))
        .where('owner_account_id', '=', account.id)
        .executeTakeFirstOrThrow();
      if (Number(used.bytes) + archive.length > 64 * 1024 * 1024)
        throw apiError(
          'conflict',
          'Your building workspace is limited to 64 MiB. Export and delete an old draft first.',
        );
      return trx
        .insertInto('building_drafts')
        .values({
          owner_account_id: account.id,
          name: 'New building family',
          archive,
        })
        .returningAll()
        .executeTakeFirstOrThrow();
    });
    return reply.status(201).send(view(row));
  });
  app.get<{ Params: { id: string } }>('/api/v1/building-drafts/:id', async (r, reply) => {
    reply.header('cache-control', 'private, no-store');
    return view(await owned(r.params.id, (await signed(r)).id));
  });
  app.put<{ Params: { id: string } }>(
    '/api/v1/building-drafts/:id',
    { bodyLimit: 9 * 1024 * 1024 },
    async (r, reply) => {
      const account = await signed(r),
        input = body(SaveBuildingDraftRequest, r.body);
      await enforce(saves, account.id, reply, 'Too many draft saves.');
      const row = await owned(r.params.id, account.id);
      if (!input.name.trim()) throw apiError('bad_request', 'Choose a family name.');
      const old = readBuildingArchive(row.archive);
      const pkg = checked(() => checkBuildingPackage(input.package));
      if (pkg.namespace !== old.package.namespace)
        throw apiError(
          'bad_request',
          'Keep this family namespace. Import a fork to create a different family.',
        );
      const assets = new Map(
        [...old.assets].filter(([hash]) => buildingPackageAssetHashes(pkg).has(hash)),
      );
      const archive = checked(() => writeBuildingArchive(pkg, assets));
      return replace(row, input.revision, archive, input.name.trim());
    },
  );
  app.put<{ Params: { id: string }; Querystring: { revision?: string } }>(
    '/api/v1/building-drafts/:id/archive',
    { bodyLimit: BUILDING_PACKAGE_LIMITS.uploadBytes },
    async (r, reply) => {
      const account = await signed(r);
      await enforce(saves, account.id, reply, 'Too many draft saves.');
      const row = await owned(r.params.id, account.id);
      if (!r.query.revision || !UUID.test(r.query.revision))
        throw apiError('bad_request', 'Choose the current draft revision.');
      if (!Buffer.isBuffer(r.body)) throw apiError('bad_request', 'Choose a building ZIP package.');
      const imported = checked(() => readBuildingArchive(r.body as Buffer));
      let normalized;
      try {
        normalized = await normalizeBuildingArtwork(imported.package, imported.assets);
      } catch (error) {
        throw apiError('bad_request', error instanceof Error ? error.message : 'Invalid artwork.');
      }
      return replace(
        row,
        r.query.revision,
        checked(() => writeBuildingArchive(normalized.package, normalized.assets)),
      );
    },
  );
  app.get<{ Params: { id: string } }>('/api/v1/building-drafts/:id/archive', async (r, reply) => {
    const row = await owned(r.params.id, (await signed(r)).id);
    return reply
      .header('content-type', 'application/octet-stream')
      .header('content-disposition', 'attachment; filename="building-family.zip"')
      .header('cache-control', 'private, no-store')
      .header('x-content-type-options', 'nosniff')
      .send(row.archive);
  });
  app.put<{
    Params: { id: string };
    Querystring: { revision?: string; sprite?: string; frame?: string; layer?: string };
  }>(
    '/api/v1/building-drafts/:id/frame',
    { bodyLimit: BUILDING_PACKAGE_LIMITS.uploadBytes },
    async (r, reply) => {
      const account = await signed(r);
      await enforce(saves, account.id, reply, 'Too many artwork uploads.');
      const row = await owned(r.params.id, account.id),
        q = r.query;
      if (
        !q.revision ||
        !UUID.test(q.revision) ||
        !q.sprite ||
        !/^[a-z0-9][a-z0-9._-]{0,63}$/.test(q.sprite) ||
        !q.frame ||
        !/^(0|[1-9][0-9]{0,2})$/.test(q.frame) ||
        !['image', 'team'].includes(q.layer ?? 'image')
      )
        throw apiError('bad_request', 'Choose a sprite, frame, layer and current revision.');
      if (!Buffer.isBuffer(r.body) || !r.body.length)
        throw apiError('bad_request', 'Choose a PNG or WebP frame.');
      const imported = readBuildingArchive(row.archive);
      let sprite = imported.package.sprites.find((s) => s.key === q.sprite);
      const index = Number(q.frame);
      if (!sprite) {
        if (index !== 0 || q.layer === 'team')
          throw apiError('bad_request', 'Add the base frame first.');
        sprite = { key: q.sprite, frames: [] };
        imported.package.sprites.push(sprite);
      }
      if (index > sprite.frames.length || index >= BUILDING_PACKAGE_LIMITS.images)
        throw apiError('bad_request', 'Add frames in order.');
      let info;
      try {
        info = await sharp(r.body, { limitInputPixels: 512 ** 2, failOn: 'warning' }).metadata();
      } catch {
        throw apiError(
          'bad_request',
          'Choose a still PNG or WebP frame no larger than 512 by 512.',
        );
      }
      if (
        !info.width ||
        !info.height ||
        !['png', 'webp'].includes(info.format ?? '') ||
        (info.pages ?? 1) !== 1
      )
        throw apiError('bad_request', 'Choose a still PNG or WebP frame.');
      const hash = buildingAssetHash(r.body),
        old = sprite.frames[index];
      if (q.layer === 'team') {
        if (!old || old.width !== info.width || old.height !== info.height)
          throw apiError('bad_request', 'Team color layers must match the base frame dimensions.');
        old.teamColorHash = hash;
      } else {
        if (old?.teamColorHash && (old.width !== info.width || old.height !== info.height))
          throw apiError(
            'bad_request',
            'Replace or remove the team layer before changing frame dimensions.',
          );
        sprite.frames[index] = { ...old, width: info.width, height: info.height, imageHash: hash };
      }
      imported.assets.set(hash, r.body);
      const required = buildingPackageAssetHashes(imported.package);
      const selected = new Map([...imported.assets].filter(([h]) => required.has(h)));
      let normalized;
      try {
        normalized = await normalizeBuildingArtwork(imported.package, selected);
      } catch (error) {
        throw apiError('bad_request', error instanceof Error ? error.message : 'Invalid artwork.');
      }
      return replace(
        row,
        q.revision,
        checked(() => writeBuildingArchive(normalized.package, normalized.assets)),
      );
    },
  );
  app.get<{ Params: { id: string; hash: string } }>(
    '/api/v1/building-drafts/:id/assets/:hash',
    async (r, reply) => {
      const account = (await authenticate(identity, r))?.account;
      if (!account) throw apiError('unauthenticated', 'Sign in to view draft artwork.');
      const row = await owned(r.params.id, account.id),
        asset = readBuildingArchive(row.archive).assets.get(r.params.hash);
      if (!asset) throw apiError('not_found', 'No such artwork.');
      return reply
        .header('content-type', 'image/webp')
        .header('cache-control', 'private, no-store')
        .header('x-content-type-options', 'nosniff')
        .send(asset);
    },
  );
  app.delete<{ Params: { id: string } }>('/api/v1/building-drafts/:id', async (r, reply) => {
    const row = await owned(r.params.id, (await signed(r)).id);
    try {
      await new BuildingAiStudio(db).removeDraft(row.owner_account_id, row.id);
    } catch (error) {
      if (error instanceof HiveError)
        throw apiError(error.code === 'conflict' ? 'conflict' : 'not_found', error.message);
      throw error;
    }
    return reply.status(204).send();
  });
}
