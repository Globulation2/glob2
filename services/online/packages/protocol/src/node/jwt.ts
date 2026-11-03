// Minimal EdDSA (Ed25519) JWT signing and verification on node:crypto. The
// platform signs access tokens and match tickets with it; tests and fixture
// generation use it to produce tickets the C++ relay must accept or reject.
// Node-only: import from '@glob2/protocol/node'.
import { createPublicKey, sign, verify, type KeyObject } from 'node:crypto';
import type { JsonWebKey } from 'node:crypto';

export interface JwtHeader {
  alg: 'EdDSA';
  typ: string;
  kid: string;
}

export function base64url(data: Uint8Array | string): string {
  return Buffer.from(data).toString('base64url');
}

export function signJwt(header: JwtHeader, claims: object, privateKey: KeyObject): string {
  const signingInput = `${base64url(JSON.stringify(header))}.${base64url(JSON.stringify(claims))}`;
  const signature = sign(null, Buffer.from(signingInput), privateKey);
  return `${signingInput}.${base64url(signature)}`;
}

export class JwtError extends Error {
  readonly reason:
    | 'malformed'
    | 'algorithm'
    | 'type'
    | 'key'
    | 'signature'
    | 'expired'
    | 'audience'
    | 'not_yet_valid';
  constructor(reason: JwtError['reason'], message: string) {
    super(message);
    this.name = 'JwtError';
    this.reason = reason;
  }
}

function decodeJson(segment: string): unknown {
  try {
    return JSON.parse(Buffer.from(segment, 'base64url').toString('utf8'));
  } catch {
    throw new JwtError('malformed', 'segment is not base64url JSON');
  }
}

export interface VerifyOptions {
  /** Expected `typ` header. */
  type: string;
  /** Expected `aud` claim. */
  audience: string;
  /** Seconds since the epoch; defaults to now. */
  now?: number;
  /** Allowed clock skew in seconds (default 30). */
  leewaySeconds?: number;
  /** Resolves a `kid` to a public key. */
  key: (kid: string) => KeyObject | undefined;
}

/**
 * Verifies signature, algorithm, type, audience and time claims. Returns the
 * decoded header and claims; the caller validates the claims' shape.
 */
export function verifyJwt(
  token: string,
  options: VerifyOptions,
): { header: JwtHeader; claims: Record<string, unknown> } {
  const parts = token.split('.');
  if (parts.length !== 3) throw new JwtError('malformed', 'token must have three segments');
  const [headerPart, claimsPart, signaturePart] = parts as [string, string, string];
  const header = decodeJson(headerPart) as Partial<JwtHeader>;
  if (header.alg !== 'EdDSA') throw new JwtError('algorithm', 'alg must be EdDSA');
  if (header.typ !== options.type) throw new JwtError('type', `typ must be ${options.type}`);
  if (typeof header.kid !== 'string') throw new JwtError('key', 'kid missing');
  const key = options.key(header.kid);
  if (!key) throw new JwtError('key', `unknown kid ${header.kid}`);
  const valid = verify(
    null,
    Buffer.from(`${headerPart}.${claimsPart}`),
    key,
    Buffer.from(signaturePart, 'base64url'),
  );
  if (!valid) throw new JwtError('signature', 'signature does not verify');
  const claims = decodeJson(claimsPart);
  if (typeof claims !== 'object' || claims === null || Array.isArray(claims)) {
    throw new JwtError('malformed', 'claims must be an object');
  }
  const record = claims as Record<string, unknown>;
  const now = options.now ?? Math.floor(Date.now() / 1000);
  const leeway = options.leewaySeconds ?? 30;
  if (record['aud'] !== options.audience) throw new JwtError('audience', 'wrong audience');
  if (typeof record['exp'] !== 'number' || record['exp'] + leeway <= now) {
    throw new JwtError('expired', 'token expired');
  }
  if (typeof record['nbf'] === 'number' && record['nbf'] - leeway > now) {
    throw new JwtError('not_yet_valid', 'token not yet valid');
  }
  return { header: header as JwtHeader, claims: record };
}

/** Public JWK (for /.well-known/jwks.json) of an Ed25519 key. */
export function publicJwk(key: KeyObject, kid: string): JsonWebKey & { kid: string } {
  const jwk = createPublicKey(key).export({ format: 'jwk' });
  return { kty: jwk.kty, crv: jwk.crv, x: jwk.x, kid, alg: 'EdDSA', use: 'sig' } as JsonWebKey & {
    kid: string;
  };
}
