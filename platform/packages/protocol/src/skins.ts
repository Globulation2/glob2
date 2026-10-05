import { Type, type Static } from 'typebox';
import { Open, Strict, Uuid, Sha256Hex, TeamIndex, Timestamp } from './common.ts';

// Swarm meshes a design can be painted for, mirroring src/online/SwarmMeshCatalog.h
// (the game ships each as data/skins/colony-v1/swarm[-<id>].gsk). 'classic' is the
// original swarm and the default; the swarm is always painted from the atlas's swarm quadrant.
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

/** Colony skin layout colony-v2: 512x512 images of four 256x256 quadrants,
 * worker (0,0), warrior (256,0), explorer (0,256) and swarm (256,256). */
export const COLONY_SKIN_LAYOUT = 'colony-v2';
/** Material ids of a material map pixel (grey level R=G=B). */
export const COLONY_SKIN_MATERIALS = ['glossy', 'matte', 'metallic', 'hairy'] as const;
/** Colour atlas: PNG or WebP, at most 1 MiB decoded. */
const ColonyAtlasBase64 = Type.String({
  minLength: 4,
  maxLength: 1398104,
  pattern: '^[A-Za-z0-9+/]+={0,2}$',
});
/** Material map: PNG or WebP of material ids, at most 256 KiB decoded. */
const MaterialMapBase64 = Type.String({
  minLength: 4,
  maxLength: 349528,
  pattern: '^[A-Za-z0-9+/]+={0,2}$',
});

const SwarmViewAngle = Type.Integer({ minimum: 0, maximum: 359 });

export const PublishSkinRequest = Strict({
  name: Type.String({ minLength: 1, maxLength: 64 }),
  skinId: Type.Optional(Uuid),
  imageBase64: ColonyAtlasBase64,
  materialBase64: MaterialMapBase64,
  buildingColor: Type.Integer({ minimum: 0, maximum: 16777215 }),
  swarmMesh: Type.Optional(SwarmMesh),
  swarmViewAngle: Type.Optional(SwarmViewAngle),
});
export const EquipSkinRequest = Strict({
  versionId: Type.Union([Uuid, Type.Null()]),
  buildingColor: Type.Optional(Type.Integer({ minimum: 0, maximum: 16777215 })),
});
export const SoftwareSprites = Open({
  format: Type.Literal('colony-sprites-v1'),
  manifestSha256: Sha256Hex,
  renderRevision: Sha256Hex,
});
export type SoftwareSprites = Static<typeof SoftwareSprites>;
export const ColonySkinVersion = Open({
  softwareStatus: Type.Optional(
    Type.Union([Type.Literal('pending'), Type.Literal('ready'), Type.Literal('failed')]),
  ),
  id: Uuid,
  skinId: Uuid,
  /** Colour atlas. */
  textureSha256: Sha256Hex,
  /** Material map. */
  materialSha256: Sha256Hex,
  /** sha256 of JSON {skinId, textureSha256, materialSha256, layout, buildingColor}, in that order,
   * followed by swarmMesh only when it is not 'classic'. */
  manifestSha256: Sha256Hex,
  layout: Type.Literal(COLONY_SKIN_LAYOUT),
  buildingColor: Type.Integer({ minimum: 0, maximum: 16777215 }),
  swarmMesh: SwarmMesh,
  swarmViewAngle: Type.Optional(SwarmViewAngle),
});
export type ColonySkinVersion = Static<typeof ColonySkinVersion>;

export const COLONY_SKIN_TYPE = 'glob2-colony-skin+jwt';
export const COLONY_SKIN_AUDIENCE = 'glob2-colony-renderer';
export const MatchColonySkin = Open({
  softwareSprites: Type.Optional(SoftwareSprites),
  team: TeamIndex,
  accountId: Uuid,
  version: ColonySkinVersion,
  buildingColor: Type.Integer({ minimum: 0, maximum: 16777215 }),
  assertion: Type.String({ minLength: 1, maxLength: 8192 }),
});
export type MatchColonySkin = Static<typeof MatchColonySkin>;
export const ColonySkinClaims = Open({
  softwareSprites: Type.Optional(SoftwareSprites),
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
  imageBase64: ColonyAtlasBase64,
  materialBase64: MaterialMapBase64,
  buildingColor: Type.Integer({ minimum: 0, maximum: 16777215 }),
  swarmMesh: Type.Optional(SwarmMesh),
  swarmViewAngle: Type.Optional(SwarmViewAngle),
});
export const SkinDraft = Open({
  skinId: Type.Optional(Uuid),
  revision: Uuid,
  name: Type.String({ minLength: 1, maxLength: 64 }),
  imageBase64: Type.String({ minLength: 4, maxLength: 1398104 }),
  materialBase64: Type.String({ minLength: 4, maxLength: 349528 }),
  buildingColor: Type.Integer({ minimum: 0, maximum: 16777215 }),
  swarmMesh: SwarmMesh,
  swarmViewAngle: Type.Optional(SwarmViewAngle),
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
