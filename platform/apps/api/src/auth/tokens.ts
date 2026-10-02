// Access and refresh tokens.
//
// Access tokens are short-lived EdDSA JWTs (`typ: at+jwt`, audience
// glob2-platform). Refresh tokens are opaque 256-bit secrets stored as SHA-256
// hashes. Each sign-in starts a family (the access token's `sid`); refreshing
// rotates the token within its family, and presenting an already-rotated token
// is treated as theft: the whole family is revoked, which also invalidates its
// access tokens on their next use.
import { randomUUID } from 'node:crypto';
import { sql, type Kysely } from 'kysely';
import type { Account, Database } from '@glob2/db';
import {
  ACCESS_TOKEN_AUDIENCE,
  ACCESS_TOKEN_TYPE,
  AccessTokenClaims,
  isValid,
  type AuthTokens,
  type ClientPlatform,
} from '@glob2/protocol';
import { JwtError } from '@glob2/protocol/node';
import { apiError } from '../errors.ts';
import type { SigningKeys } from './keys.ts';
import { randomSecret, sha256Hex } from './secrets.ts';

export interface TokenLifetimes {
  accessTokenSeconds: number;
  refreshTokenSeconds: number;
}

export interface VerifiedAccess {
  claims: AccessTokenClaims;
  account: Account;
}

export type RefreshOutcome =
  | { ok: true; tokens: AuthTokens; account: Account; familyId: string }
  | {
      ok: false;
      reason: 'unknown' | 'expired' | 'revoked' | 'reused';
      familyId?: string;
      accountId?: string;
    };

export class TokenService {
  private readonly db: Kysely<Database>;
  private readonly keys: SigningKeys;
  private readonly issuer: string;
  readonly lifetimes: TokenLifetimes;

  constructor(db: Kysely<Database>, keys: SigningKeys, issuer: string, lifetimes: TokenLifetimes) {
    this.db = db;
    this.keys = keys;
    this.issuer = issuer;
    this.lifetimes = lifetimes;
  }

  accessToken(
    account: Account,
    familyId: string,
    platform: string,
  ): { token: string; expiresAt: Date } {
    const iat = Math.floor(Date.now() / 1000);
    const exp = iat + this.lifetimes.accessTokenSeconds;
    const claims: AccessTokenClaims = {
      iss: this.issuer,
      aud: ACCESS_TOKEN_AUDIENCE,
      sub: account.id,
      jti: randomUUID(),
      iat,
      exp,
      client_id: platform,
      sid: familyId,
      kind: account.kind,
      role: account.role,
    };
    return { token: this.keys.sign(ACCESS_TOKEN_TYPE, claims), expiresAt: new Date(exp * 1000) };
  }

  /** Starts a new sign-in (refresh-token family) for the account. */
  async issue(
    account: Account,
    platform: ClientPlatform | string,
  ): Promise<{ tokens: AuthTokens; familyId: string }> {
    const familyId = randomUUID();
    return { tokens: await this.issueInFamily(account, familyId, platform), familyId };
  }

  private async issueInFamily(
    account: Account,
    familyId: string,
    platform: string,
  ): Promise<AuthTokens> {
    const refreshToken = randomSecret();
    const refreshExpires = new Date(Date.now() + this.lifetimes.refreshTokenSeconds * 1000);
    await this.db
      .insertInto('refresh_tokens')
      .values({
        account_id: account.id,
        family_id: familyId,
        token_hash: sha256Hex(refreshToken),
        client_platform: platform,
        expires_at: refreshExpires,
      })
      .execute();
    const access = this.accessToken(account, familyId, platform);
    return {
      tokenType: 'Bearer',
      accessToken: access.token,
      accessTokenExpiresAt: access.expiresAt.toISOString(),
      refreshToken,
      refreshTokenExpiresAt: refreshExpires.toISOString(),
    };
  }

