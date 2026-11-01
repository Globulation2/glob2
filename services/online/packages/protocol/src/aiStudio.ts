import { Type, type Static } from 'typebox';
import { Strict, Uuid } from './common.ts';
import { HiveConfig } from './hive.ts';
import type {
  StudioAccount,
  StudioDetail,
  StudioProject,
  CodingStudioRequest,
  StudioRevision,
} from './codingStudio.ts';
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
export type AiStudioRevision = StudioRevision;
export type AiStudioRequest = CodingStudioRequest;
export type AiStudioProject = StudioProject;
export type AiStudioAccount = StudioAccount;
export type AiStudioDetail = StudioDetail<{
  id: string;
  revision: number;
  seed: number;
  opponent: string;
  summary: string;
}>;
