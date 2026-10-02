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
  clientId: Type.String({ minLength: 1 }),
  clientSecretEnv: Type.String({ pattern: '^[A-Z][A-Z0-9_]*$' }),
  scopes: Type.Optional(Type.Array(Type.String())),
});

export const AppleProviderConfig = Strict({
  id: ProviderId,
  kind: Type.Literal('apple'),
  displayName: Type.String({ minLength: 1, maxLength: 64 }),
  clientId: Type.String({ minLength: 1, description: 'Services ID.' }),
  teamId: Type.String({ minLength: 1 }),
  keyId: Type.String({ minLength: 1 }),
  privateKeyEnv: Type.String({ pattern: '^[A-Z][A-Z0-9_]*$' }),
});

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
    local: Strict({ enabled: Type.Boolean() }),
  }),
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
