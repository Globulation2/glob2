// /internal/v1: the relays' side of the platform. Every call carries
// `Authorization: Bearer <relay key>`, one of the keys configured in
// RELAY_KEYS / RELAY_KEYS_FILE; a key written `<relayId>:<key>` acts only as
// that relay. Shapes are the protocol package's relay.ts. Relays retry these
// calls from a spool, so record uploads and end reports are idempotent.
import { createHash, timingSafeEqual } from 'node:crypto';
import type { FastifyInstance, FastifyRequest } from 'fastify';
import type { RelayKey } from '@glob2/core';
import {
  RelayHeartbeat,
  RelayMatchEnded,
  RelayNetworkSummary,
  RelayRegistration,
  schemaIssues,
  type RelayHeartbeatResponse,
  type RelayMatchEndedResponse,
  type RelayRecordReceipt,
  type RelayRegistrationResponse,
} from '@glob2/protocol';
import {
  RELAY_HEARTBEAT_SECONDS,
  STORED_MATCH_SETUP,
  readStored,
  recordMatchEnded,
  registerRelay,
  relayHeartbeat,
  storeMatchRecord,
} from '@glob2/play';
import { apiError } from '../errors.ts';
import { body } from '../http/validate.ts';

const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

function digest(value: string): Buffer {
  return createHash('sha256').update(value).digest();
}

/** Finds the configured relay key the request presents (constant-time compare). */
export function matchRelayKey(keys: readonly RelayKey[], presented: string): RelayKey | undefined {
  const candidate = digest(presented);
  let found: RelayKey | undefined;
  for (const key of keys) {
    if (timingSafeEqual(digest(key.key), candidate) && !found) found = key;
  }
  return found;
}

