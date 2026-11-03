// Portable authored building families. Gameplay semantics are validated by the engine.
import { Type, type Static } from 'typebox';
import { Strict, Sha256Hex, Uuid } from './common.ts';
import { parse } from './validate.ts';

export const BUILDING_PACKAGE_LIMITS = {
  uploadBytes: 32 * 1024 * 1024,
  manifestBytes: 8 * 1024 * 1024,
  // Rendered frame/layer references, including repeated uses of one image hash.
  // This is a pixel budget; renderer surfaces/atlases have additional overhead.
  decodedBytes: 64 * 1024 * 1024,
  frameSide: 512,
  images: 256,
  variants: 4096,
} as const;
export const BUILDING_VALIDATION_SUITE = 1;
const LocalKey = Type.String({ pattern: '^[a-z0-9][a-z0-9._-]*$', maxLength: 64 });
const StableKey = Type.String({ pattern: '^[a-z0-9][a-z0-9._-]*$', maxLength: 128 });
// The engine rejects unknown fields inside these objects and validates their values.
const Fields = Type.Record(Type.String(), Type.Unknown());
export const AuthoredBuildingVariant = Strict({
  key: StableKey,
  previous: Type.Optional(Type.Union([StableKey, Type.Literal('')])),
  next: Type.Optional(Type.Union([StableKey, Type.Literal('')])),
  requiredExperiment: Type.Optional(Type.Union([StableKey, Type.Literal('')])),
  properties: Fields,
  semantics: Fields,
  presentation: Type.Optional(Fields),
});
export const BuildingFrame = Strict({
  imageHash: Sha256Hex,
  width: Type.Integer({ minimum: 1, maximum: BUILDING_PACKAGE_LIMITS.frameSide }),
  height: Type.Integer({ minimum: 1, maximum: BUILDING_PACKAGE_LIMITS.frameSide }),
  teamColorHash: Type.Optional(Sha256Hex),
});
export const BuildingSprite = Strict({
  key: LocalKey,
  frames: Type.Array(BuildingFrame, { minItems: 1, maxItems: BUILDING_PACKAGE_LIMITS.images }),
});
export const BuildingPackage = Strict({
  schemaVersion: Type.Literal(1),
  namespace: Uuid,
  variants: Type.Array(AuthoredBuildingVariant, {
    minItems: 1,
    maxItems: BUILDING_PACKAGE_LIMITS.variants,
  }),
  experiments: Type.Array(
    Strict({
      key: StableKey,
      label: Type.String({ minLength: 1, maxLength: 1024 }),
      help: Type.String({ minLength: 1, maxLength: 1024 }),
    }),
    { maxItems: 64 },
  ),
  sprites: Type.Array(BuildingSprite, { maxItems: BUILDING_PACKAGE_LIMITS.images }),
});
export type BuildingPackage = Static<typeof BuildingPackage>;
export type AuthoredBuildingVariant = Static<typeof AuthoredBuildingVariant>;
export type BuildingSprite = Static<typeof BuildingSprite>;

export function buildingNamespacePrefix(namespace: string): string {
  parse(Uuid, namespace, 'building namespace');
  return `b-${namespace}-`;
}

/** Envelope/reference checks only. Never substitute this for engine validation. */
export function checkBuildingPackage(value: unknown): BuildingPackage {
  const pkg = parse(BuildingPackage, value, 'building package');
  const parents = new Set<object>();
  const visit = (node: unknown, depth: number) => {
    if (depth > 64) throw new Error('Building package nesting exceeds 64 levels');
    if (node === null || typeof node === 'string' || typeof node === 'boolean') return;
    if (typeof node === 'number' && Number.isFinite(node)) return;
    if (typeof node !== 'object' || node === null)
      throw new Error('Building package contains non-JSON values');
    const prototype = Object.getPrototypeOf(node);
    if (!Array.isArray(node) && prototype !== Object.prototype && prototype !== null)
      throw new Error('Building package contains non-JSON objects');
    if (parents.has(node)) throw new Error('Building package contains a cycle');
    parents.add(node);
    for (const child of Array.isArray(node) ? node : Object.values(node)) visit(child, depth + 1);
    parents.delete(node);
  };
  visit(pkg, 0);

  if (new TextEncoder().encode(JSON.stringify(pkg)).length > BUILDING_PACKAGE_LIMITS.manifestBytes)
    throw new Error('Building package manifest exceeds 8 MiB');
  const prefix = buildingNamespacePrefix(pkg.namespace);
  const unique = (keys: string[], what: string) => {
    if (new Set(keys).size !== keys.length) throw new Error(`Duplicate ${what}`);
  };
  unique(
    pkg.variants.map((v) => v.key),
    'building key',
  );
  unique(
    pkg.experiments.map((e) => e.key),
    'experiment key',
  );
  unique(
    pkg.sprites.map((s) => s.key),
    'sprite key',
  );
  const variants = new Set(pkg.variants.map((v) => v.key));
  const experiments = new Set(pkg.experiments.map((e) => e.key));
  const sprites = new Set(pkg.sprites.map((s) => s.key));
  let images = 0,
    decodedBytes = 0;
  for (const sprite of pkg.sprites)
    for (const frame of sprite.frames) {
      images += frame.teamColorHash ? 2 : 1;
      decodedBytes += frame.width * frame.height * 4 * (frame.teamColorHash ? 2 : 1);
    }
  if (images > BUILDING_PACKAGE_LIMITS.images) throw new Error('Too many building images');
  if (decodedBytes > BUILDING_PACKAGE_LIMITS.decodedBytes)
    throw new Error('Building artwork exceeds 64 MiB decoded');
  for (const e of pkg.experiments) {
    if (!e.key.startsWith(prefix) || !/^[a-z0-9]+(-[a-z0-9]+)*$/.test(e.key))
      throw new Error(`Experiment must use the package namespace: ${e.key}`);
  }
  for (const v of pkg.variants) {
    if (!v.key.startsWith(prefix))
      throw new Error(`Building must use the package namespace: ${v.key}`);
    for (const key of [v.next, v.previous])
      if (key && !variants.has(key))
        throw new Error(`Unresolved package building reference: ${key}`);
    if (v.requiredExperiment && !experiments.has(v.requiredExperiment))
      throw new Error(`Unresolved package experiment: ${v.requiredExperiment}`);
    const market = v.semantics['market'];
    if (market && typeof market === 'object' && !Array.isArray(market)) {
      for (const field of ['suppliesStockExperiment', 'fetchesStockExperiment']) {
        const key = (market as Record<string, unknown>)[field];
        if (typeof key === 'string' && key.startsWith('b-') && !experiments.has(key))
          throw new Error(`Unresolved package experiment: ${key}`);
      }
    }
    const group = v.presentation?.['connectionGroup'];
    if (
      group !== undefined &&
      group !== '' &&
      (typeof group !== 'string' || !group.startsWith(prefix))
    )
      throw new Error('Connection group must use the package namespace');
    for (const field of ['gameSprite', 'miniSprite']) {
      const ref = v.properties[field];
      if (ref === undefined) continue;
      if (typeof ref !== 'string') throw new Error(`Invalid ${field}`);
      if (ref.startsWith('package:')) {
        if (!sprites.has(ref.slice(8))) throw new Error(`Unresolved package sprite: ${ref}`);
      } else if (!/^data\/gfx\/[a-zA-Z0-9_-]+$/.test(ref)) {
        throw new Error(`Use installed artwork or a package sprite: ${ref}`);
      }
    }
  }
  return pkg;
}

