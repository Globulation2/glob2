// /internal/v1/engine: the engine agents' side of the platform. Agents run the
// legacy C++ loader on uploaded maps, saves and match records, so they get no
// database or blob-store credentials; this API is all they can reach, with a
// bearer agent key from ENGINE_AGENT_KEYS / ENGINE_AGENT_KEYS_FILE (a key
// written `<agentId>:<key>` acts only as that agent). Shapes are the protocol
// package's jobs.ts ("engine-agent HTTP API").
//
// What a key allows: announcing an agent, leasing jobs, and, for a job it
// holds a lease on, reading the blobs the job names, storing new blobs and
// reporting the result. Results are checked against the job kind's contract
// by apps/worker, as before.
import { createHash, timingSafeEqual } from 'node:crypto';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import { sql } from 'kysely';
import {
  extendEngineLease,
  leaseEngineJob,
  leasedJob,
  putContent,
  releaseEngineLease,
  reportEngineJob,
  contentKey,
  type EngineAgentKey,
} from '@glob2/core';
import {
  AiValidationReport,
  ENGINE_LEASE_HEADER,
  EngineAgentHeartbeat,
  EngineAgentId,
  EngineJobReport,
  EngineLeaseExtendRequest,
  EngineLeaseReleaseRequest,
  EngineLeaseRequest,
  schemaIssues,
  simVersionKey,
  type EngineBlobReceipt,
  type EngineJobReport as EngineJobReportType,
} from '@glob2/protocol';
import { apiError } from '../errors.ts';
import { body } from '../http/validate.ts';

const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;
const SHA256 = /^[0-9a-f]{64}$/;

/** Content types an agent may store (engine-agent blobs.ts CONTENT_TYPES). */
const AGENT_CONTENT_TYPES = new Set([
  'application/x-glob2-map',
  'application/x-glob2-generator',
  'application/x-glob2-save',
  'application/x-glob2-match-record',
  'application/x-glob2-replay',
  'application/json',
  'image/png',
]);

const digest = (value: string) => createHash('sha256').update(value).digest();

/** Finds the configured agent key the request presents (constant-time compare). */
export function matchAgentKey(
  keys: readonly EngineAgentKey[],
  presented: string,
): EngineAgentKey | undefined {
  const candidate = digest(presented);
  let found: EngineAgentKey | undefined;
  for (const key of keys) {
    if (timingSafeEqual(digest(key.key), candidate) && !found) found = key;
  }
  return found;
}

