import { Type, type Static } from 'typebox';
import { Strict, Uuid, Sha256Hex } from './common.ts';
import { StudioCreditPack } from './studioCommon.ts';
import type { ValidateSetResult } from './sets.ts';
export const TERRAIN_STUDIO_MAX_ENTRIES = 12;
export const TerrainStudioConfig = Strict({
  enabled: Type.Boolean(),
  salesEnabled: Type.Boolean(),
  textModel: Type.Optional(Type.String({ minLength: 1 })),
  imageModel: Type.Optional(Type.String({ minLength: 1 })),
  pipelineVersion: Type.Optional(Type.Literal('terrain-v1')),
  providerCallsPerDay: Type.Optional(Type.Integer({ minimum: 1, maximum: 100000 })),
  chatPerHour: Type.Optional(Type.Integer({ minimum: 1, maximum: 1000 })),
  maxOutputTokens: Type.Optional(Type.Integer({ minimum: 4000, maximum: 32000 })),
  timeoutSeconds: Type.Optional(Type.Integer({ minimum: 60, maximum: 3600 })),
  packs: Type.Optional(Type.Array(StudioCreditPack, { maxItems: 20 })),
});
export type TerrainStudioConfig = Static<typeof TerrainStudioConfig>;
export const TerrainStudioCreate = Strict({
  id: Uuid,
  title: Type.String({ minLength: 1, maxLength: 128 }),
  draftId: Type.Optional(Uuid),
  versionId: Type.Optional(Uuid),
});
export const TerrainStudioTurn = Strict({
  id: Uuid,
  text: Type.String({ minLength: 1, maxLength: 8000 }),
  expectedRevision: Type.Integer({ minimum: 0 }),
  references: Type.Array(Sha256Hex, { maxItems: 4, uniqueItems: true }),
});
export type TerrainStudioTurn = Static<typeof TerrainStudioTurn>;
export const TerrainStudioAdopt = Strict({ expectedRevision: Type.Integer({ minimum: 0 }) });
export type TerrainStudioStageId = 'prepare' | 'artwork' | 'assemble' | 'checks' | 'ready';
export interface TerrainStudioStageProgress {
  id: TerrainStudioStageId;
  label: string;
  status: 'pending' | 'running' | 'complete' | 'failed';
  detail?: string;
  completedAt?: string;
}
export interface TerrainStudioCheck {
  id: string;
  label: string;
  status: 'pass' | 'warn' | 'fail';
  detail?: string;
}
export interface TerrainStudioArtifact {
  id: string;
  requestId: string;
  stage: TerrainStudioStageId;
  kind: 'reference' | 'source' | 'preview' | 'report';
  label: string;
  url: string;
}
export interface TerrainStudioEvent {
  id: string;
  requestId: string | null;
  createdAt: string;
  type: 'state' | 'stage' | 'check' | 'artifact' | 'message' | 'text' | 'complete';
  payload:
    | TerrainStudioStageProgress
    | TerrainStudioCheck
    | TerrainStudioArtifact
    | { status: string }
    | { messageId: string }
    | { text: string; attempt: number };
}
export interface TerrainStudioRequest {
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
    brief: string;
    messages: { role: 'user' | 'assistant'; text: string }[];
    pipelineVersion: string;
    submission: TerrainStudioTurn;
  };
  error: string | null;
  charged: boolean;
  created_at: string;
}
export interface TerrainStudioRevision {
  requestId: string;
  title: string;
  applied: boolean;
  baseRevision: number;
  report: ValidateSetResult;
}
export interface TerrainStudioThread {
  draftHistory?: { revision: number; title: string; created_at: string }[];
  id: string;
  title: string;
  draftId: string;
  brief?: string;
  cursor?: string;
  messages: { id: string; role: 'user' | 'assistant'; text: string; created_at: string }[];
  references: { hash: string; url: string; label: string }[];
  requests: TerrainStudioRequest[];
  revisions: TerrainStudioRevision[];
}
export interface TerrainStudioProgress {
  requestId: string;
  stages: TerrainStudioStageProgress[];
  artifacts: TerrainStudioArtifact[];
  checks: TerrainStudioCheck[];
  notes: { text: string; attempt: number }[];
  historical: boolean;
}
export const terrainStudioSchemas = {
  TerrainStudioConfig,
  TerrainStudioCreate,
  TerrainStudioTurn,
  TerrainStudioAdopt,
};
