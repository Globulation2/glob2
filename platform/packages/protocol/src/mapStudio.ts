import { Type, type Static } from 'typebox';
import { Strict, Uuid } from './common.ts';
const HiveCreditPack = Strict({
  id: Type.String({ minLength: 1, maxLength: 64 }),
  priceId: Type.String({ pattern: '^price_' }),
  credits: Type.Integer({ minimum: 1 }),
  amount: Type.Integer({ minimum: 1 }),
  currency: Type.Union([
    Type.Literal('usd'),
    Type.Literal('cad'),
    Type.Literal('eur'),
    Type.Literal('gbp'),
  ]),
});
export const StudioSettings = Strict({
  width: Type.Union([Type.Literal(128), Type.Literal(256), Type.Literal(512)]),
  height: Type.Union([Type.Literal(128), Type.Literal(256), Type.Literal(512)]),
  players: Type.Integer({ minimum: 2, maximum: 8 }),
});
export type StudioSettings = Static<typeof StudioSettings>;
export const StudioCreate = Strict({ title: Type.String({ minLength: 1, maxLength: 128 }) });
export const StudioMessage = Strict({
  id: Uuid,
  text: Type.String({ minLength: 1, maxLength: 8000 }),
});
export const StudioGenerate = Strict({
  id: Uuid,
  settings: StudioSettings,
  parent: Type.Optional(Uuid),
});
export type StudioGenerate = Static<typeof StudioGenerate>;
export const MapStudioConfig = Strict({
  enabled: Type.Boolean(),
  salesEnabled: Type.Boolean(),
  textModel: Type.Optional(Type.String({ minLength: 1 })),
  imageModel: Type.Optional(Type.String({ minLength: 1 })),
  pipelineVersion: Type.Optional(Type.String({ minLength: 1, maxLength: 64 })),
  packs: Type.Optional(Type.Array(HiveCreditPack, { maxItems: 20 })),
  chatPerHour: Type.Optional(Type.Integer({ minimum: 1, maximum: 1000 })),
  // Required operator ceiling; requests pause when the daily provider-call budget is exhausted.
  providerCallsPerDay: Type.Optional(Type.Integer({ minimum: 1, maximum: 100000 })),
});
export type MapStudioConfig = Static<typeof MapStudioConfig>;
export interface StudioRequest {
  id: string;
  thread_id: string;
  kind: 'chat' | 'generate';
  status:
    | 'queued'
    | 'preparing'
    | 'dispatched'
    | 'processing'
    | 'importing'
    | 'ready'
    | 'failed'
    | 'uncertain';
  input: {
    settings?: StudioSettings;
    parent?: string;
    brief: string;
    messages: { role: 'user' | 'assistant'; text: string }[];
    pipelineVersion: string;
  };
  map_id: string | null;
  map_hash: string | null;
  error: string | null;
  charged: boolean;
  created_at: string;
}
export interface StudioThread {
  id: string;
  title: string;
  messages: { id: string; role: 'user' | 'assistant'; text: string; created_at: string }[];
  requests: StudioRequest[];
  history?: { messagesBefore?: string; requestsBefore?: string };
}
export const studioSchemas = {
  StudioSettings,
  StudioCreate,
  StudioMessage,
  StudioGenerate,
  MapStudioConfig,
};
