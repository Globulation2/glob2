import { Type, type Static } from 'typebox';
import { Strict, Uuid } from './common.ts';
import { HiveConfig } from './hive.ts';
export const AI_STUDIO_SOURCE_BYTES = 128 * 1024;
export const AiStudioConfig = Strict({
  ...HiveConfig.properties,
  maxRequestCredits: Type.Integer({ minimum: 1, maximum: 1000000000 }),
  maxOutputTokens: Type.Integer({ minimum: 1024, maximum: 32768 }),
});
const Revision = Type.Integer({ minimum: 1, maximum: 2147483647 });
const Source = Type.String({ minLength: 1, maxLength: AI_STUDIO_SOURCE_BYTES });
export const AiStudioCreate = Strict({
  title: Type.String({ minLength: 1, maxLength: 128 }),
  source: Type.Optional(Source),
  versionId: Type.Optional(Uuid),
});
export const AiStudioSave = Strict({
  expectedRevision: Revision,
  source: Type.Optional(Source),
  restoreRevision: Type.Optional(Revision),
  title: Type.Optional(Type.String({ minLength: 1, maxLength: 128 })),
  reason: Type.Optional(Type.Union([Type.Literal('manual'), Type.Literal('import')])),
});
export const AiStudioCommand = Strict({
  id: Uuid,
  expectedRevision: Revision,
  text: Type.String({ minLength: 1, maxLength: 16000 }),
  diagnostics: Type.Optional(Type.String({ maxLength: 16000 })),
  budget: Type.Integer({ minimum: 1, maximum: 1000000000 }),
});
export const AiStudioRun = Strict({
  id: Uuid,
  expectedRevision: Revision,
  seed: Type.Integer({ minimum: 0, maximum: 4294967295 }),
  opponent: Type.Union([Type.Literal('numbi'), Type.Literal('nicowar')]),
});
export const aiStudioSchemas = { AiStudioCreate, AiStudioSave, AiStudioCommand, AiStudioRun };
export type AiStudioCommand = Static<typeof AiStudioCommand>;
export interface AiStudioRevision {
  revision: number;
  source: string;
  hash: string;
  reason: string;
  created_at: string;
}
export interface AiStudioRequest {
  id: string;
  base_revision: number;
  prompt: string;
  diagnostics: string;
  budget: number;
  status: string;
  response: string;
  error: string | null;
  charged: number | null;
}
export interface AiStudioProject {
  id: string;
  title: string;
  revision: number;
  updated_at: string;
}
export interface AiStudioDetail extends AiStudioProject {
  current: AiStudioRevision;
  revisions: Omit<AiStudioRevision, 'source'>[];
  requests: AiStudioRequest[];
  cursor: string;
  runs: { id: string; revision: number; seed: number; opponent: string; summary: string }[];
}
export interface AiStudioAccount {
  enabled: boolean;
  model: string;
  maxRequestCredits: number;
  rate: { input: number; cachedInput: number; output: number } | null;
  balance: number;
  reserved: number;
  available: number;
  packs: { id: string; credits: number; amount: number; currency: string }[];
}
