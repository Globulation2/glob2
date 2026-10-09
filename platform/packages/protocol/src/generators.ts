// Immutable portable JavaScript generator releases. The engine owns package parsing.
import { Type, type Static } from 'typebox';
import { Open, Strict, Uuid, Sha256Hex, Timestamp, Uint32 } from './common.ts';
export const GENERATOR_VALIDATION_SUITE = 1;
export const GENERATOR_MAX_BYTES = 4 * 1024 * 1024;
export const GeneratorSettings = Strict({
  seed: Uint32,
  params: Type.Record(Type.String({ pattern: '^[A-Za-z0-9_-]{1,64}$' }), Type.Integer(), {
    maxProperties: 72,
  }),
  candidates: Type.Integer({ minimum: 1, maximum: 5, default: 1 }),
  startingUnitLevel: Type.Literal(0),
});
export const GeneratorControlInfo = Open({
  id: Type.String(),
  label: Type.String(),
  group: Type.String(),
  kind: Type.Union([Type.Literal('range'), Type.Literal('toggle'), Type.Literal('choice')]),
  minimum: Type.Optional(Type.Integer()),
  maximum: Type.Optional(Type.Integer()),
  step: Type.Optional(Type.Integer()),
  default: Type.Integer(),
  values: Type.Optional(Type.Array(Type.Integer(), { maxItems: 4096 })),
  powerOfTwo: Type.Optional(Type.Boolean()),
  choices: Type.Optional(Type.Array(Type.String())),
});
export const GeneratorMetadata = Open({
  id: Type.String({ pattern: '^[a-z0-9_.:-]{1,128}$' }),
  name: Type.String(),
  description: Type.String(),
  revision: Uint32,
  apiVersion: Type.Integer({ minimum: 1 }),
  toolkitVersion: Type.Integer({ minimum: 1 }),
  editorOnly: Type.Boolean(),
  tags: Type.Array(Type.String()),
  controls: Type.Array(GeneratorControlInfo),
});
export const GeneratorSample = Open({
  settings: GeneratorSettings,
  status: Type.Union([Type.Literal('passed'), Type.Literal('refused'), Type.Literal('failed')]),
  message: Type.Optional(Type.String({ maxLength: 2000 })),
  fingerprint: Type.Optional(Type.String()),
});
export const GeneratorValidationReport = Open({
  sourceHash: Sha256Hex,
  simVersion: Type.String(),
  suite: Type.Integer({ minimum: 1 }),
  valid: Type.Boolean(),
  packageHash: Type.Optional(Sha256Hex),
  fileHash: Type.Optional(Sha256Hex),
  metadata: Type.Optional(GeneratorMetadata),
  samples: Type.Array(GeneratorSample, { maxItems: 160 }),
  error: Type.Optional(Type.String({ maxLength: 2000 })),
  previewHash: Type.Optional(Sha256Hex),
});
export const ValidateGeneratorPayload = Strict({
  blobHash: Sha256Hex,
  suite: Type.Literal(GENERATOR_VALIDATION_SUITE),
  example: GeneratorSettings,
});
export const ScriptGeneratorDescriptor = Strict({
  libraryId: Uuid,
  versionId: Uuid,
  packageHash: Sha256Hex,
  fileHash: Sha256Hex,
  generatorId: Type.String({ pattern: '^[a-z0-9_.-]+:[a-z0-9_.:-]+$', maxLength: 128 }),
  revision: Uint32,
  ...GeneratorSettings.properties,
});
export const GeneratorUpload = Open({
  id: Uuid,
  sourceHash: Sha256Hex,
  status: Type.Union([
    Type.Literal('pending'),
    Type.Literal('valid'),
    Type.Literal('invalid'),
    Type.Literal('error'),
  ]),
  report: GeneratorValidationReport,
  expiresAt: Timestamp,
  error: Type.Optional(Type.String()),
});
const Visibility = Type.Union([
  Type.Literal('public'),
  Type.Literal('unlisted'),
  Type.Literal('private'),
]);
export const PublishGeneratorRequest = Strict({
  uploadId: Uuid,
  name: Type.String({ minLength: 1, maxLength: 128, pattern: '\\S' }),
  description: Type.String({ maxLength: 4000 }),
  visibility: Visibility,
  version: Type.String({ minLength: 1, maxLength: 64, pattern: '\\S' }),
  notes: Type.String({ maxLength: 2000 }),
});
export const UpdateGeneratorRequest = Type.Partial(
  Strict({
    name: PublishGeneratorRequest.properties.name,
    description: PublishGeneratorRequest.properties.description,
    visibility: Visibility,
  }),
);
export const GeneratorVersion = Open({
  id: Uuid,
  hash: Sha256Hex,
  packageHash: Sha256Hex,
  label: Type.String(),
  notes: Type.String(),
  profile: Type.Integer(),
  metadata: GeneratorMetadata,
  example: GeneratorSettings,
  createdAt: Timestamp,
  downloads: Type.Integer(),
  downloadUrl: Type.String(),
  validations: Type.Array(GeneratorValidationReport),
});
export const GeneratorInfo = Open({
  id: Uuid,
  name: Type.String(),
  description: Type.String(),
  tags: Type.Array(Type.String()),
  visibility: Visibility,
  hidden: Type.Boolean(),
  hiddenReason: Type.Optional(Type.String()),
  owner: Open({ id: Uuid, displayName: Type.String() }),
  createdAt: Timestamp,
  updatedAt: Timestamp,
  likes: Type.Integer(),
  downloads: Type.Integer(),
  latestVersion: GeneratorVersion,
  liked: Type.Boolean(),
  favourited: Type.Boolean(),
});
export const GeneratorList = Open({
  items: Type.Array(GeneratorInfo),
  nextCursor: Type.Optional(Type.String()),
});
export const GeneratorDetail = Open({
  generator: GeneratorInfo,
  versions: Type.Array(GeneratorVersion),
  viewer: Open({ owner: Type.Boolean(), moderator: Type.Boolean() }),
});
export const GeneratorSocialResult = Open({ active: Type.Boolean(), likes: Type.Integer() });
export type GeneratorSettings = Static<typeof GeneratorSettings>;
export type GeneratorMetadata = Static<typeof GeneratorMetadata>;
export type GeneratorValidationReport = Static<typeof GeneratorValidationReport>;
export type ScriptGeneratorDescriptor = Static<typeof ScriptGeneratorDescriptor>;
export type GeneratorUpload = Static<typeof GeneratorUpload>;
export type GeneratorVersion = Static<typeof GeneratorVersion>;
export type GeneratorInfo = Static<typeof GeneratorInfo>;
export type GeneratorList = Static<typeof GeneratorList>;
export type GeneratorDetail = Static<typeof GeneratorDetail>;
export function pendingGeneratorReport(
  sourceHash: string,
  simVersion: string,
): GeneratorValidationReport {
  return { sourceHash, simVersion, suite: GENERATOR_VALIDATION_SUITE, valid: false, samples: [] };
}
export function passedGeneratorReport(r: GeneratorValidationReport): boolean {
  return (
    r.valid &&
    r.suite === GENERATOR_VALIDATION_SUITE &&
    !!r.metadata &&
    !!r.packageHash &&
    !!r.fileHash &&
    r.samples.length > 0 &&
    r.samples[0]?.status === 'passed' &&
    r.samples.every((s) => s.status !== 'failed')
  );
}
export const generatorSchemas = {
  GeneratorSettings,
  GeneratorControlInfo,
  GeneratorMetadata,
  GeneratorSample,
  GeneratorValidationReport,
  ValidateGeneratorPayload,
  ScriptGeneratorDescriptor,
  GeneratorUpload,
  PublishGeneratorRequest,
  UpdateGeneratorRequest,
  GeneratorVersion,
  GeneratorInfo,
  GeneratorList,
  GeneratorDetail,
  GeneratorSocialResult,
};
