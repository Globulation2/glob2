import { Type, type Static } from 'typebox';
import { Strict, Open, Uuid, Sha256Hex } from './common.ts';

export const SET_PACKAGE_MAX_BYTES = 16 * 1024 * 1024;
export const SET_VALIDATION_SUITE = 1;
const Text = Type.String({ maxLength: 2000 });
const Name = Type.String({ minLength: 1, maxLength: 128, pattern: '\\S' });
const JsonObject = Type.Record(Type.String(), Type.Unknown());
export const SetLicense = Type.Union([Type.Literal('CC0-1.0'), Type.Literal('CC-BY-4.0')]);
export const SetVisibility = Type.Union([
  Type.Literal('public'),
  Type.Literal('unlisted'),
  Type.Literal('private'),
]);
export const SetCredit = Strict({ author: Name, source: Type.Optional(Text), license: SetLicense });
export type SetCredit = Static<typeof SetCredit>;
export const MapSetCredit = Strict({
  setId: Uuid,
  versionId: Uuid,
  title: Name,
  license: SetLicense,
  authors: Type.Array(SetCredit, { minItems: 1, maxItems: 64 }),
  sourceHash: Sha256Hex,
  entries: Type.Array(Type.String({ maxLength: 128 }), { maxItems: 32768 }),
});
export const MapSetCredits = Type.Array(MapSetCredit, { maxItems: 256 });
export type MapSetCredits = Static<typeof MapSetCredits>;
export const SetSheet = Strict({
  hash: Sha256Hex,
  png: Type.String({ maxLength: SET_PACKAGE_MAX_BYTES, pattern: '^[A-Za-z0-9_-]+$' }),
  frameWidth: Type.Integer({ minimum: 1, maximum: 64 }),
  frameHeight: Type.Integer({ minimum: 1, maximum: 64 }),
});
export type SetSheet = Static<typeof SetSheet>;
export const SetPackage = Strict({
  schemaVersion: Type.Literal(1),
  setId: Uuid,
  versionId: Uuid,
  title: Name,
  description: Text,
  tags: Type.Array(Type.String({ minLength: 1, maxLength: 32 }), {
    maxItems: 8,
    uniqueItems: true,
  }),
  license: SetLicense,
  credits: Type.Array(SetCredit, { minItems: 1, maxItems: 64 }),
  terrains: Type.Array(JsonObject, { maxItems: 16384 }),
  resources: Type.Array(JsonObject, { maxItems: 16384 }),
  experiments: Type.Optional(Type.Array(JsonObject, { maxItems: 128 })),
  assets: Strict({
    schemaVersion: Type.Literal(1),
    sheets: Type.Array(SetSheet, { maxItems: 256 }),
    terrains: Type.Record(Type.String(), JsonObject),
    credits: Type.Array(JsonObject, { maxItems: 64 }),
  }),
});
export type SetPackage = Static<typeof SetPackage>;
export const ValidateSetPayload = Strict({ blobHash: Sha256Hex, suite: Type.Literal(1) });
export const ValidateSetResult = Open({
  hash: Sha256Hex,
  suite: Type.Literal(1),
  valid: Type.Boolean(),
  reason: Type.Optional(Text),
  previewHash: Type.Optional(Sha256Hex),
  minVersionMinor: Type.Integer({ minimum: 144 }),
  terrainCount: Type.Integer({ minimum: 0 }),
  resourceCount: Type.Integer({ minimum: 0 }),
});
export type ValidateSetResult = Static<typeof ValidateSetResult>;
export const SaveSetDraftRequest = Strict({
  revision: Type.Integer({ minimum: 0 }),
  package: SetPackage,
});
export const PublishSetRequest = Strict({
  revision: Type.Integer({ minimum: 1 }),
  visibility: SetVisibility,
  label: Type.String({ minLength: 1, maxLength: 64 }),
  notes: Text,
});
export const UpdateSetRequest = Strict({
  title: Name,
  description: Text,
  visibility: SetVisibility,
  tags: Type.Array(Type.String({ minLength: 1, maxLength: 32 }), {
    maxItems: 8,
    uniqueItems: true,
  }),
});
export interface SetValidation {
  status: 'pending' | 'valid' | 'invalid' | 'error';
  hash: string;
  simVersion: string;
  suite: number;
  report: ValidateSetResult | null;
  error: string | null;
}
export interface SetDraftSummary {
  id: string;
  title: string;
  updatedAt: string;
  publishedVersionId: string | null;
}
export interface SetDraft {
  id: string;
  revision: number;
  package: SetPackage;
  validation: SetValidation | null;
  publishedVersionId: string | null;
}
export interface SetVersion {
  id: string;
  hash: string;
  label: string;
  notes: string;
  license: Static<typeof SetLicense>;
  credits: Static<typeof SetCredit>[];
  simVersion: string;
  minVersionMinor: number;
  terrainCount: number;
  resourceCount: number;
  createdAt: string;
}
export interface SetInfo {
  id: string;
  title: string;
  description: string;
  tags: string[];
  owner: { id: string; displayName: string };
  visibility: Static<typeof SetVisibility>;
  hidden: boolean;
  hiddenReason?: string;
  likes: number;
  liked: boolean;
  downloads: number;
  createdAt: string;
  updatedAt: string;
  versions: SetVersion[];
}
export interface SetList {
  items: SetInfo[];
  nextCursor?: string;
}
export const setSchemas = {
  SetPackage: { schema: SetPackage },
  SetSheet: { schema: SetSheet },
  SaveSetDraftRequest: { schema: SaveSetDraftRequest },
  PublishSetRequest: { schema: PublishSetRequest },
  UpdateSetRequest: { schema: UpdateSetRequest },
  ValidateSetPayload: { schema: ValidateSetPayload },
  ValidateSetResult: { schema: ValidateSetResult },
};
