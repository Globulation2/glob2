// instance.yaml: operator-editable, non-secret instance settings (name, sign-in
// providers, queues, access policy). Secrets stay in .env and are referenced
// by environment-variable name.
import { Type, type Static } from 'typebox';
import { GeneratorDescriptor, Strict } from '@glob2/protocol';

const ProviderId = Type.String({ pattern: '^[a-z0-9][a-z0-9-]{0,31}$' });

export const OidcProviderConfig = Strict({
  id: ProviderId,
  kind: Type.Literal('oidc'),
  displayName: Type.String({ minLength: 1, maxLength: 64 }),
  /** Issuer URL, or a preset that fills it in. */
  preset: Type.Optional(Type.Union([Type.Literal('google'), Type.Literal('microsoft')])),
  issuer: Type.Optional(Type.String()),
  /** microsoft preset: tenant (default `common`: work, school and personal accounts). */
  tenant: Type.Optional(Type.String({ pattern: '^[A-Za-z0-9.-]{1,64}$' })),
  clientId: Type.String({ minLength: 1 }),
  /** Omit for public clients (PKCE only). */
  clientSecretEnv: Type.Optional(Type.String({ pattern: '^[A-Z][A-Z0-9_]*$' })),
  scopes: Type.Optional(Type.Array(Type.String())),
  /** Development and tests only: allow an http:// issuer. */
  allowInsecureIssuer: Type.Optional(Type.Boolean()),
});
export type OidcProviderConfig = Static<typeof OidcProviderConfig>;

export const AppleProviderConfig = Strict({
  id: ProviderId,
  kind: Type.Literal('apple'),
  displayName: Type.String({ minLength: 1, maxLength: 64 }),
  clientId: Type.String({ minLength: 1, description: 'Services ID.' }),
  teamId: Type.String({ minLength: 1 }),
  keyId: Type.String({ minLength: 1 }),
  /** Environment variable holding the .p8 private key (PEM). */
  privateKeyEnv: Type.String({ pattern: '^[A-Z][A-Z0-9_]*$' }),
  /** Default https://appleid.apple.com; overridden only in tests. */
  issuer: Type.Optional(Type.String()),
  allowInsecureIssuer: Type.Optional(Type.Boolean()),
});
export type AppleProviderConfig = Static<typeof AppleProviderConfig>;
export type ProviderConfig = OidcProviderConfig | AppleProviderConfig;

export const QueueConfig = Strict({
  id: Type.String({ pattern: '^[a-z0-9][a-z0-9-]{0,31}$' }),
  name: Type.String({ minLength: 1, maxLength: 64 }),
  /** 1v1, or 2v2 solo queue (four players, two sides). */
  mode: Type.Union([Type.Literal('1v1'), Type.Literal('2v2')]),
  /** Rated queues update the ladder named by the queue id; guests cannot join them. */
  rated: Type.Boolean(),
  /** Seconds before an empty seat is filled by the closest-rated AI; omit to never backfill. */
  aiBackfillSeconds: Type.Optional(Type.Integer({ minimum: 0 })),
  /**
   * Accept prompt for all-human groups, in seconds; 0 starts at once. Default:
   * 10 for rated queues, 0 otherwise. AI-backfilled groups never prompt.
   */
  acceptSeconds: Type.Optional(Type.Integer({ minimum: 0, maximum: 120 })),
  /** Queue ban after declining or ignoring an accept prompt (default 60). */
  declineCooldownSeconds: Type.Optional(Type.Integer({ minimum: 0, maximum: 3600 })),
  /**
   * Soft region preference (default 100 ms + 5 ms/s, any region after 30 s):
   * players sharing a good relay are paired first; the matchmaker always falls
   * back to the best relay that exists.
   */
  rttPreference: Type.Optional(
    Strict({
      initialMs: Type.Optional(Type.Integer({ minimum: 0, maximum: 60000 })),
      perSecondMs: Type.Optional(Type.Number({ minimum: 0 })),
      anyRegionAfterSeconds: Type.Optional(Type.Integer({ minimum: 0, maximum: 3600 })),
    }),
  ),
  /**
   * Opt-in hard RTT cap for instances with relays near every player; off by
   * default. When set, a player no relay serves within it can only be matched
   * through AI backfill.
   */
  maxRttMs: Type.Optional(Type.Integer({ minimum: 1, maximum: 60000 })),
  /** Accepted skill difference in display points, widening with wait time. */
  ratingWindow: Type.Optional(
    Strict({
      initial: Type.Optional(Type.Number({ minimum: 0 })),
      perSecond: Type.Optional(Type.Number({ minimum: 0 })),
      max: Type.Optional(Type.Number({ minimum: 0 })),
    }),
  ),
  /** AIs that may backfill seats (default: every rated AI). */
  aiPool: Type.Optional(
    Type.Array(
      Type.Union(
        (
          ['maxima', 'cabino', 'nicowar', 'cortex', 'warrush', 'econo', 'castor', 'numbi'] as const
        ).map((id) => Type.Literal(id)),
      ),
      { uniqueItems: true },
    ),
  ),
  /**
   * Map pool: generator descriptors without seed (the platform picks one per
   * match). Default: the 128x128 fair-by-construction generators for the mode.
   */
  mapPool: Type.Optional(Type.Array(Type.Omit(GeneratorDescriptor, ['seed']), { minItems: 1 })),
});
export type QueueConfig = Static<typeof QueueConfig>;

