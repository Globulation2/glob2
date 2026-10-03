// Wires the identity services (accounts, tokens, providers, handoff, web
// sessions, moderation, realtime fan-out) from the API's injected services.
import type { FastifyReply, FastifyRequest } from 'fastify';
import type { Account } from '@glob2/db';
import type { ApiServices } from './services.ts';
import { AccountService } from './auth/accounts.ts';
import { AdminService, hasRole, type Role } from './auth/admin.ts';
import { HandoffService } from './auth/handoff.ts';
import { SigningKeys } from './auth/keys.ts';
import { ProviderRegistry } from './auth/providers.ts';
import { TokenService } from './auth/tokens.ts';
import { WebSessionService } from './auth/webSessions.ts';
import { apiError } from './errors.ts';
import { SharedLimit } from './http/rateLimits.ts';
import { RealtimeHub } from './realtime/hub.ts';

export interface Limits {
  authPerMinute: number;
  guestsPerHour: number;
  apiPerMinute: number;
  realtimePerSecond: number;
  realtimeBurst: number;
  realtimeConnectionsPerIp: number;
}

/** Limits shared by every replica (Postgres counters; see http/rateLimits.ts). */
export interface SharedLimits {
  /** New guest accounts per client address per hour. */
  guests: SharedLimit;
  /** Browser sign-ins started per client address per hour, and by everyone per minute. */
  handoffPerAddress: SharedLimit;
  handoffTotal: SharedLimit;
  /** Wrong passwords per username (15 minutes) and per client address (an hour). */
  passwordFailuresPerAccount: SharedLimit;
  passwordFailuresPerAddress: SharedLimit;
}

export interface Identity {
  keys: SigningKeys;
  accounts: AccountService;
  tokens: TokenService;
  handoff: HandoffService;
  webSessions: WebSessionService;
  providers: ProviderRegistry;
  admin: AdminService;
  hub: RealtimeHub;
  origin: string;
  /** Instance display name, for server-rendered pages. */
  instanceName: string;
  allowedOrigins: Set<string>;
  secureCookies: boolean;
  limits: Limits;
  shared: SharedLimits;
  localAuth: { enabled: boolean; allowRegistration: boolean };
  handoffSeconds: number;
}

export function createIdentity(services: ApiServices): Identity {
  const { config, db, logger } = services;
  const auth = config.instance.auth;
  const keys = services.keys ?? SigningKeys.load(config.keys, config.publicOrigin, logger);
  const accounts = new AccountService(db, {
    renameIntervalDays: config.instance.accounts?.renameIntervalDays,
  });
  const tokens = new TokenService(db, keys, config.publicOrigin, {
    accessTokenSeconds: (auth.accessTokenMinutes ?? 10) * 60,
    refreshTokenSeconds: (auth.refreshTokenDays ?? 60) * 86_400,
  });
  const handoffSeconds = (auth.handoffMinutes ?? 10) * 60;
  const handoff = new HandoffService(db, accounts, handoffSeconds);
  const webSessions = new WebSessionService(db, (auth.webSessionDays ?? 30) * 86_400);
  const hub = new RealtimeHub(db, services.pubsub, logger);
  handoff.notify = (attemptId) => hub.publish({ t: 'handoff', attemptId });
  const admin = new AdminService(db, accounts, {
    async endSessions(accountId, reason) {
      await tokens.revokeAccount(accountId);
      await webSessions.revokeAccount(accountId);
      await hub.sendToAccount(accountId, 'session.revoked', { reason }, { close: true });
    },
  });
  const limits = config.instance.limits ?? {};
  return {
    keys,
    accounts,
    tokens,
    handoff,
    webSessions,
    providers: new ProviderRegistry(config, logger),
    admin,
    hub,
    origin: config.publicOrigin,
    instanceName: config.instance.name,
    allowedOrigins: new Set([config.publicOrigin, ...(config.instance.web?.allowedOrigins ?? [])]),
    secureCookies: config.publicOrigin.startsWith('https://'),
    shared: {
      guests: new SharedLimit(db, 'guest-create', limits.guestsPerHour ?? 20, 3_600_000),
      handoffPerAddress: new SharedLimit(
        db,
        'signin-attempt',
        limits.signinAttemptsPerHour ?? 30,
        3_600_000,
      ),
      handoffTotal: new SharedLimit(
        db,
        'signin-attempt-total',
        limits.signinAttemptsPerMinuteTotal ?? 300,
        60_000,
      ),
      passwordFailuresPerAccount: new SharedLimit(
        db,
        'password-fail-account',
        limits.passwordFailuresPerAccount ?? 10,
        15 * 60_000,
      ),
      passwordFailuresPerAddress: new SharedLimit(
        db,
        'password-fail-address',
        limits.passwordFailuresPerIp ?? 50,
        3_600_000,
      ),
    },
    limits: {
      authPerMinute: limits.authPerMinute ?? 30,
      guestsPerHour: limits.guestsPerHour ?? 20,
      apiPerMinute: limits.apiPerMinute ?? 600,
      realtimePerSecond: limits.realtimePerSecond ?? 10,
      realtimeBurst: limits.realtimeBurst ?? 40,
      realtimeConnectionsPerIp: limits.realtimeConnectionsPerIp ?? 20,
    },
    localAuth: {
      enabled: auth.local.enabled,
      allowRegistration: auth.local.enabled && auth.local.allowRegistration !== false,
    },
    handoffSeconds,
  };
}

