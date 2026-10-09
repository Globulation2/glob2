import { Type, type Static } from 'typebox';
import { Strict, Uuid, Sha256Hex } from './common.ts';
import { StudioCreditPack } from './studioCommon.ts';
import type { EngineJobOutput } from './jobs.ts';
import type { BuildingPackage } from './buildings.ts';
export type BuildingStudioReport = EngineJobOutput<'validate-buildings'>;
export const BUILDING_STUDIO_MAX_ENTRIES = 12;
export const BuildingAiStudioConfig = Strict({
  enabled: Type.Boolean(),
  salesEnabled: Type.Boolean(),
  textModel: Type.Optional(Type.String({ minLength: 1 })),
  imageModel: Type.Optional(Type.String({ minLength: 1 })),
  pipelineVersion: Type.Optional(Type.Literal('building-v1')),
  providerCallsPerDay: Type.Optional(Type.Integer({ minimum: 1, maximum: 100000 })),
  chatPerHour: Type.Optional(Type.Integer({ minimum: 1, maximum: 1000 })),
  maxOutputTokens: Type.Optional(Type.Integer({ minimum: 4000, maximum: 32000 })),
  timeoutSeconds: Type.Optional(Type.Integer({ minimum: 60, maximum: 3600 })),
  packs: Type.Optional(Type.Array(StudioCreditPack, { maxItems: 20 })),
});
export type BuildingAiStudioConfig = Static<typeof BuildingAiStudioConfig>;
export const BuildingAiStudioCreate = Strict({
  id: Uuid,
  title: Type.String({ minLength: 1, maxLength: 128 }),
  draftId: Type.Optional(Uuid),
});
export const BuildingAiStudioTurn = Strict({
  id: Uuid,
  text: Type.String({ minLength: 1, maxLength: 8000 }),
  expectedRevision: Uuid,
  references: Type.Array(Sha256Hex, { maxItems: 4, uniqueItems: true }),
});
export type BuildingAiStudioTurn = Static<typeof BuildingAiStudioTurn>;
export const BuildingAiStudioAdopt = Strict({ expectedRevision: Uuid });
export type BuildingAiStudioStageId = 'prepare' | 'artwork' | 'assemble' | 'checks' | 'ready';
export interface BuildingAiStudioStageProgress {
  id: BuildingAiStudioStageId;
  label: string;
  status: 'pending' | 'running' | 'complete' | 'failed';
  detail?: string;
  completedAt?: string;
}
export interface BuildingAiStudioCheck {
  id: string;
  label: string;
  status: 'pass' | 'warn' | 'fail';
  detail?: string;
}
export interface BuildingAiStudioArtifact {
  id: string;
  requestId: string;
  stage: BuildingAiStudioStageId;
  kind: 'reference' | 'source' | 'preview' | 'report';
  label: string;
  url: string;
}
export interface BuildingAiStudioEvent {
  id: string;
  requestId: string | null;
  createdAt: string;
  type: 'state' | 'stage' | 'check' | 'artifact' | 'message' | 'text' | 'complete';
  payload:
    | BuildingAiStudioStageProgress
    | BuildingAiStudioCheck
    | BuildingAiStudioArtifact
    | { status: string }
    | { messageId: string }
    | { text: string; attempt: number };
}
export interface BuildingAiStudioRequest {
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
    submission: BuildingAiStudioTurn;
  };
  error: string | null;
  charged: boolean;
  created_at: string;
}
export interface BuildingAiStudioRevision {
  appliedRevision?: string | null;
  requestId: string;
  title: string;
  applied: boolean;
  baseRevision: string;
  report: BuildingStudioReport;
  package: BuildingPackage;
}
export interface BuildingAiStudioThread {
  draftHistory?: { revision: string; title: string; created_at: string }[];
  id: string;
  title: string;
  draftId: string;
  brief?: string;
  cursor?: string;
  messages: { id: string; role: 'user' | 'assistant'; text: string; created_at: string }[];
  references: { hash: string; url: string; label: string }[];
  requests: BuildingAiStudioRequest[];
  revisions: BuildingAiStudioRevision[];
}
export interface BuildingAiStudioProgress {
  requestId: string;
  stages: BuildingAiStudioStageProgress[];
  artifacts: BuildingAiStudioArtifact[];
  checks: BuildingAiStudioCheck[];
  notes: { text: string; attempt: number }[];
  historical: boolean;
}
export const buildingStudioSchemas = {
  BuildingAiStudioConfig,
  BuildingAiStudioCreate,
  BuildingAiStudioTurn,
  BuildingAiStudioAdopt,
};
