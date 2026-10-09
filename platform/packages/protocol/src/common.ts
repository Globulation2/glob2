// Shared building blocks. Every schema in this package is plain JSON Schema
// (draft 2020-12 compatible) so non-TypeScript consumers (the C++ client, relay
// and verifier) can use the exported files under fixtures/schemas/.
//
// Strictness rule: documents that an untrusted party sends, or that must mean
// exactly one thing to every engine (MatchSetup, requests, job payloads), reject
// unknown properties. Documents the platform emits to clients (responses,
// events, REST resources) allow unknown properties so newer servers can add
// fields without breaking older clients.
import { Type, type Static, type TProperties, type TObject, type TObjectOptions } from 'typebox';

/** Highest number of teams (colonies) and of player records in a game, Team::MAX_COUNT. */
export const MAX_TEAMS = 12;
/** BasePlayer::MAX_NAME_LENGTH, in UTF-8 bytes. */
export const MAX_PLAYER_NAME_BYTES = 32;
/** GAME_TICKS_PER_SECOND in src/engine/EngineTiming.h. */
export const TICKS_PER_SECOND = 30;

/** An object that rejects unknown properties. */
export function Strict<P extends TProperties>(
  properties: P,
  options: TObjectOptions = {},
): TObject<P> {
  return Type.Object(properties, { additionalProperties: false, ...options });
}

/** An object that tolerates unknown properties (for server-emitted documents). */
export function Open<P extends TProperties>(
  properties: P,
  options: TObjectOptions = {},
): TObject<P> {
  return Type.Object(properties, options);
}

export const Sha256Hex = Type.String({
  pattern: '^[0-9a-f]{64}$',
  description: 'Lowercase hexadecimal SHA-256 digest.',
});

export const Uuid = Type.String({
  pattern: '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$',
  description: 'Lowercase UUID.',
});

export const Uint32 = Type.Integer({ minimum: 0, maximum: 4294967295 });

export const Timestamp = Type.String({
  pattern: '^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}(\\.\\d+)?(Z|[+-]\\d{2}:\\d{2})$',
  description: 'RFC 3339 date-time.',
});

export const HttpsOrWssUrl = Type.String({
  pattern: '^(https|wss|http|ws)://[^\\s]+$',
  maxLength: 2048,
});

/** Player record index in a GameHeader (BasePlayer::number). */
export const SeatIndex = Type.Integer({ minimum: 0, maximum: MAX_TEAMS - 1 });

/** Team (colony) index in the map (BasePlayer::teamNumber). */
export const TeamIndex = Type.Integer({ minimum: 0, maximum: MAX_TEAMS - 1 });

export const DisplayName = Type.String({
  minLength: 1,
  maxLength: MAX_PLAYER_NAME_BYTES,
  pattern: '^[^\\u0000-\\u001f\\u007f]+$',
  description: `Display name; at most ${MAX_PLAYER_NAME_BYTES} UTF-8 bytes (checked semantically).`,
});

/** Opaque short code used in invite links (https://<instance>/j/<code>). */
export const InviteCode = Type.String({ pattern: '^[A-Za-z0-9]{6,16}$' });

export const Cursor = Type.String({ minLength: 1, maxLength: 512 });

export const ClientPlatform = Type.Union(
  [Type.Literal('desktop'), Type.Literal('android'), Type.Literal('ios'), Type.Literal('browser')],
  { description: 'Kind of game client.' },
);

/** Machine-readable error codes shared by REST and realtime responses. */
export const ErrorCode = Type.Union([
  Type.Literal('bad_request'),
  Type.Literal('unauthenticated'),
  Type.Literal('forbidden'),
  Type.Literal('access_denied'),
  Type.Literal('not_found'),
  Type.Literal('conflict'),
  Type.Literal('rate_limited'),
  Type.Literal('update_required'),
  Type.Literal('unsupported'),
  Type.Literal('unavailable'),
  Type.Literal('internal'),
]);

export const ErrorBody = Open(
  {
    code: ErrorCode,
    message: Type.String({ maxLength: 2000 }),
    details: Type.Optional(Type.Unknown()),
  },
  { description: 'Error payload: REST error responses and failed realtime responses.' },
);

/** UTF-8 byte length, used where the engine limits bytes rather than code units. */
export function utf8ByteLength(value: string): number {
  return new TextEncoder().encode(value).length;
}

export type Sha256Hex = Static<typeof Sha256Hex>;
export type Uuid = Static<typeof Uuid>;
export type Uint32 = Static<typeof Uint32>;
export type Timestamp = Static<typeof Timestamp>;
export type HttpsOrWssUrl = Static<typeof HttpsOrWssUrl>;
export type SeatIndex = Static<typeof SeatIndex>;
export type TeamIndex = Static<typeof TeamIndex>;
export type DisplayName = Static<typeof DisplayName>;
export type InviteCode = Static<typeof InviteCode>;
export type Cursor = Static<typeof Cursor>;
export type ClientPlatform = Static<typeof ClientPlatform>;
export type ErrorCode = Static<typeof ErrorCode>;
export type ErrorBody = Static<typeof ErrorBody>;