export async function engineAgentRoutes(app: FastifyInstance): Promise<void> {
  const { db, blobs, config } = app.services;
  const keys = config.engineAgentKeys ?? [];
  const maxBlobBytes = config.recordMaxBytes ?? 64 * 1024 * 1024;

  function agentKey(request: FastifyRequest): EngineAgentKey {
    const header = request.headers.authorization;
    const token = header ? /^Bearer\s+(\S+)$/i.exec(header)?.[1] : undefined;
    if (!token) throw apiError('unauthenticated', 'Engine agent key required.');
    const key = matchAgentKey(keys, token);
    if (!key) throw apiError('unauthenticated', 'Unknown engine agent key.');
    return key;
  }

  function actAs(key: EngineAgentKey, agentId: string): void {
    if (key.agentId && key.agentId !== agentId) {
      throw apiError('forbidden', 'This key belongs to engine agent {p0}.', undefined, {
        p0: String(key.agentId),
      });
    }
  }

  function leaseToken(request: FastifyRequest): string {
    const value = request.headers[ENGINE_LEASE_HEADER];
    if (typeof value !== 'string' || !/^[A-Za-z0-9_-]{32,128}$/.test(value)) {
      throw apiError('unauthenticated', "Send the job's lease token in {p0}.", undefined, {
        p0: String(ENGINE_LEASE_HEADER),
      });
    }
    return value;
  }

  function jobId(request: FastifyRequest<{ Params: { jobId: string } }>): string {
    if (!UUID.test(request.params.jobId)) throw apiError('not_found', 'No such job.');
    return request.params.jobId;
  }

  /** The job the request's lease currently holds, with a pinned key's agent check. */
  async function heldJob(request: FastifyRequest, key: EngineAgentKey, id?: string) {
    const job = await leasedJob(db, leaseToken(request), id);
    if (!job) throw apiError('conflict', 'The lease expired or the job was already reported.');
    if (key.agentId && job.leased_by !== key.agentId) {
      throw apiError('forbidden', 'The job is leased by another engine agent.');
    }
    return job;
  }

  const internal = { config: { rateLimit: false } } as const;

  app.post('/internal/v1/engine/ai-validation-progress', internal, async (request, reply) => {
    const job = await heldJob(request, agentKey(request));
    const report = body(AiValidationReport, request.body);
    const payload = job.payload as { blobHash?: string; suite?: number };
    if (
      job.kind !== 'validate-ai' ||
      report.valid ||
      report.sourceHash !== payload.blobHash ||
      report.suite !== payload.suite ||
      report.simVersion !== job.sim_version
    )
      throw apiError('bad_request', 'Progress does not match the leased AI validation.');
    await db
      .updateTable('ai_validations')
      .set({ report: JSON.stringify(report) })
      .where('job_id', '=', job.id)
      .where('status', '=', 'pending')
      .execute();
    return reply.status(204).send();
  });

  app.post('/internal/v1/engine/agents/heartbeat', internal, async (request, reply) => {
    const key = agentKey(request);
    const beat = body(EngineAgentHeartbeat, request.body);
    actAs(key, beat.agentId);
    const version = simVersionKey(beat.simVersion);
    await db
      .insertInto('engine_agents')
      .values({
        id: beat.agentId,
        sim_version: version,
        kinds: beat.kinds,
        build: beat.build,
        building_catalog_hash: beat.buildingCatalogHash ?? null,
      })
      .onConflict((conflict) =>
        conflict.column('id').doUpdateSet({
          sim_version: version,
          building_catalog_hash: beat.buildingCatalogHash ?? null,
          kinds: beat.kinds,
          build: beat.build,
          last_seen_at: sql<Date>`now()`,
        }),
      )
      .execute();
    return reply.status(204).send();
  });

  app.delete<{ Params: { agentId: string } }>(
    '/internal/v1/engine/agents/:agentId',
    internal,
    async (request, reply) => {
      const key = agentKey(request);
      const id = request.params.agentId;
      if (schemaIssues(EngineAgentId, id).length > 0) throw apiError('not_found', 'No such agent.');
      actAs(key, id);
      await db.deleteFrom('engine_agents').where('id', '=', id).execute();
      return reply.status(204).send();
    },
  );

  app.post('/internal/v1/engine/jobs/lease', internal, async (request, reply) => {
    const key = agentKey(request);
    const lease = body(EngineLeaseRequest, request.body);
    actAs(key, lease.agentId);
    const granted = await leaseEngineJob(db, lease);
    if (!granted) return reply.status(204).send();
    request.log.info(
      {
        job: granted.job.jobId,
        kind: granted.job.kind,
        agent: lease.agentId,
        attempt: granted.attempt,
      },
      'engine job leased',
    );
    return granted;
  });

  app.post<{ Params: { jobId: string } }>(
    '/internal/v1/engine/jobs/:jobId/extend',
    internal,
    async (request, reply) => {
      const key = agentKey(request);
      const id = jobId(request);
      const { leaseSeconds } = body(EngineLeaseExtendRequest, request.body);
      await heldJob(request, key, id);
      if (!(await extendEngineLease(db, id, leaseToken(request), leaseSeconds))) {
        throw apiError('conflict', 'The lease expired or the job was already reported.');
      }
      return reply.status(204).send();
    },
  );

  app.post<{ Params: { jobId: string } }>(
    '/internal/v1/engine/jobs/:jobId/release',
    internal,
    async (request, reply) => {
      const key = agentKey(request);
      const id = jobId(request);
      const { retryAfterSeconds } = body(EngineLeaseReleaseRequest, request.body);
      await heldJob(request, key, id);
      await releaseEngineLease(db, id, leaseToken(request), retryAfterSeconds);
      return reply.status(204).send();
    },
  );

  app.post<{ Params: { jobId: string } }>(
    '/internal/v1/engine/jobs/:jobId/result',
    { ...internal, bodyLimit: 32 * 1024 * 1024 },
    async (request, reply) => {
      const key = agentKey(request);
      const id = jobId(request);
      const report = body(EngineJobReport, request.body) as EngineJobReportType;
      const token = leaseToken(request);
      if (key.agentId) {
        const holder = await db
          .selectFrom('engine_jobs')
          .select('leased_by')
          .where('id', '=', id)
          .executeTakeFirst();
        if (holder && holder.leased_by !== key.agentId) {
          throw apiError('forbidden', 'The job is leased by another engine agent.');
        }
      }
      const outcome = await reportEngineJob(db, id, token, report);
      if (outcome === 'lost') {
        throw apiError('conflict', 'The lease expired or the job was already reported.');
      }
      if (outcome === 'accepted') {
        request.log.info({ job: id, ok: report.ok }, 'engine job reported');
      }
      return reply.status(204).send();
    },
  );

  // A blob the leased job names (its payload holds the hash): input maps,
  // saves and match records. Nothing else is readable with a lease.
  app.get<{ Params: { sha256: string } }>(
    '/internal/v1/engine/blobs/:sha256',
    internal,
    async (request, reply) => {
      const key = agentKey(request);
      const hash = request.params.sha256;
      if (!SHA256.test(hash)) throw apiError('not_found', 'No such blob.');
      const job = await heldJob(request, key);
      const named = await db
        .selectFrom('engine_jobs')
        .select('id')
        .where('id', '=', job.id)
        .where(sql<boolean>`strpos(payload::text, ${hash}) > 0`)
        .executeTakeFirst();
      if (!named) throw apiError('forbidden', 'The leased job does not use this blob.');
      const storageKey = contentKey(hash);
      const size = await blobs.size(storageKey);
      const stream = size === undefined ? undefined : await blobs.get(storageKey);
      if (size === undefined || !stream) throw apiError('not_found', 'No such blob.');
      return reply
        .header('content-type', 'application/octet-stream')
        .header('content-length', String(size))
        .send(stream);
    },
  );

  // Stores an engine output (generated map, preview, replay, result) by content.
  app.put<{ Querystring: { contentType?: string; visibility?: string } }>(
    '/internal/v1/engine/blobs',
    { ...internal, bodyLimit: maxBlobBytes },
    async (request, reply): Promise<EngineBlobReceipt> => {
      const key = agentKey(request);
      await heldJob(request, key);
      const contentType = request.query.contentType ?? '';
      if (!AGENT_CONTENT_TYPES.has(contentType)) {
        throw apiError('bad_request', 'Unknown contentType.');
      }
      const visibility = request.query.visibility === 'public' ? 'public' : 'private';
      const bytes = request.body;
      if (!(bytes instanceof Buffer) || bytes.length === 0) {
        throw apiError('bad_request', 'Send the blob as the body.');
      }
      const { sha256, size, key: storageKey } = await putContent(blobs, bytes);
      await db
        .insertInto('blobs')
        .values({ sha256, size, content_type: contentType, storage_key: storageKey, visibility })
        .onConflict((oc) => oc.column('sha256').doNothing())
        .execute();
      reply.status(201);
      return { sha256, size };
    },
  );
}
