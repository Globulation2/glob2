import { Type, type Static } from 'typebox';
import { MusicLicense } from './music.ts';
import { Strict, Uuid } from './common.ts';
import { StudioCreate, StudioMessage, MapStudioConfig } from './mapStudio.ts';
export const MusicStudioCreate = StudioCreate;
export const MusicStudioMessage = StudioMessage;
export const MusicStudioSettings = Strict({
  pipeline: Type.Union([Type.Literal('acoustic-v1'), Type.Literal('synth-v1')]),
  license: Type.Optional(MusicLicense),
  seed: Type.Integer({ minimum: 0, maximum: 2147483647 }),
});
export type MusicStudioSettings = Static<typeof MusicStudioSettings>;
export const MusicStudioGenerate = Strict({
  id: Uuid,
  settings: MusicStudioSettings,
  parent: Type.Optional(Uuid),
});
export type MusicStudioGenerate = Static<typeof MusicStudioGenerate>;
export const MusicStudioConfig = Strict({
  enabled: Type.Boolean(),
  salesEnabled: Type.Boolean(),
  textModel: Type.Optional(Type.String({ minLength: 1 })),
  pipelineVersion: Type.Optional(Type.Literal('music-v1')),
  packs: MapStudioConfig.properties.packs,
  chatPerHour: Type.Optional(Type.Integer({ minimum: 1, maximum: 1000 })),
  providerCallsPerDay: Type.Optional(Type.Integer({ minimum: 1, maximum: 100000 })),
  maxCalls: Type.Optional(Type.Integer({ minimum: 3, maximum: 30 })),
  maxOutputTokens: Type.Optional(Type.Integer({ minimum: 4000, maximum: 32000 })),
  maxTotalTokens: Type.Optional(Type.Integer({ minimum: 10000, maximum: 500000 })),
  maxSourceBytes: Type.Optional(Type.Integer({ minimum: 4096, maximum: 131072 })),
  maxOutputBytes: Type.Optional(Type.Integer({ minimum: 1048576, maximum: 134217728 })),
  timeoutSeconds: Type.Optional(Type.Integer({ minimum: 60, maximum: 3600 })),
});
export type MusicStudioConfig = Static<typeof MusicStudioConfig>;
export interface MusicStudioRequest {
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
    settings?: MusicStudioSettings;
    parent?: string;
    brief: string;
    messages: { role: 'user' | 'assistant'; text: string }[];
    pipelineVersion: string;
    config?: MusicStudioConfig;
  };
  release_id: string | null;
  error: string | null;
  charged: boolean;
  created_at: string;
}
export interface MusicStudioThread {
  id: string;
  title: string;
  cursor?: string;
  brief?: string;
  messages: { id: string; role: 'user' | 'assistant'; text: string; created_at: string }[];
  requests: MusicStudioRequest[];
  history?: { messagesBefore?: string; requestsBefore?: string };
}
export const MusicStudioStageId = Type.Union([
  Type.Literal('prepare'),
  Type.Literal('score'),
  Type.Literal('render'),
  Type.Literal('master'),
  Type.Literal('checks'),
  Type.Literal('repair'),
  Type.Literal('ready'),
]);
export type MusicStudioStageId = Static<typeof MusicStudioStageId>;
export const MusicStudioStageProgress = Strict({
  id: MusicStudioStageId,
  label: Type.String(),
  status: Type.Union([
    Type.Literal('pending'),
    Type.Literal('running'),
    Type.Literal('complete'),
    Type.Literal('failed'),
  ]),
  detail: Type.Optional(Type.String()),
  completedAt: Type.Optional(Type.String()),
});
export type MusicStudioStageProgress = Static<typeof MusicStudioStageProgress>;
export const MusicStudioMeasure = Strict({
  name: Type.String(),
  status: Type.Union([
    Type.Literal('pass'),
    Type.Literal('warn'),
    Type.Literal('fail'),
    Type.Literal('waived'),
    Type.Literal('info'),
    Type.Literal('skip'),
  ]),
  value: Type.Unknown(),
  threshold: Type.String(),
  detail: Type.String(),
  unit: Type.Optional(Type.String()),
});
export const MusicStudioCheck = Strict({
  id: Type.String(),
  label: Type.String(),
  attempt: Type.Integer({ minimum: 1, maximum: 3 }),
  status: Type.Union([
    Type.Literal('pending'),
    Type.Literal('running'),
    Type.Literal('pass'),
    Type.Literal('warn'),
    Type.Literal('fail'),
    Type.Literal('waived'),
    Type.Literal('info'),
    Type.Literal('skip'),
  ]),
  detail: Type.Optional(Type.String()),
  measures: Type.Array(MusicStudioMeasure),
});
export type MusicStudioCheck = Static<typeof MusicStudioCheck>;
export const MusicStudioArtifact = Strict({
  id: Uuid,
  requestId: Uuid,
  stage: MusicStudioStageId,
  kind: Type.Union([Type.Literal('preview'), Type.Literal('source'), Type.Literal('report')]),
  label: Type.String(),
  url: Type.String(),
});
export type MusicStudioArtifact = Static<typeof MusicStudioArtifact>;
export const MusicStudioProgress = Strict({
  requestId: Uuid,
  stages: Type.Array(MusicStudioStageProgress),
  artifacts: Type.Array(MusicStudioArtifact),
  notes: Type.Array(Strict({ text: Type.String(), attempt: Type.Integer() })),
  checks: Type.Array(MusicStudioCheck),
  historical: Type.Boolean(),
});
export type MusicStudioProgress = Static<typeof MusicStudioProgress>;
const base = {
  id: Type.String({ pattern: '^[0-9]+$' }),
  requestId: Type.Union([Uuid, Type.Null()]),
  createdAt: Type.String(),
};
export const MusicStudioEvent = Type.Union([
  Strict({
    ...base,
    type: Type.Union([Type.Literal('state'), Type.Literal('complete')]),
    payload: Strict({ status: Type.String() }),
  }),
  Strict({ ...base, type: Type.Literal('stage'), payload: MusicStudioStageProgress }),
  Strict({ ...base, type: Type.Literal('check'), payload: MusicStudioCheck }),
  Strict({ ...base, type: Type.Literal('artifact'), payload: MusicStudioArtifact }),
  Strict({ ...base, type: Type.Literal('message'), payload: Strict({ messageId: Uuid }) }),
  Strict({
    ...base,
    type: Type.Literal('text'),
    payload: Strict({ text: Type.String({ maxLength: 16000 }), attempt: Type.Integer() }),
  }),
]);
export type MusicStudioEvent = Static<typeof MusicStudioEvent>;
export const musicStudioSchemas = {
  MusicStudioCreate,
  MusicStudioMessage,
  MusicStudioSettings,
  MusicStudioGenerate,
  MusicStudioConfig,
  MusicStudioStageProgress,
  MusicStudioCheck,
  MusicStudioArtifact,
  MusicStudioProgress,
  MusicStudioEvent,
};
