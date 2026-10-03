import { hiveRoutes } from './hive/routes.ts';
// The HTTP application, built from injected services so tests can run it
// against a test database without listening on a port.
//
// Route prefixes: /api/v1 (public REST), /realtime (WebSocket), /internal
// (relays), /.well-known (JWKS, mobile app-link files), the browser sign-in pages /signin and
// /auth/<provider>/… (served here, so they share the API's origin and
// cookies), and invite landing pages /j/<code>.
import Fastify, { type FastifyBaseLogger, type FastifyError, type FastifyInstance } from 'fastify';
import cookie from '@fastify/cookie';
import formbody from '@fastify/formbody';
import rateLimit from '@fastify/rate-limit';
import websocket from '@fastify/websocket';
import { sql, type Kysely } from 'kysely';
import { readableSize, resolveQueue } from '@glob2/core';
import type { Database } from '@glob2/db';
import {
  MATCH_RECORD_CONTENT_TYPE,
  parseSimVersionKey,
  type ErrorBody,
  type InstanceInfo,
  type SimVersion,
} from '@glob2/protocol';
import { HttpError, apiError } from './errors.ts';
import { createIdentity, type Identity } from './identity.ts';
import type { ApiServices } from './services.ts';
import { accountRoutes } from './routes/accounts.ts';
import { adminRoutes } from './routes/admin.ts';
import { authRoutes } from './routes/auth.ts';
import { signinRoutes } from './routes/signin.ts';
import { internalRoutes } from './routes/internal.ts';
import { inviteRoutes } from './routes/invite.ts';
import { playRoutes } from './routes/play.ts';
import { mapCatalogRoutes } from './maps/routes.ts';
import { historyRoutes } from './history/routes.ts';
import { appLinkRoutes } from './web/appLinks.ts';
import { pageAssetRoutes } from './web/pages.ts';
import { Assignments } from './play/assignments.ts';
import { PlayRealtime } from './play/realtime.ts';
import { RoomService } from './play/rooms.ts';
import { MAX_FRAME_BYTES, realtimeRoutes, type RealtimeOptions } from './realtime/server.ts';

export type { ApiServices } from './services.ts';
export { HttpError } from './errors.ts';

declare module 'fastify' {
  interface FastifyInstance {
    services: ApiServices;
    identity: Identity;
  }
}

export interface BuildOptions {
  realtime?: RealtimeOptions;
  /** Room sweep interval in ms (default 30 s; 0 disables, for tests). */
  roomSweepMs?: number;
}

/** An engine agent counts as available if it was seen this recently. */
const AGENT_FRESHNESS_SECONDS = 300;

function errorBodyFor(
  error: FastifyError,
  uploadMaxBytes: number,
): { status: number; body: ErrorBody } {
  if (error instanceof HttpError) return { status: error.statusCode, body: error.body };
  if (error.statusCode === 413) {
    // Shown to players by the web app and the game (map and save uploads).
    return {
      status: 413,
      body: {
        code: 'bad_request',
        message: `This file is too big: uploads are limited to ${readableSize(uploadMaxBytes)}.`,
        details: { problem: 'too_large' },
      },
    };
  }
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

export async function buildApp(
  services: ApiServices,
  options: BuildOptions = {},
): Promise<FastifyInstance> {
  const app = Fastify({
    loggerInstance: services.logger as FastifyBaseLogger,
    trustProxy: true,
    bodyLimit: 1024 * 1024,
    requestIdHeader: 'x-request-id',
  });
  const identity = createIdentity(services);
  app.decorate('services', services);
  app.decorate('identity', identity);

  app.setErrorHandler((error: FastifyError, request, reply) => {
    const { status, body } = errorBodyFor(
      error,
      services.config.uploadMaxBytes ?? 16 * 1024 * 1024,
    );
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

  await app.register(cookie);
  await app.register(formbody, { bodyLimit: 64 * 1024 });
  // Per-replica, per-address limits; auth routes set tighter ones of their own.
  await app.register(rateLimit, {
    global: true,
    max: identity.limits.apiPerMinute,
    timeWindow: 60_000,
    errorResponseBuilder: () => apiError('rate_limited', 'Too many requests.'),
  });
  await app.register(websocket, { options: { maxPayload: MAX_FRAME_BYTES } });
  // Raw bodies: uploaded maps and saves, relay match records. Routes set
  // their own size limits.
  app.addContentTypeParser(
    ['application/octet-stream', MATCH_RECORD_CONTENT_TYPE],
    {
      parseAs: 'buffer',
      bodyLimit: Math.max(
        services.config.uploadMaxBytes ?? 16 * 1024 * 1024,
        services.config.recordMaxBytes ?? 64 * 1024 * 1024,
      ),
    },
    (_request, payload, done) => done(null, payload),
  );

  // Liveness: the process is up.
  app.get('/healthz', { config: { rateLimit: false } }, async () => ({ status: 'ok' }));

  // Readiness: the database answers. Load balancers route only to ready replicas.
  app.get('/readyz', { config: { rateLimit: false } }, async (_request, reply) => {
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
      authProviders: [
        ...identity.providers.list().map((provider) => ({
          id: provider.id,
          kind: provider.kind,
          displayName: provider.displayName,
        })),
        ...(identity.localAuth.enabled
          ? [{ id: 'local', kind: 'local' as const, displayName: 'Username and password' }]
          : []),
      ],
      queues: config.instance.queues
        .map((q) => resolveQueue(q))
        .map((queue) => ({
          id: queue.id,
          name: queue.name,
          mode: queue.mode,
          rated: queue.rated,
          ...(queue.aiBackfillSeconds === undefined
            ? {}
            : { aiBackfillSeconds: queue.aiBackfillSeconds }),
          acceptSeconds: queue.acceptSeconds,
          maps: [...new Set(queue.mapPool.map((entry) => entry.generatorId))].slice(0, 64),
        })),
      guestsAllowed: config.instance.guests.enabled,
    };
  });

  const rooms = new RoomService({
    db: services.db,
    jobs: services.jobs,
    access: services.access,
    origin: services.config.publicOrigin,
    logger: services.logger,
  });
  const assignments = new Assignments(services.db, identity.keys, services.config.publicOrigin);
  const play = new PlayRealtime({
    config: services.config,
    access: services.access,
    pubsub: services.pubsub,
    hub: identity.hub,
    rooms,
    assignments,
    logger: services.logger,
    ...(options.roomSweepMs === undefined ? {} : { sweepMs: options.roomSweepMs }),
  });

  await authRoutes(app, identity);
  await accountRoutes(app, identity);
  await hiveRoutes(app);
  await adminRoutes(app, identity);
  await pageAssetRoutes(app);
  await signinRoutes(app, identity);
  await playRoutes(app, identity, rooms);
  await mapCatalogRoutes(app, identity);
  await historyRoutes(app, identity);
  await appLinkRoutes(app);
  await inviteRoutes(app, rooms);
  await internalRoutes(app);
  await app.register(async (scope) =>
    realtimeRoutes(scope, identity, options.realtime, play.handlers),
  );
  await identity.hub.start();
  await play.start();
  app.addHook('onClose', async () => play.stop());

  return app;
}
