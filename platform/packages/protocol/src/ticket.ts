// Match tickets: EdDSA (Ed25519) JWTs the platform issues to each player when a
// match starts or a player reconnects. The relay verifies them against the
// platform's JWKS (/.well-known/jwks.json) and trusts nothing else.
import { Type, type Static } from 'typebox';
import { HttpsOrWssUrl, Open, SeatIndex, Uuid } from './common.ts';
import { SimVersion } from './simVersion.ts';

/** JOSE header `typ` of a match ticket. */
export const MATCH_TICKET_TYPE = 'glob2-match+jwt';
/** `aud` claim of a match ticket. */
export const MATCH_TICKET_AUDIENCE = 'glob2-relay';
/** JOSE header `typ` of an access token (RFC 9068); never accepted as a ticket. */
export const ACCESS_TOKEN_TYPE = 'at+jwt';
/** Signing algorithm of every platform-issued JWT. */
export const PLATFORM_JWT_ALGORITHM = 'EdDSA';

export const MatchTicketHeader = Open(
  {
    alg: Type.Literal(PLATFORM_JWT_ALGORITHM),
    typ: Type.Literal(MATCH_TICKET_TYPE),
    kid: Type.String({ minLength: 1, maxLength: 128 }),
  },
  { description: 'JOSE header of a match ticket.' },
);
export type MatchTicketHeader = Static<typeof MatchTicketHeader>;

export const MatchTicketClaims = Open(
  {
    iss: Type.String({ description: 'Instance origin, e.g. https://play.example.org.' }),
    aud: Type.Literal(MATCH_TICKET_AUDIENCE),
    sub: Uuid,
    jti: Uuid,
    iat: Type.Integer({ minimum: 0 }),
    nbf: Type.Optional(Type.Integer({ minimum: 0 })),
    exp: Type.Integer({ minimum: 0 }),
    matchId: Uuid,
    seat: SeatIndex,
    accountId: Uuid,
    simVersion: SimVersion,
    humanSeats: Type.Array(SeatIndex, {
      minItems: 1,
      maxItems: 12,
      uniqueItems: true,
      description:
        'Every human seat of the match; identical in all tickets of a match so the relay knows whom to wait for.',
    }),
    relayUrl: HttpsOrWssUrl,
    entitlements: Type.Array(Type.String({ maxLength: 64 }), {
      maxItems: 64,
      description: 'Entitlement keys of the account. Relays ignore them today.',
    }),
  },
  {
    description:
      'Claims of a match ticket. `sub` equals `accountId`. Relays check that all tickets of a match agree on simVersion and humanSeats.',
  },
);
export type MatchTicketClaims = Static<typeof MatchTicketClaims>;