export async function internalRoutes(app: FastifyInstance): Promise<void> {
  const { services } = app;
  const { db, blobs, jobs, config } = services;
  const keys = config.relayKeys ?? [];
  const origin = config.publicOrigin;

  function relayKey(request: FastifyRequest): RelayKey {
    const header = request.headers.authorization;
    const token = header ? /^Bearer\s+(\S+)$/i.exec(header)?.[1] : undefined;
    if (!token) throw apiError('unauthenticated', 'Relay key required.');
    const key = matchRelayKey(keys, token);
    if (!key) throw apiError('unauthenticated', 'Unknown relay key.');
    return key;
  }

  function actAs(key: RelayKey, relayId: string): void {
    if (key.relayId && key.relayId !== relayId) {
      throw apiError('forbidden', `This key belongs to relay ${key.relayId}.`);
    }
  }

  function matchId(request: FastifyRequest<{ Params: { matchId: string } }>): string {
    const id = request.params.matchId;
    if (!UUID.test(id)) throw apiError('not_found', 'No such match.');
    return id;
  }

  /** A pinned key may only touch matches placed on its relay. */
  async function checkMatchRelay(key: RelayKey, id: string): Promise<void> {
    const match = await db
      .selectFrom('matches')
      .select('relay_id')
      .where('id', '=', id)
      .executeTakeFirst();
    if (!match) throw apiError('not_found', 'No such match.');
    if (key.relayId && match.relay_id && match.relay_id !== key.relayId) {
      throw apiError('forbidden', 'The match is placed on another relay.');
    }
  }

  const internal = { config: { rateLimit: false } } as const;

  app.post(
    '/internal/v1/relays/register',
    internal,
    async (request): Promise<RelayRegistrationResponse> => {
      const key = relayKey(request);
      const registration = body(RelayRegistration, request.body);
      actAs(key, registration.relayId);
      await registerRelay(db, registration);
      request.log.info(
        {
          relay: registration.relayId,
          region: registration.region,
          draining: registration.draining,
        },
        'relay registered',
      );
      return {
        relayId: registration.relayId,
        heartbeatIntervalSeconds: RELAY_HEARTBEAT_SECONDS,
        jwksUrl: `${origin}/.well-known/jwks.json`,
      };
    },
  );

  app.post('/internal/v1/relays/heartbeat', internal, async (request, reply) => {
    const key = relayKey(request);
    const heartbeat = body(RelayHeartbeat, request.body);
    actAs(key, heartbeat.relayId);
    const outcome = await relayHeartbeat(db, heartbeat);
    if (!outcome.known) {
      const response: RelayHeartbeatResponse = { ok: false, reregister: true };
      return reply.status(404).send(response);
    }
    const response: RelayHeartbeatResponse = { ok: true };
    return response;
  });

  app.get<{ Params: { matchId: string } }>(
    '/internal/v1/matches/:matchId/setup',
    internal,
    async (request, reply) => {
      const key = relayKey(request);
      const id = matchId(request);
      await checkMatchRelay(key, id);
      const match = await db
        .selectFrom('matches')
        .select('setup')
        .where('id', '=', id)
        .executeTakeFirstOrThrow();
      // Relays get the current MatchSetup version: older rows are upgraded on
      // the way out, and a row that cannot be read fails here, not in the relay.
      const setup = readStored(STORED_MATCH_SETUP, match.setup);
      return reply.header('content-type', 'application/json; charset=utf-8').send(setup);
    },
  );

  app.put<{ Params: { matchId: string } }>(
    '/internal/v1/matches/:matchId/record',
    { ...internal, bodyLimit: config.recordMaxBytes ?? 64 * 1024 * 1024 },
    async (request): Promise<RelayRecordReceipt> => {
      const key = relayKey(request);
      const id = matchId(request);
      await checkMatchRelay(key, id);
      const bytes = request.body;
      if (!(bytes instanceof Buffer) || bytes.length === 0) {
        throw apiError('bad_request', 'Send the match record as the body.');
      }
      const outcome = await storeMatchRecord(db, blobs, id, bytes);
      if (!outcome.ok) {
        throw outcome.reason === 'not_found'
          ? apiError('not_found', 'No such match.')
          : apiError('conflict', 'The match already ended with a different record.');
      }
      return { matchId: id, sha256: outcome.sha256, size: outcome.size };
    },
  );

  app.post<{ Params: { matchId: string } }>(
    '/internal/v1/matches/:matchId/end',
    internal,
    async (request): Promise<RelayMatchEndedResponse> => {
      const key = relayKey(request);
      const id = matchId(request);
      // Network telemetry never blocks a match end: an unreadable summary (a
      // relay newer or older than this platform) is dropped, the rest applies.
      let raw = request.body;
      if (raw && typeof raw === 'object' && 'network' in raw) {
        const issues = schemaIssues(RelayNetworkSummary, (raw as { network: unknown }).network);
        if (issues.length > 0) {
          request.log.warn(
            { match: id, issues: issues.slice(0, 5) },
            'dropping an unreadable network summary from an end report',
          );
          const rest: Record<string, unknown> = { ...(raw as Record<string, unknown>) };
          delete rest['network'];
          raw = rest;
        }
      }
      const report = body(RelayMatchEnded, raw);
      if (report.matchId !== id) throw apiError('bad_request', 'matchId differs from the path.');
      actAs(key, report.relayId);
      await checkMatchRelay(key, id);
      const outcome = await recordMatchEnded(db, jobs, report);
      if (!outcome.ok) {
        switch (outcome.reason) {
          case 'not_found':
            throw apiError('not_found', 'No such match.');
          case 'record_missing':
            throw apiError('conflict', 'Upload the match record (PUT …/record) first.');
          case 'record_mismatch':
            throw apiError('conflict', 'record.sha256 differs from the uploaded record.');
          case 'sim_version_mismatch':
            throw apiError('conflict', 'simVersion differs from the match setup.');
        }
      }
      if (!outcome.duplicate) {
        request.log.info(
          { match: id, reason: report.reason, relay: report.relayId },
          'match ended',
        );
      }
      return { ok: true, ...(outcome.duplicate ? { duplicate: true } : {}) };
    },
  );
}
