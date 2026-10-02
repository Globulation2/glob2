// The /realtime WebSocket: upgrade checks (Origin allow-list, connections per
// address), heartbeat, and the session and sign-in methods. Room, queue and
// match methods answer `unsupported` until their milestones land.
import type { FastifyInstance } from 'fastify';
import type { WebSocket } from 'ws';
import {
  REALTIME_PROTOCOL_VERSION,
  sameSimVersion,
  type RealtimeMethod,
  type RealtimeParams,
} from '@glob2/protocol';
import { supportedSimVersions } from '../app.ts';
import { apiError } from '../errors.ts';
import type { Identity } from '../identity.ts';
import { RealtimeConnection, type MethodHandler } from './connection.ts';

export const MAX_FRAME_BYTES = 64 * 1024;
/** Pending browser sign-ins per socket. */
const MAX_PENDING_ATTEMPTS = 3;

export interface RealtimeOptions {
  /** Interval between server pings; a socket that missed one pong is dropped. */
  heartbeatMs?: number;
}

export async function realtimeRoutes(
  app: FastifyInstance,
  identity: Identity,
  options: RealtimeOptions = {},
): Promise<void> {
  const { services } = app;
  const { hub } = identity;
  const perIp = new Map<string, number>();
  const expiryTimers = new Map<string, NodeJS.Timeout>();

  hub.onHandoff = (connection, attemptId) => {
    void deliverHandoff(connection, attemptId).catch((error: unknown) =>
      services.logger.error({ err: error, attempt: attemptId }, 'handoff delivery failed'),
    );
  };

  async function deliverHandoff(connection: RealtimeConnection, attemptId: string) {
    // A closed socket leaves the result unclaimed for auth.handoff.resume.
    if (!connection.open) return;
    const delivery = await identity.handoff.claimDelivery(attemptId);
    if (!delivery) return;
    clearTimeout(expiryTimers.get(attemptId));
    expiryTimers.delete(attemptId);
    hub.unwatchAttempt(attemptId);
    connection.pendingAttempts.delete(attemptId);
    if (delivery.kind === 'failed') {
      connection.sendEvent('auth.handoff.failed', {
        attemptId,
        reason: delivery.reason,
        ...(delivery.conflict ? { conflict: delivery.conflict } : {}),
      });
      return;
    }
    const { tokens, familyId } = await identity.tokens.issue(delivery.account, delivery.platform);
    connection.authenticate(delivery.account, familyId);
    connection.sendEvent('auth.handoff.completed', {
      attemptId,
      linked: delivery.linked,
      session: { account: await identity.accounts.selfView(delivery.account), tokens },
    });
  }

  /** Makes this socket the receiver of an attempt's result, with an expiry timer. */
  function watch(connection: RealtimeConnection, attemptId: string, expiresAt: Date) {
    connection.pendingAttempts.add(attemptId);
    hub.watchAttempt(attemptId, connection);
    clearTimeout(expiryTimers.get(attemptId));
    const timer = setTimeout(
      () => {
        expiryTimers.delete(attemptId);
        // The worker may have expired it already (without a notification),
        // so deliver directly rather than waiting for one.
        void identity.handoff
          .fail(attemptId, 'expired')
          .then(() => deliverHandoff(connection, attemptId))
          .catch((error: unknown) =>
            services.logger.warn({ err: error, attempt: attemptId }, 'handoff expiry failed'),
          );
      },
      Math.max(0, expiresAt.getTime() - Date.now()) + 1000,
    );
    timer.unref();
    expiryTimers.set(attemptId, timer);
  }

  const handlers: Partial<Record<RealtimeMethod, MethodHandler>> = {
    'session.hello': async (connection, raw) => {
      const params = raw as RealtimeParams<'session.hello'>;
      if (params.protocol !== REALTIME_PROTOCOL_VERSION) {
        throw apiError('update_required', 'Unsupported realtime protocol version.');
      }
      if (params.accessToken) {
        const { account, claims } = await identity.tokens.verifyAccess(params.accessToken);
        connection.authenticate(account, claims.sid);
        await identity.accounts.touch(account.id);
      }
      connection.helloDone = true;
      connection.platform = params.client.platform;
      connection.simVersion = params.client.simVersion;
      const supported = await supportedSimVersions(services.db);
      connection.simSupported = supported.some((v) => sameSimVersion(v, params.client.simVersion));
      return {
        sessionId: connection.id,
        serverTime: new Date().toISOString(),
        simSupported: connection.simSupported,
        ...(connection.account
          ? { account: await identity.accounts.selfView(connection.account) }
          : {}),
      };
    },

    'session.authenticate': async (connection, raw) => {
      const params = raw as RealtimeParams<'session.authenticate'>;
      const { account, claims } = await identity.tokens.verifyAccess(params.accessToken);
      connection.authenticate(account, claims.sid);
      return { account: await identity.accounts.selfView(account) };
    },

    'session.ping': async () => ({ serverTime: new Date().toISOString() }),

    'auth.handoff.begin': async (connection, raw) => {
      const params = raw as RealtimeParams<'auth.handoff.begin'>;
      if (
        params.provider &&
        params.provider !== 'local' &&
        !identity.providers.get(params.provider)
      ) {
        throw apiError('not_found', `No sign-in provider ${params.provider}.`);
      }
      if (connection.pendingAttempts.size >= MAX_PENDING_ATTEMPTS) {
        throw apiError('rate_limited', 'Too many sign-ins in progress on this connection.');
      }
      const mode = params.mode ?? (connection.account ? 'link' : 'signin');
      const attempt = await identity.handoff.begin({
        provider: params.provider,
        mode,
        requestingAccountId: mode === 'link' ? connection.account?.id : undefined,
        platform: connection.platform,
      });
      watch(connection, attempt.id, attempt.expiresAt);
      const url = new URL('/signin', identity.origin);
      url.searchParams.set('attempt', attempt.id);
      return {
        attemptId: attempt.id,
        signInUrl: url.href,
        confirmationCode: attempt.confirmationCode,
        expiresAt: attempt.expiresAt.toISOString(),
        resumeToken: attempt.resumeToken,
      };
    },

    'auth.handoff.resume': async (connection, raw) => {
      const params = raw as RealtimeParams<'auth.handoff.resume'>;
      const attempt = await identity.handoff.forResume(params.attemptId, params.resumeToken);
      if (!attempt) throw apiError('not_found', 'No such sign-in.');
      if (connection.pendingAttempts.size >= MAX_PENDING_ATTEMPTS) {
        throw apiError('rate_limited', 'Too many sign-ins in progress on this connection.');
      }
      watch(connection, attempt.id, attempt.expires_at);
      // Finished while no socket was waiting: deliver now.
      if (attempt.status !== 'pending') hub.onHandoff?.(connection, attempt.id);
      return { status: attempt.status === 'pending' ? 'pending' : 'finished' };
    },

    'auth.handoff.cancel': async (connection, raw) => {
      const params = raw as RealtimeParams<'auth.handoff.cancel'>;
      if (!connection.pendingAttempts.has(params.attemptId)) {
        throw apiError('not_found', 'No such sign-in on this connection.');
      }
      await identity.handoff.fail(params.attemptId, 'cancelled');
      return {};
    },
  };

  app.get(
    '/realtime',
    {
      websocket: true,
      config: { rateLimit: false },
      preValidation: async (request, reply) => {
        const origin = request.headers.origin;
        // Browsers always send Origin on WebSocket upgrades; native clients send none.
        if (origin && !identity.allowedOrigins.has(origin)) {
          return reply.status(403).send({ code: 'forbidden', message: 'Origin not allowed.' });
        }
        if ((perIp.get(request.ip) ?? 0) >= identity.limits.realtimeConnectionsPerIp) {
          return reply.status(429).send({ code: 'rate_limited', message: 'Too many connections.' });
        }
      },
    },
    (socket: WebSocket, request) => {
      perIp.set(request.ip, (perIp.get(request.ip) ?? 0) + 1);
      const connection = new RealtimeConnection(socket, request.ip, services.logger, {
        perSecond: identity.limits.realtimePerSecond,
        burst: identity.limits.realtimeBurst,
      });
      connection.onAccountChange = (c) => hub.setAccount(c, c.account?.id);
      hub.add(connection);
      socket.on('message', (data, isBinary) => {
        if (isBinary) {
          connection.close(1003, 'binary frames are not accepted');
          return;
        }
        void connection.receive(data.toString(), handlers);
      });
      socket.on('close', () => {
        const left = (perIp.get(request.ip) ?? 1) - 1;
        if (left <= 0) perIp.delete(request.ip);
        else perIp.set(request.ip, left);
        // Pending browser sign-ins stay pending: a phone app often loses its
        // socket while the browser is in front, and resumes on a new one.
        for (const attemptId of connection.pendingAttempts) {
          clearTimeout(expiryTimers.get(attemptId));
          expiryTimers.delete(attemptId);
        }
        hub.remove(connection);
      });
    },
  );

  const heartbeat = setInterval(() => {
    for (const client of app.websocketServer.clients) {
      const tracked = client as WebSocket & { glob2Alive?: boolean };
      if (tracked.glob2Alive === false) {
        client.terminate();
        continue;
      }
      tracked.glob2Alive = false;
      client.ping();
    }
  }, options.heartbeatMs ?? 30_000);
  heartbeat.unref();
  app.websocketServer.on('connection', (client: WebSocket) => {
    const tracked = client as WebSocket & { glob2Alive?: boolean };
    tracked.glob2Alive = true;
    client.on('pong', () => (tracked.glob2Alive = true));
    client.on('message', () => (tracked.glob2Alive = true));
  });

  app.addHook('onClose', async () => {
    clearInterval(heartbeat);
    for (const timer of expiryTimers.values()) clearTimeout(timer);
    await hub.stop();
  });
}
