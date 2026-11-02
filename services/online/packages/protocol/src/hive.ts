// Hive Mind v1. Private execution documents are never player-facing reports.
import { Type, type Static } from 'typebox';
import { Strict, Uuid, SeatIndex } from './common.ts';
export const HIVE_API_VERSION = 1;
export const HIVE_LIMITS = Object.freeze({
  sourceBytes: 128 * 1024,
  stateBytes: 1024 * 1024,
  outputBytes: 64 * 1024,
  programs: 8,
  orders: 32,
  queuedOrders: 64,
  minIntervalTicks: 25,
  wakesPerMinute: 6,
  maxModelSteps: 20,
  maxOutputTokens: 4096,
});
const Tick = Type.Integer({ minimum: 0, maximum: 4294967295 });
const Revision = Type.Integer({ minimum: 1, maximum: 2147483647 });
const Source = Type.String({
  minLength: 1,
  maxLength: HIVE_LIMITS.sourceBytes,
  pattern: '^[^\\u0000]*$',
});
export const HiveContext = Strict({ matchId: Uuid, seat: SeatIndex });
export const HiveCommand = Strict({
  id: Uuid,
  text: Type.String({ minLength: 1, maxLength: 4096 }),
  ongoing: Type.Boolean(),
});
export const HiveProgram = Strict({
  id: Uuid,
  revision: Revision,
  name: Type.String({ minLength: 1, maxLength: 80 }),
  description: Type.String({ maxLength: 512 }),
  source: Source,
  intervalTicks: Type.Integer({ minimum: 25, maximum: 2147483647 }),
});
export type HiveProgram = Static<typeof HiveProgram>;
export const HiveTool = Type.Union([
  Strict({ kind: Type.Literal('execute'), source: Source }),
  Strict({ kind: Type.Literal('install'), program: HiveProgram }),
  Strict({
    kind: Type.Literal('replace'),
    program: HiveProgram,
    expectedRevision: Revision,
    migration: Type.Optional(Source),
  }),
  Strict({ kind: Type.Literal('list') }),
  Strict({
    kind: Type.Union([Type.Literal('pause'), Type.Literal('resume'), Type.Literal('remove')]),
    programId: Uuid,
    expectedRevision: Revision,
  }),
]);
export type HiveTool = Static<typeof HiveTool>;
export const HiveWake = Strict({
  eventId: Uuid,
  programId: Uuid,
  revision: Revision,
  tick: Tick,
  key: Type.String({ minLength: 1, maxLength: 80 }),
  reason: Type.String({ minLength: 1, maxLength: 512 }),
  // JSON text permits the same byte/depth validation in both native and web clients.
  data: Type.String({ maxLength: HIVE_LIMITS.outputBytes }),
});
export type HiveWake = Static<typeof HiveWake>;
export const HiveClientPoll = Strict({
  clientId: Uuid,
  lease: Type.Optional(Uuid),
  tick: Tick,
  caughtUp: Type.Boolean(),
});
export const HiveToolResult = Strict({
  operationId: Uuid,
  lease: Uuid,
  tick: Tick,
  status: Type.Union([
    Type.Literal('completed'),
    Type.Literal('failed'),
    Type.Literal('uncertain'),
  ]),
  output: Type.String({ maxLength: HIVE_LIMITS.outputBytes }),
});
export const HiveCheckout = Strict({ pack: Type.String({ minLength: 1, maxLength: 64 }) });
export const hiveSchemas = {
  HiveContext,
  HiveCommand,
  HiveProgram,
  HiveTool,
  HiveWake,
  HiveClientPoll,
  HiveToolResult,
  HiveCheckout,
};
export const HiveConfig = Strict({
  enabled: Type.Boolean(),
  model: Type.Optional(Type.String({ minLength: 1, maxLength: 128 })),
  rate: Type.Optional(
    Strict({
      version: Type.String({ minLength: 1, maxLength: 64 }),
      input: Type.Integer({ minimum: 1, maximum: 1000000000 }),
      cachedInput: Type.Integer({ minimum: 0, maximum: 1000000000 }),
      cacheWrite: Type.Optional(Type.Integer({ minimum: 0, maximum: 1000000000 })),
      output: Type.Integer({ minimum: 1, maximum: 1000000000 }),
    }),
  ),
  salesEnabled: Type.Optional(Type.Boolean()),
  packs: Type.Optional(
    Type.Array(
      Strict({
        id: Type.String({ minLength: 1, maxLength: 64 }),
        priceId: Type.String({ pattern: '^price_' }),
        credits: Type.Integer({ minimum: 1, maximum: 1000000000 }),
        amount: Type.Integer({ minimum: 1, maximum: 1000000000 }),
        currency: Type.String({ pattern: '^[a-z]{3}$' }),
      }),
      { maxItems: 16 },
    ),
  ),
});
