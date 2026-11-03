// REST sign-in: guests, token refresh and sign-out, local passwords, JWKS.
import type { FastifyInstance } from 'fastify';
import {
  GuestSignInRequest,
  LocalRegisterRequest,
  LocalSignInRequest,
  RefreshRequest,
  SignOutRequest,
  type AuthTokens,
  type PlatformJwks,
  type SignInResponse,
} from '@glob2/protocol';
import type { Account } from '@glob2/db';
import { apiError } from '../errors.ts';
import { body } from '../http/validate.ts';
import { SharedLimit, enforce, perAddress } from '../http/rateLimits.ts';
import { authenticate, clearSessionCookie, sessionCookieName, type Identity } from '../identity.ts';
import {
  LOCAL_PROVIDER,
  hashPassword,
  normalizeUsername,
  verifyPassword,
} from '../auth/passwords.ts';

export async function signInResponse(
  identity: Identity,
  account: Account,
  platform: string,
  deviceCredential?: string,
): Promise<SignInResponse> {
  const { tokens } = await identity.tokens.issue(account, platform);
  return {
    account: await identity.accounts.selfView(account),
    tokens,
    ...(deviceCredential ? { deviceCredential } : {}),
  };
}

export async function authRoutes(app: FastifyInstance, identity: Identity): Promise<void> {
  const { services } = app;
  // Per client address and route, across replicas.
  const authLimit = (route: string) => ({
    preHandler: perAddress(
      new SharedLimit(services.db, `auth:${route}`, identity.limits.authPerMinute, 60_000),
    ),
  });

  app.get('/.well-known/jwks.json', async (_request, reply): Promise<PlatformJwks> => {
    void reply.header('cache-control', 'public, max-age=300');
    return identity.keys.jwks();
  });

  app.post(
    '/api/v1/auth/guest',
    authLimit('guest'),
    async (request, reply): Promise<SignInResponse> => {
      if (!services.config.instance.guests.enabled) {
        throw apiError('forbidden', 'Guest accounts are disabled on this instance.');
      }
      const input = body(GuestSignInRequest, request.body);
      if (input.deviceCredential) {
        const account = await identity.accounts.findByDeviceCredential(input.deviceCredential);
        if (!account || account.status === 'deleted') {
          throw apiError('unauthenticated', 'Unknown device credential.');
        }
        if (account.status === 'banned') throw apiError('forbidden', 'This account is banned.');
        await identity.accounts.touch(account.id);
        return signInResponse(identity, account, input.platform);
      }
      await enforce(
        identity.shared.guests,
        request.ip,
        reply,
        'Too many new guest accounts from this address.',
      );
      const { account, credential } = await identity.accounts.createGuest(input.platform);
      request.log.info({ account: account.id }, 'guest account created');
      return signInResponse(identity, account, input.platform, credential);
    },
  );

  app.post('/api/v1/auth/refresh', authLimit('refresh'), async (request): Promise<AuthTokens> => {
    const { refreshToken } = body(RefreshRequest, request.body);
    const outcome = await identity.tokens.refresh(refreshToken);
    if (outcome.ok) return outcome.tokens;
    if (outcome.reason === 'reused' && outcome.familyId) {
      request.log.warn(
        { account: outcome.accountId, family: outcome.familyId },
        'refresh token reuse detected; sign-in revoked',
      );
      await identity.hub.publish({
        t: 'event',
        to: { family: outcome.familyId },
        event: 'session.revoked',
        data: { reason: 'refresh token reuse' },
        signOut: true,
      });
    }
    throw apiError('unauthenticated', 'The refresh token is not valid.', {
      reason: outcome.reason,
    });
  });

  app.post('/api/v1/auth/sign-out', authLimit('sign-out'), async (request, reply) => {
    const { refreshToken } = body(SignOutRequest, request.body);
    const revoked = await identity.tokens.revokeByToken(refreshToken);
    if (revoked) {
      await identity.hub.publish({
        t: 'event',
        to: { family: revoked.familyId },
        event: 'session.revoked',
        data: { reason: 'signed out' },
        signOut: true,
      });
    }
    return reply.status(204).send();
  });

  // Ends the browser's web session (cookie).
  app.post('/api/v1/auth/web/sign-out', async (request, reply) => {
    const caller = await authenticate(identity, request);
    const secret = request.cookies[sessionCookieName(identity)];
    if (caller?.via === 'cookie' && secret) await identity.webSessions.revoke(secret);
    clearSessionCookie(identity, reply);
    return reply.status(204).send();
  });

  // ------------------------------------------------------- local accounts

  app.post(
    '/api/v1/auth/local/register',
    authLimit('register'),
    async (request): Promise<SignInResponse> => {
      if (!identity.localAuth.allowRegistration) {
        throw apiError('unsupported', 'Local accounts are not enabled on this instance.');
      }
      const input = body(LocalRegisterRequest, request.body);
      const caller = await authenticate(identity, request);
      const passwordHash = await hashPassword(input.password);
      const outcome = await identity.accounts.resolveIdentity(
        {
          provider: LOCAL_PROVIDER,
          subject: normalizeUsername(input.username),
          name: input.displayName ?? input.username,
        },
        { current: caller?.account, mode: 'link', passwordHash, createOnly: true },
      );
      if (outcome.kind === 'conflict') {
        throw apiError('conflict', 'That username is taken.', { reason: 'username_taken' });
      }
      return signInResponse(identity, outcome.account, input.platform);
    },
  );

  app.post(
    '/api/v1/auth/local/sign-in',
    authLimit('local-sign-in'),
    async (request): Promise<SignInResponse> => {
      if (!identity.localAuth.enabled) {
        throw apiError('unsupported', 'Local accounts are not enabled on this instance.');
      }
      const input = body(LocalSignInRequest, request.body);
      const account = await localSignIn(identity, input.username, input.password, request.ip);
      return signInResponse(identity, account, input.platform);
    },
  );
}

/**
 * Verifies a local username and password; the same error for every wrong
 * password. Failures count per username and per client address, on every
 * replica: a username with too many recent failures is locked for the rest of
 * its 15-minute window (whoever guesses), and so is an address guessing many
 * usernames. A correct password clears its username's count.
 */
export async function localSignIn(
  identity: Identity,
  username: string,
  password: string,
  address: string,
) {
  const subject = normalizeUsername(username);
  const { passwordFailuresPerAccount: perAccount, passwordFailuresPerAddress: perAddress } =
    identity.shared;
  if ((await perAccount.exhausted(subject)) || (await perAddress.exhausted(address))) {
    throw apiError(
      'rate_limited',
      'Too many wrong passwords. Wait 15 minutes and try again, or sign in another way.',
      { reason: 'password_attempts' },
    );
  }
  const row = await identity.accounts.localIdentity(subject);
  const ok = await verifyPassword(row?.password_hash, password);
  if (!row || !ok) {
    await perAccount.take(subject);
    await perAddress.take(address);
    throw apiError('unauthenticated', 'Wrong username or password.');
  }
  await perAccount.reset(subject);
  return identity.accounts.requireUsable(row.account_id);
}