export const InstanceConfig = Strict({
  name: Type.String({ minLength: 1, maxLength: 128 }),
  guests: Strict({ enabled: Type.Boolean() }),
  auth: Strict({
    providers: Type.Array(Type.Union([OidcProviderConfig, AppleProviderConfig])),
    local: Strict({
      enabled: Type.Boolean(),
      /** Whether new local accounts may be created (default true when enabled). */
      allowRegistration: Type.Optional(Type.Boolean()),
    }),
    /** Access token lifetime in minutes (default 10). */
    accessTokenMinutes: Type.Optional(Type.Integer({ minimum: 1, maximum: 60 })),
    /** Refresh token lifetime in days (default 60). */
    refreshTokenDays: Type.Optional(Type.Integer({ minimum: 1, maximum: 365 })),
    /** Browser sign-in attempt lifetime in minutes (default 10). */
    handoffMinutes: Type.Optional(Type.Integer({ minimum: 1, maximum: 60 })),
    /** Web session (cookie) lifetime in days (default 30). */
    webSessionDays: Type.Optional(Type.Integer({ minimum: 1, maximum: 365 })),
  }),
  accounts: Type.Optional(
    Strict({
      /** Days between renames of a registered account (default 30; 0 disables the limit). */
      renameIntervalDays: Type.Optional(Type.Integer({ minimum: 0, maximum: 3650 })),
    }),
  ),
  web: Type.Optional(
    Strict({
      /**
       * Extra origins (besides PUBLIC_ORIGIN) allowed to open /realtime from a
       * browser and to send cookie-authenticated requests, e.g. a separate web
       * client host. Native clients send no Origin and are always allowed.
       */
      allowedOrigins: Type.Optional(
        Type.Array(Type.String({ pattern: '^https?://[^/\\s]+$' }), { maxItems: 32 }),
      ),
      /**
       * Where the browser build of the game is served; invite pages link to it
       * with `?join=<code>`. Default `<PUBLIC_ORIGIN>/play/`.
       */
      browserClientUrl: Type.Optional(Type.String({ pattern: '^https?://[^\\s]+$' })),
    }),
  ),
  limits: Type.Optional(
    Strict({
      /** Sign-in, refresh and registration requests per client IP per minute (default 30). */
      authPerMinute: Type.Optional(Type.Integer({ minimum: 1 })),
      /** New guest accounts per client IP per hour (default 20). */
      guestsPerHour: Type.Optional(Type.Integer({ minimum: 1 })),
      /** Other API requests per client IP per minute (default 600). */
      apiPerMinute: Type.Optional(Type.Integer({ minimum: 1 })),
      /** Realtime messages per socket: sustained per second (default 10), burst (default 40). */
      realtimePerSecond: Type.Optional(Type.Number({ exclusiveMinimum: 0 })),
      realtimeBurst: Type.Optional(Type.Integer({ minimum: 1 })),
      /** Open realtime sockets per client IP per replica (default 20). */
      realtimeConnectionsPerIp: Type.Optional(Type.Integer({ minimum: 1 })),
    }),
  ),
  access: Strict({
    policy: Type.Literal('allow-all', {
      description: 'AccessPolicy implementation; only allow-all exists today.',
    }),
  }),
  queues: Type.Array(QueueConfig),
});
export type InstanceConfig = Static<typeof InstanceConfig>;

/** Used when no instance.yaml exists: guests only, no queues. */
export const DEFAULT_INSTANCE_CONFIG: InstanceConfig = {
  name: 'Globulation 2',
  guests: { enabled: true },
  auth: { providers: [], local: { enabled: false } },
  access: { policy: 'allow-all' },
  queues: [],
};