/** A fork gets new stable keys, including presentation connection groups. */
export function forkBuildingPackage(value: unknown, namespace: string): BuildingPackage {
  const pkg = structuredClone(checkBuildingPackage(value));
  const from = buildingNamespacePrefix(pkg.namespace),
    to = buildingNamespacePrefix(namespace);
  const rewrite = (key: string) => (key.startsWith(from) ? to + key.slice(from.length) : key);
  pkg.namespace = namespace;
  for (const e of pkg.experiments) e.key = rewrite(e.key);
  for (const v of pkg.variants) {
    v.key = rewrite(v.key);
    if (v.next) v.next = rewrite(v.next);
    if (v.previous) v.previous = rewrite(v.previous);
    if (v.requiredExperiment) v.requiredExperiment = rewrite(v.requiredExperiment);
    const market = v.semantics['market'];
    if (market && typeof market === 'object' && !Array.isArray(market)) {
      for (const field of ['suppliesStockExperiment', 'fetchesStockExperiment']) {
        const key = (market as Record<string, unknown>)[field];
        if (typeof key === 'string') (market as Record<string, unknown>)[field] = rewrite(key);
      }
    }
    if (typeof v.presentation?.['connectionGroup'] === 'string')
      v.presentation['connectionGroup'] = rewrite(v.presentation['connectionGroup']);
  }
  return checkBuildingPackage(pkg);
}

export const SaveBuildingDraftRequest = Strict({
  revision: Uuid,
  name: Type.String({ minLength: 1, maxLength: 128 }),
  package: BuildingPackage,
});
export type SaveBuildingDraftRequest = Static<typeof SaveBuildingDraftRequest>;
export interface BuildingDraft {
  id: string;
  revision: string;
  name: string;
  updatedAt: string;
  package: BuildingPackage;
}
export interface BuildingDraftList {
  items: Pick<BuildingDraft, 'id' | 'revision' | 'name' | 'updatedAt'>[];
}

export const PublishBuildingRequest = Strict({
  revision: Uuid,
  description: Type.String({ maxLength: 4000 }),
  visibility: Type.Union([
    Type.Literal('public'),
    Type.Literal('unlisted'),
    Type.Literal('private'),
  ]),
});
/** Mutable listing metadata; immutable release archives do not change. */
export const UpdateBuildingFamilyRequest = Strict(
  {
    name: Type.Optional(Type.String({ minLength: 1, maxLength: 128 })),
    description: Type.Optional(Type.String({ maxLength: 4000 })),
    visibility: Type.Optional(PublishBuildingRequest.properties.visibility),
  },
  { minProperties: 1 },
);
export interface BuildingRelease {
  id: string;
  archiveHash: string;
  simVersion: string;
  baseHash: string;
  createdAt: string;
  status: 'pending' | 'valid' | 'invalid' | 'error';
  error?: string;
  catalogHash?: string;
  artworkHash?: string;
}
export interface BuildingFamily {
  id: string;
  namespace: string;
  name: string;
  description: string;
  visibility: 'public' | 'unlisted' | 'private';
  owner: { id: string; displayName: string };
  hidden: boolean;
  downloads: number;
  likes: number;
  liked: boolean;
  favourite: boolean;
  releases: BuildingRelease[];
  updatedAt: string;
}
export interface BuildingLibrary {
  items: BuildingFamily[];
  nextCursor?: string;
}