// ------------------------------------------------------------- cookies

export function sessionCookieName(identity: Identity): string {
  return identity.secureCookies ? '__Host-glob2_session' : 'glob2_session';
}

export function bindingCookieName(identity: Identity): string {
  return identity.secureCookies ? '__Host-glob2_signin' : 'glob2_signin';
}

export function setSessionCookie(identity: Identity, reply: FastifyReply, secret: string): void {
  void reply.setCookie(sessionCookieName(identity), secret, {
    path: '/',
    httpOnly: true,
    secure: identity.secureCookies,
    sameSite: 'lax',
    maxAge: identity.webSessions.maxAgeSeconds,
  });
}

export function clearSessionCookie(identity: Identity, reply: FastifyReply): void {
  void reply.clearCookie(sessionCookieName(identity), {
    path: '/',
    httpOnly: true,
    secure: identity.secureCookies,
    sameSite: 'lax',
  });
}

/**
 * The handoff browser binding. It must survive Apple's cross-site form_post to
 * the callback, so over HTTPS it is SameSite=None; it authenticates nothing by
 * itself, and every state-changing form also checks Origin.
 */
export function setBindingCookie(identity: Identity, reply: FastifyReply, secret: string): void {
  void reply.setCookie(bindingCookieName(identity), secret, {
    path: '/',
    httpOnly: true,
    secure: identity.secureCookies,
    sameSite: identity.secureCookies ? 'none' : 'lax',
    maxAge: identity.handoffSeconds,
  });
}

// --------------------------------------------------------------- guards

const SAFE_METHODS = new Set(['GET', 'HEAD', 'OPTIONS']);

/** True if a browser request comes from one of the instance's own origins. */
export function sameOriginRequest(identity: Identity, request: FastifyRequest): boolean {
  const origin = request.headers.origin;
  if (origin) return identity.allowedOrigins.has(origin);
  // Browsers that omit Origin still send Fetch Metadata.
  return request.headers['sec-fetch-site'] === 'same-origin';
}

/** Rejects state-changing cookie-authenticated requests from other sites (CSRF). */
export function requireSameOrigin(identity: Identity, request: FastifyRequest): void {
  if (!SAFE_METHODS.has(request.method) && !sameOriginRequest(identity, request)) {
    throw apiError('forbidden', 'Cross-site request refused.');
  }
}

export interface Authenticated {
  account: Account;
  via: 'bearer' | 'cookie';
  familyId?: string;
}

export function bearerToken(request: FastifyRequest): string | undefined {
  const header = request.headers.authorization;
  if (!header) return undefined;
  const match = /^Bearer\s+(\S+)$/i.exec(header);
  if (!match) throw apiError('unauthenticated', 'Malformed Authorization header.');
  return match[1];
}

/** The caller's account from a bearer access token, or (web) the session cookie. */
export async function authenticate(
  identity: Identity,
  request: FastifyRequest,
): Promise<Authenticated | undefined> {
  const token = bearerToken(request);
  if (token) {
    const { account, claims } = await identity.tokens.verifyAccess(token);
    return { account, via: 'bearer', familyId: claims.sid };
  }
  const cookie = request.cookies[sessionCookieName(identity)];
  if (cookie) {
    const account = await identity.webSessions.find(cookie);
    if (account) {
      requireSameOrigin(identity, request);
      return { account, via: 'cookie' };
    }
  }
  return undefined;
}

export async function requireAccount(identity: Identity, request: FastifyRequest) {
  const caller = await authenticate(identity, request);
  if (!caller) throw apiError('unauthenticated', 'Sign in first.');
  return caller;
}

export async function requireRole(identity: Identity, request: FastifyRequest, role: Role) {
  const caller = await requireAccount(identity, request);
  if (!hasRole(caller.account, role)) throw apiError('forbidden', `Requires the ${role} role.`);
  return caller;
}
