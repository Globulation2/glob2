import { Type, type Static } from 'typebox';
import { Strict, Uuid } from './common.ts';
import { AiStudioConfig, AiStudioCommand } from './aiStudio.ts';
import { GeneratorSettings } from './generators.ts';
import type { StudioDetail } from './codingStudio.ts';

export const GENERATOR_STUDIO_DRAFT_BYTES = 256 * 1024;
export const GeneratorStudioConfig = AiStudioConfig;
/** Both editable files travel and persist as one JSON envelope. Manifest text may
 * be incomplete; only packaging/checking parses it as an engine manifest. */
export interface GeneratorStudioDraft {
  manifest: string;
  script: string;
}
export function decodeGeneratorDraft(source: string): GeneratorStudioDraft {
  const d: unknown = JSON.parse(source);
  if (!d || typeof d !== 'object' || Array.isArray(d)) throw Error('Invalid generator draft.');
  const value = d as Record<string, unknown>;
  if (
    Object.keys(value).length !== 2 ||
    typeof value.manifest !== 'string' ||
    typeof value.script !== 'string'
  )
    throw Error('A generator draft contains manifest and script text.');
  return { manifest: value.manifest, script: value.script };
}
export function encodeGeneratorDraft(draft: GeneratorStudioDraft): string {
  return JSON.stringify(draft);
}
export function generatorPackage(source: string): string {
  const { manifest: text, script } = decodeGeneratorDraft(source);
  const manifest: unknown = JSON.parse(text);
  if (!manifest || typeof manifest !== 'object' || Array.isArray(manifest))
    throw Error('The manifest must be a JSON object.');
  const entry = (manifest as Record<string, unknown>).entry ?? 'generator.js';
  if (
    typeof entry !== 'string' ||
    !entry.endsWith('.js') ||
    entry.startsWith('/') ||
    entry.includes('\\') ||
    entry.split('/').some((p) => !p || p === '.' || p === '..')
  )
    throw Error('Choose a relative .js entry filename inside the package.');
  return JSON.stringify({ formatVersion: 1, manifest, modules: { [entry]: script } });
}
export function importGeneratorPackage(text: string): string {
  const p = JSON.parse(text) as { formatVersion?: unknown; manifest?: unknown; modules?: unknown };
  if (
    !p ||
    p.formatVersion !== 1 ||
    !p.manifest ||
    typeof p.manifest !== 'object' ||
    !p.modules ||
    typeof p.modules !== 'object' ||
    Array.isArray(p.modules)
  )
    throw Error('Import a format-version-1 portable generator package.');
  const modules = p.modules as Record<string, unknown>;
  const entry = (p.manifest as Record<string, unknown>).entry ?? 'generator.js';
  if (Object.keys(modules).length !== 1)
    throw Error(
      'Generator Studio v1 supports one JavaScript module. Multi-module packages cannot be imported; no files were discarded.',
    );
  if (typeof entry !== 'string' || typeof modules[entry] !== 'string')
    throw Error('The manifest entry must identify the single JavaScript module.');
  const source = encodeGeneratorDraft({
    manifest: JSON.stringify(p.manifest, null, 2),
    script: modules[entry] as string,
  });
  generatorPackage(source);
  return source;
}
/** Recovery downloads preserve invalid manifest text and are distinct from engine packages. */
export function importGeneratorStudioFile(text: string): string {
  const value: unknown = JSON.parse(text);
  if (
    value &&
    typeof value === 'object' &&
    !Array.isArray(value) &&
    Object.keys(value).length === 2 &&
    typeof (value as GeneratorStudioDraft).manifest === 'string' &&
    typeof (value as GeneratorStudioDraft).script === 'string'
  )
    return encodeGeneratorDraft(decodeGeneratorDraft(text));
  return importGeneratorPackage(text);
}
export type GeneratorStudioDetail = StudioDetail<{
  id: string;
  revision: number;
  settings: Static<typeof GeneratorSettings>;
  source_hash: string;
  summary: string;
  created_at: string;
}>;
const Revision = Type.Integer({ minimum: 1, maximum: 2147483647 });
const Source = Type.String({ minLength: 1, maxLength: GENERATOR_STUDIO_DRAFT_BYTES });
export const GeneratorStudioCreate = Strict({
  title: Type.String({ minLength: 1, maxLength: 128 }),
  source: Type.Optional(Source),
  versionId: Type.Optional(Uuid),
});
export const GeneratorStudioSave = Strict({
  expectedRevision: Revision,
  source: Type.Optional(Source),
  restoreRevision: Type.Optional(Revision),
  title: Type.Optional(Type.String({ minLength: 1, maxLength: 128 })),
  reason: Type.Optional(Type.Union([Type.Literal('manual'), Type.Literal('import')])),
});
export const GeneratorStudioCommand = AiStudioCommand;
export const GeneratorStudioSettings = Strict({
  ...GeneratorSettings.properties,
  candidates: Type.Literal(1),
  params: Type.Record(
    Type.String({ pattern: '^[A-Za-z0-9_-]{1,64}$' }),
    Type.Integer({ minimum: -2147483648, maximum: 2147483647 }),
    { maxProperties: 72 },
  ),
});
export const GeneratorStudioRun = Strict({
  id: Uuid,
  expectedRevision: Revision,
  settings: GeneratorStudioSettings,
});
export type GeneratorStudioRun = Static<typeof GeneratorStudioRun>;
export const generatorStudioSchemas = {
  GeneratorStudioConfig,
  GeneratorStudioCreate,
  GeneratorStudioSave,
  GeneratorStudioCommand,
  GeneratorStudioRun,
};
