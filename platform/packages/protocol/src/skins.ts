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
/** Materials of a material map pixel (grey level R=G=B is the id), mirroring
 * libgag/shaders/skin-materials.json in id order; a test keeps them equal.
 * `shells` materials grow fur passes in the renderers. */
export const COLONY_SKIN_MATERIALS = [
  { id: 0, key: 'glossy', name: 'Classic glossy', group: 'Basics', shells: false },
  { id: 1, key: 'matte', name: 'Matte', group: 'Basics', shells: false },
  { id: 2, key: 'metallic', name: 'Riveted metal', group: 'Metal', shells: false },
  { id: 3, key: 'hairy', name: 'Hairy', group: 'Organic', shells: true },
  { id: 4, key: 'cartoon', name: 'Cartoon', group: 'Basics', shells: false },
  { id: 5, key: 'woven', name: 'Woven fabric', group: 'Craft', shells: false },
  { id: 6, key: 'goo', name: 'Oozy goo', group: 'Wet', shells: false },
  { id: 7, key: 'wood', name: 'Wood', group: 'Craft', shells: false },
  { id: 8, key: 'stone', name: 'Stone', group: 'Mineral', shells: false },
  { id: 9, key: 'scales', name: 'Scales', group: 'Organic', shells: false },
  { id: 10, key: 'leather', name: 'Leather', group: 'Organic', shells: false },
  { id: 11, key: 'crystal', name: 'Crystal', group: 'Mineral', shells: false },
  { id: 12, key: 'lava', name: 'Lava', group: 'Wet', shells: false },
  { id: 13, key: 'chrome', name: 'Chrome', group: 'Metal', shells: false },
  { id: 14, key: 'carbon', name: 'Carbon fibre', group: 'Metal', shells: false },
  { id: 15, key: 'velvet', name: 'Velvet', group: 'Organic', shells: false },
  { id: 16, key: 'honeycomb', name: 'Honeycomb', group: 'Craft', shells: false },
  { id: 17, key: 'ice', name: 'Ice', group: 'Mineral', shells: false },
  { id: 18, key: 'cloud', name: 'Cloud', group: 'Organic', shells: true },
  { id: 19, key: 'hammered', name: 'Hammered metal', group: 'Metal', shells: false },
  { id: 20, key: 'candy', name: 'Candy', group: 'Basics', shells: false },
  { id: 21, key: 'slime', name: 'Slime', group: 'Wet', shells: false },
] as const;
export type ColonySkinMaterial = (typeof COLONY_SKIN_MATERIALS)[number];
/** Fur shell passes drawn beyond the body for `shells` materials, how far the
 * outermost reaches in the game's tile NDC, and the per-shell depth bias. */
export const COLONY_SKIN_SHELLS = 8;
export const COLONY_SKIN_FUR_LENGTH = 0.05;
export const COLONY_SKIN_SHELL_DEPTH = 0.002;
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
  /** Immutable input identity, when live artwork uses separate WebP renditions. */
  source: Type.Optional(
    Open({
      manifestSha256: Sha256Hex,
      textureSha256: Sha256Hex,
      materialSha256: Sha256Hex,
    }),
  ),
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
