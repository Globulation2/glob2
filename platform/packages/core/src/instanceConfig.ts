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
  mode: Type.Union([Type.Literal('1v1'), Type.Literal('2v2')]),
  rated: Type.Boolean(),
  /** Seconds before an empty seat is filled by the closest-rated AI; omit to never backfill. */
  aiBackfillSeconds: Type.Optional(Type.Integer({ minimum: 0 })),
  /** Map pool: generator descriptors without seed (the platform picks one per match). */
  mapPool: Type.Array(Type.Omit(GeneratorDescriptor, ['seed']), { minItems: 1 }),
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
