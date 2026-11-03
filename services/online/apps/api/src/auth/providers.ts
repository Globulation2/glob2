// External sign-in providers configured in instance.yaml: generic OpenID
// Connect (with Google and Microsoft presets) and Sign in with Apple. Each runs
// the authorization-code flow in the player's browser; the platform keeps the
// flow state server-side (auth_flows) and resolves the callback to a stable
// (provider id, subject) identity.
import { createPrivateKey, sign, type KeyObject } from 'node:crypto';
import * as oidc from 'openid-client';
import { readSecret, type PlatformConfig, type ProviderConfig } from '@glob2/core';
import type { Logger } from '@glob2/core';
import type { ProviderIdentity } from './accounts.ts';

export interface AuthorizationRequest {
  state: string;
  nonce: string;
  codeVerifier: string;
  redirectUri: string;
}

export interface ProviderCallback {
  /** Callback URL including the provider's parameters (query, or form_post body moved into the query). */
  url: URL;
  /** Apple only: the `user` form field sent on the first sign-in. */
  user?: string | undefined;
}

export interface ExternalProvider {
  readonly id: string;
  readonly kind: 'oidc' | 'apple';
  readonly displayName: string;
  /** How the provider returns to the callback. */
  readonly responseMode: 'query' | 'form_post';
  authorizationUrl(request: AuthorizationRequest): Promise<URL>;
  complete(callback: ProviderCallback, request: AuthorizationRequest): Promise<ProviderIdentity>;
}

export class ProviderError extends Error {
  /** `denied` when the user refused or cancelled at the provider. */
  readonly reason: 'denied' | 'error';
  constructor(reason: 'denied' | 'error', message: string, options?: ErrorOptions) {
    super(message, options);
    this.reason = reason;
  }
}

function presetIssuer(config: Extract<ProviderConfig, { kind: 'oidc' }>): string {
  if (config.issuer) return config.issuer;
  switch (config.preset) {
    case 'google':
      return 'https://accounts.google.com';
    case 'microsoft':
      // Multi-tenant issuers carry a {tenantid} template; openid-client
      // validates the ID token's issuer against the token's own tid claim.
      return `https://login.microsoftonline.com/${config.tenant ?? 'common'}/v2.0`;
    default:
      throw new Error(`provider ${config.id}: set issuer or preset`);
  }
}

function stringClaim(value: unknown): string | undefined {
  return typeof value === 'string' && value.length > 0 ? value : undefined;
}

function emailOf(claims: Record<string, unknown>): string | undefined {
  const verified = claims['email_verified'];
  if (verified === false || verified === 'false') return undefined;
  const email = stringClaim(claims['email']);
  return email && email.length <= 320 ? email : undefined;
}

/** Shared authorization-code flow over openid-client. */
abstract class CodeFlowProvider implements ExternalProvider {
  abstract readonly id: string;
  abstract readonly kind: 'oidc' | 'apple';
  abstract readonly displayName: string;
  abstract readonly responseMode: 'query' | 'form_post';
  private configuration: Promise<oidc.Configuration> | undefined;

  protected abstract discover(): Promise<oidc.Configuration>;
  protected abstract authorizationParameters(
    request: AuthorizationRequest,
  ): Promise<Record<string, string>>;
  protected abstract identityFrom(
    claims: Record<string, unknown>,
    callback: ProviderCallback,
  ): ProviderIdentity;
  protected usesPkce = true;

  protected config(): Promise<oidc.Configuration> {
    this.configuration ??= this.discover().catch((error: unknown) => {
      this.configuration = undefined; // retry discovery on the next sign-in
      throw new ProviderError('error', `discovery failed for ${this.id}`, { cause: error });
    });
    return this.configuration;
  }

  async authorizationUrl(request: AuthorizationRequest): Promise<URL> {
    const config = await this.config();
    return oidc.buildAuthorizationUrl(config, await this.authorizationParameters(request));
  }

  async complete(
    callback: ProviderCallback,
    request: AuthorizationRequest,
  ): Promise<ProviderIdentity> {
    const error = callback.url.searchParams.get('error');
    if (error) {
      throw new ProviderError(
        error === 'access_denied' || error === 'user_cancelled_authorize' ? 'denied' : 'error',
        `provider ${this.id} returned ${error}`,
      );
    }
    const config = await this.config();
    try {
      const tokens = await oidc.authorizationCodeGrant(config, callback.url, {
        expectedState: request.state,
        expectedNonce: request.nonce,
        idTokenExpected: true,
        ...(this.usesPkce ? { pkceCodeVerifier: request.codeVerifier } : {}),
      });
      const claims = tokens.claims();
      if (!claims) throw new Error('no ID token');
      return this.identityFrom(claims as Record<string, unknown>, callback);
    } catch (cause) {
      throw new ProviderError('error', `sign-in with ${this.id} failed`, { cause });
    }
  }
}

export class OidcProvider extends CodeFlowProvider {
  readonly id: string;
  readonly kind = 'oidc' as const;
  readonly displayName: string;
  readonly responseMode = 'query' as const;
  private readonly settings: Extract<ProviderConfig, { kind: 'oidc' }>;
  private readonly secret: string | undefined;

  constructor(settings: Extract<ProviderConfig, { kind: 'oidc' }>, secret: string | undefined) {
    super();
    this.id = settings.id;
    this.displayName = settings.displayName;
    this.settings = settings;
    this.secret = secret;
  }

  protected discover(): Promise<oidc.Configuration> {
    return oidc.discovery(
      new URL(presetIssuer(this.settings)),
      this.settings.clientId,
      this.secret ? { client_secret: this.secret } : undefined,
      this.secret ? oidc.ClientSecretPost(this.secret) : oidc.None(),
      { execute: this.settings.allowInsecureIssuer ? [oidc.allowInsecureRequests] : [] },
    );
  }

