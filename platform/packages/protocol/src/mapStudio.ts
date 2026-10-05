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
export const StudioCreate = Strict({
  title: Type.String({ minLength: 1, maxLength: 128 }),
  /** Lets a prompt-first client recover an interrupted project-creation response. */
  id: Type.Optional(Uuid),
});
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
  /** Cursor and brief are optional for history imported before live progress existed. */
  cursor?: string;
  brief?: string;
  messages: { id: string; role: 'user' | 'assistant'; text: string; created_at: string }[];
  requests: StudioRequest[];
  history?: { messagesBefore?: string; requestsBefore?: string };
}
export type StudioStageId = 'prepare' | 'terrain' | 'build' | 'checks' | 'ready';
export interface StudioStageProgress {
  id: StudioStageId;
  label: string;
  status: 'pending' | 'running' | 'complete' | 'failed';
  detail?: string;
  completedAt?: string;
}
export interface StudioCheck {
  id: string;
  label: string;
  status: 'pending' | 'running' | 'passed' | 'failed' | 'not-evaluated';
  detail?: string;
  colony?: number;
  location?: { x: number; y: number };
}
export interface StudioArtifact {
  id: string;
  requestId: string;
  stage: StudioStageId;
  kind: 'reference' | 'generated' | 'crop' | 'categorical' | 'preview';
  label: string;
  url: string;
  width?: number;
  height?: number;
}
export interface StudioProgress {
  requestId: string;
  stages: StudioStageProgress[];
  artifacts: StudioArtifact[];
  checks: StudioCheck[];
  historical: boolean;
}
interface StudioEventBase {
  id: string;
  requestId: string | null;
  createdAt: string;
}
export type StudioEvent = StudioEventBase &
  (
    | { type: 'state' | 'complete'; payload: { status: StudioRequest['status'] } }
    | { type: 'stage'; payload: StudioStageProgress }
    | { type: 'artifact'; payload: StudioArtifact }
    | { type: 'check'; payload: StudioCheck }
    | { type: 'message'; payload: { messageId: string } }
  );
const StageId = Type.Union(
  ['prepare', 'terrain', 'build', 'checks', 'ready'].map((id) => Type.Literal(id)),
);
export const StudioStageProgress = Strict({
  id: StageId,
  label: Type.String(),
  status: Type.Union(['pending', 'running', 'complete', 'failed'].map((s) => Type.Literal(s))),
  detail: Type.Optional(Type.String()),
  completedAt: Type.Optional(Type.String()),
});
export const StudioCheck = Strict({
  id: Type.String(),
  label: Type.String(),
  status: Type.Union(
    ['pending', 'running', 'passed', 'failed', 'not-evaluated'].map((s) => Type.Literal(s)),
  ),
  detail: Type.Optional(Type.String()),
  colony: Type.Optional(Type.Integer({ minimum: 0 })),
  location: Type.Optional(Strict({ x: Type.Number(), y: Type.Number() })),
});
export const StudioArtifact = Strict({
  id: Type.String(),
  requestId: Uuid,
  stage: StageId,
  kind: Type.Union(
    ['reference', 'generated', 'crop', 'categorical', 'preview'].map((s) => Type.Literal(s)),
  ),
  label: Type.String(),
  url: Type.String(),
  width: Type.Optional(Type.Integer({ minimum: 1 })),
  height: Type.Optional(Type.Integer({ minimum: 1 })),
});
export const StudioProgress = Strict({
  requestId: Uuid,
  stages: Type.Array(StudioStageProgress),
  artifacts: Type.Array(StudioArtifact),
  checks: Type.Array(StudioCheck),
  historical: Type.Boolean(),
});
const EventBase = {
  id: Type.String({ pattern: '^[0-9]+$' }),
  requestId: Type.Union([Uuid, Type.Null()]),
  createdAt: Type.String(),
};
export const StudioEvent = Type.Union([
  Strict({
    ...EventBase,
    type: Type.Union([Type.Literal('state'), Type.Literal('complete')]),
    payload: Strict({
      status: Type.Union(
        [
          'queued',
          'preparing',
          'dispatched',
          'processing',
          'importing',
          'ready',
          'failed',
          'uncertain',
        ].map((s) => Type.Literal(s)),
      ),
    }),
  }),
  Strict({ ...EventBase, type: Type.Literal('stage'), payload: StudioStageProgress }),
  Strict({ ...EventBase, type: Type.Literal('artifact'), payload: StudioArtifact }),
  Strict({ ...EventBase, type: Type.Literal('check'), payload: StudioCheck }),
  Strict({ ...EventBase, type: Type.Literal('message'), payload: Strict({ messageId: Uuid }) }),
]);
export const studioSchemas = {
  StudioSettings,
  StudioCreate,
  StudioMessage,
  StudioGenerate,
  MapStudioConfig,
  StudioStageProgress,
  StudioCheck,
  StudioArtifact,
  StudioProgress,
  StudioEvent,
};
