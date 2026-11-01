// Versioned, local-play JavaScript controllers. Source bytes are never rewritten.
import { Type, type Static } from 'typebox';
import { Open, Strict, Uuid, Sha256Hex, Timestamp } from './common.ts';
export const AI_TAGS = [
  'Balanced',
  'Rush',
  'Defensive',
  'Economy',
  'Expansion',
  'Experimental',
  'Beginner-friendly',
] as const;
export const AI_CHECKS = [
  'file',
  'syntax',
  'startup',
  'state',
  'gameplay',
  'determinism',
  'continuation',
] as const;
export const AI_CHECK_LABELS: Record<(typeof AI_CHECKS)[number], string> = {
  file: 'File and API profile',
  syntax: 'Syntax and profile restrictions',
  startup: 'Startup and callback',
  state: 'Persistent state',
  gameplay: 'Seeded gameplay',
  determinism: 'Repeatable simulation',
  continuation: 'Save and resume',
};
export const AI_VALIDATION_SUITE = 1;
const Tags = Type.Array(
  Type.Union([
    Type.Literal('Balanced'),
    Type.Literal('Rush'),
    Type.Literal('Defensive'),
    Type.Literal('Economy'),
    Type.Literal('Expansion'),
    Type.Literal('Experimental'),
    Type.Literal('Beginner-friendly'),
  ]),
  {
    maxItems: 5,
    uniqueItems: true,
  },
);
const Name = Type.String({ minLength: 1, maxLength: 128, pattern: '\\S' });
const Visibility = Type.Union([
  Type.Literal('public'),
  Type.Literal('unlisted'),
  Type.Literal('private'),
]);
export const AiCheck = Open({
  id: Type.Union([
    Type.Literal('file'),
    Type.Literal('syntax'),
    Type.Literal('startup'),
    Type.Literal('state'),
    Type.Literal('gameplay'),
    Type.Literal('determinism'),
    Type.Literal('continuation'),
  ]),
  status: Type.Union([
    Type.Literal('pending'),
    Type.Literal('running'),
    Type.Literal('passed'),
    Type.Literal('failed'),
    Type.Literal('skipped'),
  ]),
  message: Type.Optional(Type.String({ maxLength: 2000 })),
});
export const AiMetadata = Open({
  apiVersion: Type.Integer({ minimum: 1, maximum: 2 }),
  name: Type.String(),
  description: Type.String(),
  version: Type.String(),
  author: Type.String(),
});
export const AiValidationReport = Open({
  sourceHash: Sha256Hex,
  simVersion: Type.String(),
  suite: Type.Integer({ minimum: 1 }),
  valid: Type.Boolean(),
  checks: Type.Array(AiCheck, { minItems: 7, maxItems: 7 }),
  metadata: Type.Optional(AiMetadata),
});
export const ValidateAiPayload = Strict({
  blobHash: Sha256Hex,
  suite: Type.Literal(AI_VALIDATION_SUITE),
});
export const AiUpload = Open({
  id: Uuid,
  sourceHash: Sha256Hex,
  status: Type.Union([
    Type.Literal('pending'),
    Type.Literal('valid'),
    Type.Literal('invalid'),
    Type.Literal('error'),
  ]),
  report: AiValidationReport,
  error: Type.Optional(Type.String()),
  expiresAt: Timestamp,
});
export const PublishAiRequest = Strict({
  uploadId: Uuid,
  name: Name,
  description: Type.String({ maxLength: 4000 }),
  tags: Tags,
  visibility: Visibility,
  version: Type.String({ minLength: 1, maxLength: 64, pattern: '\\S' }),
  notes: Type.String({ maxLength: 2000 }),
});
export const UpdateAiRequest = Type.Partial(
  Strict({
    name: Name,
    description: Type.String({ maxLength: 4000 }),
    tags: Tags,
    visibility: Visibility,
  }),
);
export const AiVersion = Open({
  id: Uuid,
  hash: Sha256Hex,
  label: Type.String(),
  notes: Type.String(),
  profile: Type.Integer({ minimum: 1, maximum: 2 }),
  createdAt: Timestamp,
  downloads: Type.Integer({ minimum: 0 }),
  downloadUrl: Type.String(),
  validations: Type.Array(AiValidationReport),
});
export const AiInfo = Open({
  id: Uuid,
  name: Type.String(),
  description: Type.String(),
  tags: Tags,
  visibility: Visibility,
  hidden: Type.Boolean(),
  hiddenReason: Type.Optional(Type.String()),
  owner: Open({ id: Uuid, displayName: Type.String() }),
  createdAt: Timestamp,
  updatedAt: Timestamp,
  likes: Type.Integer({ minimum: 0 }),
  downloads: Type.Integer({ minimum: 0 }),
  latestVersion: AiVersion,
  liked: Type.Boolean(),
  favourited: Type.Boolean(),
});
export const AiList = Open({ items: Type.Array(AiInfo), nextCursor: Type.Optional(Type.String()) });
export const AiDetail = Open({
  ai: AiInfo,
  versions: Type.Array(AiVersion),
  viewer: Open({ owner: Type.Boolean(), moderator: Type.Boolean() }),
});
export const AiSocialResult = Open({ active: Type.Boolean(), likes: Type.Integer({ minimum: 0 }) });
export type AiValidationReport = Static<typeof AiValidationReport>;
export type AiUpload = Static<typeof AiUpload>;
export type PublishAiRequest = Static<typeof PublishAiRequest>;
export type UpdateAiRequest = Static<typeof UpdateAiRequest>;
export type AiVersion = Static<typeof AiVersion>;
export type AiInfo = Static<typeof AiInfo>;
export type AiList = Static<typeof AiList>;
export type AiDetail = Static<typeof AiDetail>;
export type AiSocialResult = Static<typeof AiSocialResult>;
export const aiSchemas = {
  AiCheck,
  AiMetadata,
  AiValidationReport,
  ValidateAiPayload,
  AiUpload,
  PublishAiRequest,
  UpdateAiRequest,
  AiVersion,
  AiInfo,
  AiList,
  AiDetail,
  AiSocialResult,
};
export function pendingAiReport(sourceHash: string, simVersion: string): AiValidationReport {
  return {
    sourceHash,
    simVersion,
    suite: AI_VALIDATION_SUITE,
    valid: false,
    checks: AI_CHECKS.map((id) => ({ id, status: 'pending' })),
  };
}
export function passedAiReport(report: AiValidationReport): boolean {
  return (
    report.valid &&
    report.suite === AI_VALIDATION_SUITE &&
    AI_CHECKS.every(
      (id) => report.checks.filter((c) => c.id === id && c.status === 'passed').length === 1,
    )
  );
}
