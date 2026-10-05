import { Type, type Static } from 'typebox';
import { Open, Strict, Uuid, Sha256Hex, Timestamp } from './common.ts';
export const MusicMood = Type.Union([
  Type.Literal('calm'),
  Type.Literal('building'),
  Type.Literal('combat'),
]);
export const MusicLicense = Type.Union([
  Type.Literal('CC0-1.0'),
  Type.Literal('CC-BY-4.0'),
  Type.Literal('CC-BY-SA-4.0'),
]);
const text = (maxLength: number, minLength = 0) =>
  Type.String({ minLength, maxLength, pattern: '^[^\\u0000]*$' });
export const MusicMetadata = Strict({
  title: text(128, 1),
  artist: text(128, 1),
  description: text(4000),
  license: MusicLicense,
  credits: text(4000),
  sources: Type.Array(Type.String({ pattern: '^https?://[^\\s]+$', maxLength: 2048 }), {
    maxItems: 20,
  }),
  tags: Type.Array(text(32, 1), { maxItems: 12, uniqueItems: true }),
  aiGenerated: Type.Boolean(),
});
export type MusicMetadata = Static<typeof MusicMetadata>;
export const MusicConvert = Strict({
  repair: Type.Union([Type.Literal('none'), Type.Literal('trim'), Type.Literal('pad')]),
  master: Type.Boolean(),
});
export type MusicConvert = Static<typeof MusicConvert>;
export const MusicTrack = Open({
  mood: MusicMood,
  sha256: Sha256Hex,
  bytes: Type.Integer({ minimum: 1 }),
  url: Type.String(),
  waveform: Type.Array(Type.Number({ minimum: 0, maximum: 1 }), { maxItems: 512 }),
});
export type MusicTrack = Static<typeof MusicTrack>;
export const MusicRelease = Open({
  id: Uuid,
  ownerId: Uuid,
  metadata: MusicMetadata,
  status: Type.Union([
    Type.Literal('draft'),
    Type.Literal('inspecting'),
    Type.Literal('inspected'),
    Type.Literal('converting'),
    Type.Literal('ready'),
    Type.Literal('published'),
    Type.Literal('withdrawn'),
    Type.Literal('failed'),
  ]),
  createdAt: Timestamp,
  frames: Type.Integer({ minimum: 0, maximum: 43200000 }),
  tracks: Type.Array(MusicTrack, { maxItems: 3 }),
  warnings: Type.Array(Type.String()),
  inspection: Type.Union([
    Type.Null(),
    Open({
      frames: Type.Array(Type.Integer(), { minItems: 3, maxItems: 3 }),
      seconds: Type.Array(Type.Number(), { minItems: 3, maxItems: 3 }),
      equal: Type.Boolean(),
    }),
  ]),
  uploaded: Type.Array(Type.String()),
  coverUrl: Type.Union([Type.Null(), Type.String()]),
  likes: Type.Integer({ minimum: 0 }),
  downloads: Type.Integer({ minimum: 0 }),
  liked: Type.Boolean(),
  hidden: Type.Boolean(),
  error: Type.Union([Type.Null(), Type.String()]),
});
export type MusicRelease = Static<typeof MusicRelease>;
export const MusicList = Open({
  items: Type.Array(MusicRelease),
  next: Type.Union([Type.Null(), Type.String()]),
});
export type MusicList = Static<typeof MusicList>;
export const MusicBatch = Strict({
  ids: Type.Array(Uuid, { minItems: 1, maxItems: 10, uniqueItems: true }),
});
export const MusicReport = Strict({ reason: text(2000, 1) });
export const MusicModerate = Strict({ hidden: Type.Boolean(), reason: text(2000, 1) });
export const musicSchemas = {
  MusicMetadata,
  MusicConvert,
  MusicRelease,
  MusicList,
  MusicBatch,
  MusicReport,
  MusicModerate,
};
