// The HTTP application, built from injected services so tests can run it
// against a test database without listening on a port.
//
// Route prefixes: /api/v1 (public REST), /realtime (WebSocket, M3), /internal
// (relays and agents, M4), /.well-known (JWKS, M3).
import Fastify, { type FastifyBaseLogger, type FastifyError, type FastifyInstance } from 'fastify';
import { sql, type Kysely } from 'kysely';
import type { AccessPolicy, BlobStore, JobQueue, Logger, PlatformConfig } from '@glob2/core';
import type { Database, PgPubSub } from '@glob2/db';
import {
  parseSimVersionKey,
  type ErrorBody,
  type InstanceInfo,
  type SimVersion,
} from '@glob2/protocol';

export interface ApiServices {
  config: PlatformConfig;
  logger: Logger;
  db: Kysely<Database>;
  pubsub: PgPubSub;
  jobs: JobQueue;
  blobs: BlobStore;
  access: AccessPolicy;
}

declare module 'fastify' {
  interface FastifyInstance {
    services: ApiServices;
  }
}

/** An engine agent counts as available if it was seen this recently. */
const AGENT_FRESHNESS_SECONDS = 300;

export class HttpError extends Error {
  readonly statusCode: number;
  readonly body: ErrorBody;
  constructor(statusCode: number, body: ErrorBody) {
    super(body.message);
    this.statusCode = statusCode;
    this.body = body;
  }
}

function errorBodyFor(error: FastifyError): { status: number; body: ErrorBody } {
  if (error instanceof HttpError) return { status: error.statusCode, body: error.body };
  if (error.validation) {
    return {
      status: 400,
      body: { code: 'bad_request', message: error.message, details: error.validation },
    };
  }
  if (error.statusCode === 429) {
    return { status: 429, body: { code: 'rate_limited', message: 'Too many requests.' } };
  }
  if (error.statusCode && error.statusCode >= 400 && error.statusCode < 500) {
    return { status: error.statusCode, body: { code: 'bad_request', message: error.message } };
  }
  return { status: 500, body: { code: 'internal', message: 'Internal server error.' } };
}

/** Sim versions with a recently seen engine agent: the versions this instance can serve. */
export async function supportedSimVersions(db: Kysely<Database>): Promise<SimVersion[]> {
  const rows = await db
    .selectFrom('engine_agents')
    .select('sim_version')
    .distinct()
    .where(
      'last_seen_at',
      '>',
      sql<Date>`now() - make_interval(secs => ${AGENT_FRESHNESS_SECONDS})`,
    )
    .orderBy('sim_version')
    .execute();
  return rows.flatMap((row) => parseSimVersionKey(row.sim_version) ?? []);
}

export async function buildApp(services: ApiServices): Promise<FastifyInstance> {
  const app = Fastify({
    loggerInstance: services.logger as FastifyBaseLogger,
    trustProxy: true,
    bodyLimit: 1024 * 1024,
    requestIdHeader: 'x-request-id',
  });
  app.decorate('services', services);

  app.setErrorHandler((error: FastifyError, request, reply) => {
    const { status, body } = errorBodyFor(error);
    if (status >= 500) request.log.error({ err: error }, 'request failed');
    void reply.status(status).send(body);
  });
  app.setNotFoundHandler((request, reply) => {
    const body: ErrorBody = {
      code: 'not_found',
      message: `No route for ${request.method} ${request.url}`,
    };
    void reply.status(404).send(body);
  });

  // Liveness: the process is up.
  app.get('/healthz', async () => ({ status: 'ok' }));

  // Readiness: the database answers. Load balancers route only to ready replicas.
  app.get('/readyz', async (_request, reply) => {
    try {
      await sql`SELECT 1`.execute(services.db);
      return { status: 'ready' };
    } catch (error) {
      services.logger.warn({ err: error }, 'readiness check failed');
      return reply.status(503).send({ status: 'unavailable' });
    }
  });

  app.get('/api/v1/instance', async (): Promise<InstanceInfo> => {
    const { config } = services;
    const origin = config.publicOrigin;
    return {
      name: config.instance.name,
      origin,
      realtimeUrl: `${origin.replace(/^http/, 'ws')}/realtime`,
      supportedSimVersions: await supportedSimVersions(services.db),
      authProviders: config.instance.auth.providers.map((provider) => ({
        id: provider.id,
        kind: provider.kind,
        displayName: provider.displayName,
      })),
      queues: config.instance.queues.map((queue) => ({
        id: queue.id,
        name: queue.name,
        mode: queue.mode,
        rated: queue.rated,
        ...(queue.aiBackfillSeconds === undefined
          ? {}
          : { aiBackfillSeconds: queue.aiBackfillSeconds }),
      })),
      guestsAllowed: config.instance.guests.enabled,
    };
  });

  return app;
}
