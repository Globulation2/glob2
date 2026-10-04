import { Type, type Static } from 'typebox';
import { Open, Strict, Uuid, Sha256Hex, TeamIndex, Timestamp } from './common.ts';

// Swarm meshes a design can be painted for, mirroring src/online/SwarmMeshCatalog.h
// (the game ships each as data/skins/colony-v1/swarm[-<id>].gsk). 'classic' is the
// original swarm and the default; versions published before mesh choice use it.
export const SwarmMesh = Type.Union([
  Type.Literal('classic'),
  Type.Literal('crown'),
  Type.Literal('clutch'),
  Type.Literal('toadstool'),
  Type.Literal('coral'),
  Type.Literal('skep'),
  Type.Literal('bloom'),
]);
export type SwarmMeshId = Static<typeof SwarmMesh>;
/** Catalog order: the game's mesh index, and the designer's picker order. */
export const SWARM_MESHES = SwarmMesh.anyOf.map((mesh) => mesh.const) as readonly SwarmMeshId[];

export const PublishSkinRequest = Strict({
  name: Type.String({ minLength: 1, maxLength: 64 }),
  skinId: Type.Optional(Uuid),
  imageBase64: Type.String({ minLength: 4, maxLength: 349528, pattern: '^[A-Za-z0-9+/]+={0,2}$' }),
  buildingColor: Type.Integer({ minimum: 0, maximum: 16777215 }),
  swarmMesh: Type.Optional(SwarmMesh),
});
export const EquipSkinRequest = Strict({
  versionId: Type.Union([Uuid, Type.Null()]),
  buildingColor: Type.Optional(Type.Integer({ minimum: 0, maximum: 16777215 })),
});
export const ColonySkinVersion = Open({
  id: Uuid,
  skinId: Uuid,
  textureSha256: Sha256Hex,
  manifestSha256: Sha256Hex,
  layout: Type.Literal('colony-v1'),
  buildingColor: Type.Integer({ minimum: 0, maximum: 16777215 }),
  swarmMesh: SwarmMesh,
});
export type ColonySkinVersion = Static<typeof ColonySkinVersion>;

export const COLONY_SKIN_TYPE = 'glob2-colony-skin+jwt';
export const COLONY_SKIN_AUDIENCE = 'glob2-colony-renderer';
export const MatchColonySkin = Open({
  team: TeamIndex,
  accountId: Uuid,
  version: ColonySkinVersion,
  buildingColor: Type.Integer({ minimum: 0, maximum: 16777215 }),
  assertion: Type.String({ minLength: 1, maxLength: 8192 }),
});
export type MatchColonySkin = Static<typeof MatchColonySkin>;
export const ColonySkinClaims = Open({
  iss: Type.String(),
  aud: Type.Literal(COLONY_SKIN_AUDIENCE),
  sub: Uuid,
  iat: Type.Integer({ minimum: 0 }),
  exp: Type.Integer({ minimum: 0 }),
  matchId: Uuid,
  team: TeamIndex,
  accountId: Uuid,
  version: ColonySkinVersion,
  buildingColor: Type.Integer({ minimum: 0, maximum: 16777215 }),
});
export type ColonySkinClaims = Static<typeof ColonySkinClaims>;

// null creates the first draft; a revision must match before replacing it.
export const SaveSkinDraftRequest = Strict({
  skinId: Type.Optional(Uuid),
  revision: Type.Union([Uuid, Type.Null()]),
  name: Type.String({ minLength: 1, maxLength: 64 }),
  imageBase64: Type.String({ minLength: 4, maxLength: 349528, pattern: '^[A-Za-z0-9+/]+={0,2}$' }),
  buildingColor: Type.Integer({ minimum: 0, maximum: 16777215 }),
  swarmMesh: Type.Optional(SwarmMesh),
});
export const SkinDraft = Open({
  skinId: Type.Optional(Uuid),
  revision: Uuid,
  name: Type.String({ minLength: 1, maxLength: 64 }),
  imageBase64: Type.String({ minLength: 4, maxLength: 349528 }),
  buildingColor: Type.Integer({ minimum: 0, maximum: 16777215 }),
  swarmMesh: SwarmMesh,
});
export type SkinDraft = Static<typeof SkinDraft>;

export const SkinReportRequest = Strict({ reason: Type.String({ minLength: 1, maxLength: 1000 }) });
export const ResolveSkinReportRequest = Strict({
  resolution: Type.Union([Type.Literal('dismissed'), Type.Literal('disabled')]),
  reason: Type.String({ minLength: 1, maxLength: 1000 }),
});
export const ModerateSkinRequest = Strict({
  disabled: Type.Boolean(),
  reason: Type.String({ minLength: 1, maxLength: 1000 }),
});

export const SkinReportInfo = Open({
  id: Uuid,
  versionId: Uuid,
  skinId: Uuid,
  name: Type.String(),
  reporterName: Type.String(),
  reason: Type.String(),
  createdAt: Timestamp,
  resolution: Type.Union([Type.Literal('dismissed'), Type.Literal('disabled'), Type.Null()]),
  resolutionReason: Type.Union([Type.String(), Type.Null()]),
  disabledAt: Type.Union([Timestamp, Type.Null()]),
});
export type SkinReportInfo = Static<typeof SkinReportInfo>;
export const SkinReportList = Open({
  items: Type.Array(SkinReportInfo),
  nextCursor: Type.Optional(Uuid),
});
export type SkinReportList = Static<typeof SkinReportList>;
