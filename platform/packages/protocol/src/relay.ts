// Internal contracts between relays and the platform (/internal/v1/relays/...).
// Relays authenticate with a bearer relay key configured by the operator.
import { Type, type Static } from 'typebox';
import { HttpsOrWssUrl, Open, SeatIndex, Sha256Hex, Strict, Timestamp, Uuid } from './common.ts';
import { SimVersion } from './simVersion.ts';

export const RelayRegion = Type.String({
  pattern: '^[a-z0-9][a-z0-9-]{0,31}$',
  description: 'Operator-chosen region id, e.g. "eu-west".',
});

const RelayLoad = Strict({
  matches: Type.Integer({ minimum: 0 }),
  connections: Type.Integer({ minimum: 0 }),
  cpu: Type.Optional(Type.Number({ minimum: 0, maximum: 1, description: 'Fraction of capacity.' })),
});

/** POST /internal/v1/relays/register — sent at start-up and after the platform forgets the relay. */
export const RelayRegistration = Strict(
  {
    relayId: Type.String({ pattern: '^[A-Za-z0-9._-]{1,64}$' }),
    publicUrl: HttpsOrWssUrl,
    region: RelayRegion,
    build: Type.String({ maxLength: 128, description: 'Relay build identifier.' }),
    turnProtocol: Type.Integer({
      minimum: 1,
      description: 'Binary turn-protocol version (docs/multiplayer/turn-protocol.md).',
    }),
    capacity: Strict({ maxMatches: Type.Integer({ minimum: 1 }) }),
    load: RelayLoad,
    draining: Type.Boolean(),
  },
  { description: 'Relay start-up registration.' },
);
export type RelayRegistration = Static<typeof RelayRegistration>;

export const RelayRegistrationResponse = Open({
  relayId: Type.String(),
  heartbeatIntervalSeconds: Type.Integer({ minimum: 1 }),
  jwksUrl: HttpsOrWssUrl,
});
export type RelayRegistrationResponse = Static<typeof RelayRegistrationResponse>;

/** POST /internal/v1/relays/heartbeat — periodic load and drain report. */
export const RelayHeartbeat = Strict(
  {
    relayId: Type.String({ pattern: '^[A-Za-z0-9._-]{1,64}$' }),
    load: RelayLoad,
    draining: Type.Boolean(),
    activeMatchIds: Type.Array(Uuid, { maxItems: 10000 }),
  },
  { description: 'Periodic relay health and load report.' },
);
export type RelayHeartbeat = Static<typeof RelayHeartbeat>;

export const RelayHeartbeatResponse = Open({
  ok: Type.Boolean(),
  reregister: Type.Optional(
    Type.Boolean({ description: 'The platform does not know this relay; register again.' }),
  ),
});
export type RelayHeartbeatResponse = Static<typeof RelayHeartbeatResponse>;

/**
 * POST /internal/v1/matches/{matchId}/end — sent by the relay when a match ends,
 * after uploading the match record with PUT /internal/v1/matches/{matchId}/record.
 */
export const RelayMatchEnded = Strict(
  {
    matchId: Uuid,
    relayId: Type.String({ pattern: '^[A-Za-z0-9._-]{1,64}$' }),
    simVersion: SimVersion,
    startedAt: Timestamp,
    endedAt: Timestamp,
    finalTick: Type.Integer({ minimum: 0 }),
    reason: Type.Union(
      [Type.Literal('completed'), Type.Literal('abandoned'), Type.Literal('aborted')],
      {
        description:
          'completed: the game ended normally; abandoned: every human left; aborted: the relay ended it (error, drain timeout).',
      },
    ),
    seats: Type.Array(
      Strict({
        seat: SeatIndex,
        disconnects: Type.Integer({ minimum: 0 }),
        quitTick: Type.Optional(Type.Integer({ minimum: 0 })),
        droppedForDesync: Type.Boolean(),
      }),
      { maxItems: 12 },
    ),
    desync: Strict({
      flagged: Type.Boolean({
        description: 'Checksums disagreed and no majority settled it (two clients).',
      }),
      minoritySeats: Type.Array(SeatIndex, { maxItems: 12, uniqueItems: true }),
    }),
    record: Strict({
      sha256: Sha256Hex,
      size: Type.Integer({ minimum: 0 }),
      formatVersion: Type.Integer({ minimum: 1 }),
    }),
  },
  { description: 'Relay report of a finished match.' },
);
export type RelayMatchEnded = Static<typeof RelayMatchEnded>;

export type RelayRegion = Static<typeof RelayRegion>;