  protected async authorizationParameters(request: AuthorizationRequest) {
    return {
      redirect_uri: request.redirectUri,
      scope: (this.settings.scopes ?? ['openid', 'email', 'profile']).join(' '),
      state: request.state,
      nonce: request.nonce,
      code_challenge: await oidc.calculatePKCECodeChallenge(request.codeVerifier),
      code_challenge_method: 'S256',
      prompt: 'select_account',
    };
  }

  protected identityFrom(claims: Record<string, unknown>): ProviderIdentity {
    const subject = stringClaim(claims['sub']);
    if (!subject || subject.length > 255) throw new Error('ID token has no usable sub');
    return {
      provider: this.id,
      subject,
      email: emailOf(claims),
      name:
        stringClaim(claims['name']) ??
        stringClaim(claims['preferred_username']) ??
        stringClaim(claims['given_name']),
    };
  }
}

/**
 * Sign in with Apple: OIDC with two quirks. The client secret is a short-lived
 * ES256 JWT signed with the operator's key, and responses that request the
 * name or email arrive by form_post (a cross-site POST to the callback), with
 * the user's name only in the first sign-in's `user` field.
 */
export class AppleProvider extends CodeFlowProvider {
  readonly id: string;
  readonly kind = 'apple' as const;
  readonly displayName: string;
  readonly responseMode = 'form_post' as const;
  private readonly settings: Extract<ProviderConfig, { kind: 'apple' }>;
  private readonly privateKey: KeyObject;
  private readonly issuer: string;
  // Apple does not document PKCE for web flows; state, nonce and the
  // confidential client secret protect the exchange.
  protected override usesPkce = false;

  constructor(settings: Extract<ProviderConfig, { kind: 'apple' }>, privateKeyPem: string) {
    super();
    this.id = settings.id;
    this.displayName = settings.displayName;
    this.settings = settings;
    this.issuer = settings.issuer ?? 'https://appleid.apple.com';
    this.privateKey = createPrivateKey(privateKeyPem.replace(/\\n/g, '\n'));
    if (this.privateKey.asymmetricKeyType !== 'ec') {
      throw new Error(`provider ${settings.id}: the Apple key must be an EC (P-256) key`);
    }
  }

  /** The client secret JWT Apple expects: ES256, iss team, sub Services ID, aud Apple. */
  clientSecret(now = Math.floor(Date.now() / 1000)): string {
    const header = { alg: 'ES256', kid: this.settings.keyId, typ: 'JWT' };
    const claims = {
      iss: this.settings.teamId,
      iat: now,
      exp: now + 300,
      aud: this.issuer,
      sub: this.settings.clientId,
    };
    const input = `${Buffer.from(JSON.stringify(header)).toString('base64url')}.${Buffer.from(
      JSON.stringify(claims),
    ).toString('base64url')}`;
    const signature = sign('sha256', Buffer.from(input), {
      key: this.privateKey,
      dsaEncoding: 'ieee-p1363',
    });
    return `${input}.${signature.toString('base64url')}`;
  }

  protected discover(): Promise<oidc.Configuration> {
    // A fresh secret for every token request.
    const auth: oidc.ClientAuth = (_as, client, body) => {
      body.set('client_id', client.client_id);
      body.set('client_secret', this.clientSecret());
    };
    return oidc.discovery(new URL(this.issuer), this.settings.clientId, undefined, auth, {
      execute: this.settings.allowInsecureIssuer ? [oidc.allowInsecureRequests] : [],
    });
  }

  protected async authorizationParameters(request: AuthorizationRequest) {
    return {
      redirect_uri: request.redirectUri,
      response_mode: 'form_post',
      scope: 'name email',
      state: request.state,
      nonce: request.nonce,
    };
  }

  protected identityFrom(
    claims: Record<string, unknown>,
    callback: ProviderCallback,
  ): ProviderIdentity {
    const subject = stringClaim(claims['sub']);
    if (!subject || subject.length > 255) throw new Error('ID token has no usable sub');
    let name: string | undefined;
    if (callback.user) {
      try {
        const user = JSON.parse(callback.user) as {
          name?: { firstName?: string; lastName?: string };
        };
        name = [user.name?.firstName, user.name?.lastName].filter(Boolean).join(' ') || undefined;
      } catch {
        // The name is a convenience; ignore a malformed field.
      }
    }
    return { provider: this.id, subject, email: emailOf(claims), name };
  }
}

export class ProviderRegistry {
  private readonly providers = new Map<string, ExternalProvider>();

  constructor(config: PlatformConfig, logger: Logger) {
    for (const settings of config.instance.auth.providers) {
      try {
        const provider =
          settings.kind === 'oidc'
            ? new OidcProvider(
                settings,
                settings.clientSecretEnv ? readSecret(config, settings.clientSecretEnv) : undefined,
              )
            : new AppleProvider(settings, readSecret(config, settings.privateKeyEnv));
        this.providers.set(settings.id, provider);
      } catch (error) {
        // A misconfigured provider is left out rather than taking the API down.
        logger.error({ err: error, provider: settings.id }, 'sign-in provider disabled');
      }
    }
  }

  get(id: string): ExternalProvider | undefined {
    return this.providers.get(id);
  }

  list(): ExternalProvider[] {
    return [...this.providers.values()];
  }
}

export const randomFlowValues = () => ({
  state: oidc.randomState(),
  nonce: oidc.randomNonce(),
  codeVerifier: oidc.randomPKCECodeVerifier(),
});
