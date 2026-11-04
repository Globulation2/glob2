import { isDeepStrictEqual } from 'node:util';
import { sql } from 'kysely';
import { Type } from 'typebox';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import {
  GeneratorStudioRun,
  GeneratorStudioSettings,
  Strict,
  Uuid,
  generatorPackage,
  parseSimVersionKey,
} from '@glob2/protocol';
import { putContent, ensureGeneratorValidation } from '@glob2/core';
import { insertBlob } from '@glob2/play';
import type { StudioStore } from '../coding-studio/store.ts';
import { requireAccount } from '../identity.ts';
import { body } from '../http/validate.ts';
import { type SharedLimit, enforce } from '../http/rateLimits.ts';
import { apiError } from '../errors.ts';
import { HiveError } from '@glob2/billing';

export async function generatorToolsRoutes(
  app: FastifyInstance,
  store: StudioStore,
  checks: SharedLimit,
) {
  const ROOT = '/api/v1/generator-studio/projects/:id';
  const { db, blobs } = app.services;
  const revisionOf = async (id: string, revision: number, tx = db) => {
    try {
      return await store.revision(id, revision, tx);
    } catch (e) {
      if (e instanceof HiveError) throw apiError('not_found', e.message);
      throw e;
    }
  };
  const owner = async (r: FastifyRequest) => {
    const { account } = await requireAccount(app.identity, r);
    if (!app.services.config.instance.generatorStudio?.enabled)
      throw apiError('not_found', 'Generator Studio is not enabled on this instance.');
    const id = body(Strict({ id: Uuid }), r.params).id;
    try {
      await store.project(id, account.id);
    } catch (e) {
      if (e instanceof HiveError) throw apiError('not_found', e.message);
      throw e;
    }
    return { id, account };
  };
  app.post(ROOT + '/check', async (r, reply) => {
    const { id, account } = await owner(r);
    await enforce(checks, account.id, reply, 'Too many checks; please wait.');
    const input = body(
      Strict({ revision: Type.Integer({ minimum: 1 }), settings: GeneratorStudioSettings }),
      r.body,
    );
    const revision = await revisionOf(id, input.revision);
    let bytes: Buffer;
    try {
      bytes = Buffer.from(generatorPackage(revision.source));
    } catch (e) {
      throw apiError('bad_request', e instanceof Error ? e.message : 'Invalid manifest.');
    }
    const agents = await db
      .selectFrom('engine_agents')
      .select('sim_version')
      .where('last_seen_at', '>', new Date(Date.now() - 120000))
      .where(sql<boolean>`'validate-generator'=ANY(kinds)`)
      .execute();
    const sim = agents
      .map((x) => parseSimVersionKey(x.sim_version))
      .filter((x) => !!x)
      .sort((a, b) => b.versionMinor - a.versionMinor || b.netProtocol - a.netProtocol)[0];
    if (!sim) throw apiError('unavailable', 'No isolated generator validator is available.');
    const stored = await putContent(blobs, bytes);
    return db.transaction().execute(async (tx) => {
      const a = await tx
        .selectFrom('accounts')
        .select('status')
        .where('id', '=', account.id)
        .forUpdate()
        .executeTakeFirst();
      if (a?.status !== 'active') throw apiError('not_found', 'No active account.');
      await store.project(id, account.id, tx, true);
      const pending = await tx
        .selectFrom('generator_uploads as u')
        .innerJoin('generator_validations as v', 'v.id', 'u.validation_id')
        .select('v.id')
        .where('u.owner_account_id', '=', account.id)
        .where('v.status', '=', 'pending')
        .execute();
      if (pending.length >= 2) throw apiError('conflict', 'Wait for a pending generator check.');
      await insertBlob(tx, stored.sha256, stored.size, 'application/json', 'private', account.id);
      const validation_id = await ensureGeneratorValidation(
        tx,
        stored.sha256,
        sim,
        input.settings,
        true,
      );
      const upload = await tx
        .insertInto('generator_uploads')
        .values({ owner_account_id: account.id, validation_id })
        .returning('id')
        .executeTakeFirstOrThrow();
      await tx
        .insertInto('generator_studio_checks')
        .values({ project_id: id, revision: input.revision, upload_id: upload.id })
        .execute();
      await store.event(tx, id, 'validation', { revision: input.revision, uploadId: upload.id });
      return { uploadId: upload.id, revision: input.revision };
    });
  });
  app.get(ROOT + '/checks', async (r) => {
    const { id, account } = await owner(r);
    return {
      items: (
        await sql`SELECT c.revision,r.hash AS draft_hash,v.report,v.status,v.error,v.created_at,u.id AS upload_id,u.expires_at FROM generator_studio_checks c JOIN generator_studio_revisions r ON r.project_id=c.project_id AND r.revision=c.revision JOIN generator_uploads u ON u.id=c.upload_id AND u.owner_account_id=${account.id} JOIN generator_validations v ON v.id=u.validation_id WHERE c.project_id=${id} ORDER BY v.created_at DESC,c.revision DESC LIMIT 200`.execute(
          db,
        )
      ).rows,
    };
  });
  app.post(ROOT + '/runs', async (r) => {
    const { id, account } = await owner(r),
      v = body(GeneratorStudioRun, r.body);
    return db.transaction().execute(async (tx) => {
      await store.project(id, account.id, tx, true);
      const revision = await revisionOf(id, v.expectedRevision, tx);
      let bytes: string;
      try {
        bytes = generatorPackage(revision.source);
      } catch (e) {
        throw apiError('bad_request', e instanceof Error ? e.message : 'Invalid manifest.');
      }
      const prior = await tx
        .selectFrom('generator_studio_runs')
        .selectAll()
        .where('id', '=', v.id)
        .executeTakeFirst();
      if (
        prior &&
        (prior.project_id !== id ||
          prior.revision !== v.expectedRevision ||
          !isDeepStrictEqual(prior.settings, v.settings))
      )
        throw apiError('conflict', 'Run identifier already used.');
      await tx
        .insertInto('generator_studio_runs')
        .values({
          id: v.id,
          project_id: id,
          revision: v.expectedRevision,
          settings: JSON.stringify(v.settings),
          source_hash: revision.hash,
        })
        .onConflict((oc) => oc.doNothing())
        .execute();
      await store.event(tx, id, 'preview', { id: v.id, revision: v.expectedRevision });
      return {
        runId: v.id,
        revision: v.expectedRevision,
        source: bytes,
        draftHash: revision.hash,
        settings: v.settings,
      };
    });
  });
  app.post(ROOT + '/run-result', async (r) => {
    const { id } = await owner(r),
      v = body(Strict({ runId: Uuid, summary: Type.String({ maxLength: 16000 }) }), r.body);
    await db
      .updateTable('generator_studio_runs')
      .set({ summary: v.summary.replaceAll('\0', '') })
      .where('id', '=', v.runId)
      .where('project_id', '=', id)
      .execute();
    return { saved: true };
  });
}