  /** Rotates a refresh token. Reuse of a rotated token revokes its family. */
  async refresh(refreshToken: string): Promise<RefreshOutcome> {
    const hash = sha256Hex(refreshToken);
    const result = await this.db.transaction().execute(async (tx) => {
      const row = await tx
        .selectFrom('refresh_tokens')
        .selectAll()
        .where('token_hash', '=', hash)
        .forUpdate()
        .executeTakeFirst();
      if (!row) return { ok: false as const, reason: 'unknown' as const };
      const ids = { familyId: row.family_id, accountId: row.account_id };
      if (row.revoked_at) return { ok: false as const, reason: 'revoked' as const, ...ids };
      if (row.rotated_at) {
        await tx
          .updateTable('refresh_tokens')
          .set({ revoked_at: sql<Date>`now()` })
          .where('family_id', '=', row.family_id)
          .where('revoked_at', 'is', null)
          .execute();
        return { ok: false as const, reason: 'reused' as const, ...ids };
      }
      if (row.expires_at <= new Date())
        return { ok: false as const, reason: 'expired' as const, ...ids };
      await tx
        .updateTable('refresh_tokens')
        .set({ rotated_at: sql<Date>`now()` })
        .where('id', '=', row.id)
        .execute();
      return { ok: true as const, row };
    });
    if (!result.ok) return result;
    const { row } = result;
    const account = await this.db
      .selectFrom('accounts')
      .selectAll()
      .where('id', '=', row.account_id)
      .executeTakeFirst();
    if (!account || account.status !== 'active') {
      await this.revokeFamily(row.family_id);
      return { ok: false, reason: 'revoked', familyId: row.family_id, accountId: row.account_id };
    }
    const tokens = await this.issueInFamily(
      account,
      row.family_id,
      row.client_platform ?? 'desktop',
    );
    return { ok: true, tokens, account, familyId: row.family_id };
  }

  /** Revokes the family of a refresh token (sign-out). Returns the family, if any. */
  async revokeByToken(
    refreshToken: string,
  ): Promise<{ familyId: string; accountId: string } | undefined> {
    const row = await this.db
      .selectFrom('refresh_tokens')
      .select(['family_id', 'account_id'])
      .where('token_hash', '=', sha256Hex(refreshToken))
      .executeTakeFirst();
    if (!row) return undefined;
    await this.revokeFamily(row.family_id);
    return { familyId: row.family_id, accountId: row.account_id };
  }

  async revokeFamily(familyId: string): Promise<void> {
    await this.db
      .updateTable('refresh_tokens')
      .set({ revoked_at: sql<Date>`now()` })
      .where('family_id', '=', familyId)
      .where('revoked_at', 'is', null)
      .execute();
  }

  async revokeAccount(accountId: string): Promise<void> {
    await this.db
      .updateTable('refresh_tokens')
      .set({ revoked_at: sql<Date>`now()` })
      .where('account_id', '=', accountId)
      .where('revoked_at', 'is', null)
      .execute();
  }

  /** Signature and claims only; no database access. */
  verifyAccessSignature(token: string): AccessTokenClaims {
    let claims: Record<string, unknown>;
    try {
      ({ claims } = this.keys.verify(token, {
        type: ACCESS_TOKEN_TYPE,
        audience: ACCESS_TOKEN_AUDIENCE,
      }));
    } catch (error) {
      if (error instanceof JwtError) {
        throw apiError(
          'unauthenticated',
          error.reason === 'expired'
            ? 'The access token has expired.'
            : 'The access token is invalid.',
          { reason: error.reason },
        );
      }
      throw error;
    }
    if (!isValid(AccessTokenClaims, claims) || claims.iss !== this.issuer) {
      throw apiError('unauthenticated', 'The access token is invalid.', { reason: 'claims' });
    }
    return claims;
  }

  /**
   * Verifies an access token and loads its account. Fails if the account is
   * not active or the token's sign-in was revoked (sign-out, reuse, ban).
   */
  async verifyAccess(token: string): Promise<VerifiedAccess> {
    const claims = this.verifyAccessSignature(token);
    const row = await this.db
      .selectFrom('accounts')
      .selectAll('accounts')
      .select(
        sql<boolean>`EXISTS (SELECT 1 FROM refresh_tokens r WHERE r.family_id = ${claims.sid}::uuid AND r.revoked_at IS NOT NULL)`.as(
          'family_revoked',
        ),
      )
      .where('accounts.id', '=', claims.sub)
      .executeTakeFirst();
    if (!row || row.status === 'deleted' || row.family_revoked) {
      throw apiError('unauthenticated', 'The session has ended.', { reason: 'revoked' });
    }
    if (row.status === 'banned') throw apiError('forbidden', 'This account is banned.');
    const { family_revoked: revoked, ...account } = row;
    void revoked;
    return { claims, account };
  }
}
