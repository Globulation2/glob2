// Decoding of JSON documents read back from Postgres jsonb columns.
//
// Every document is validated when it is written (REST/realtime bodies, relay
// reports and engine-job results are checked against the protocol schemas at
// the boundary), but rows outlive the code that wrote them: a protocol change
// to MatchSetup or a job result would otherwise silently mis-read old rows in
// verify jobs, history pages and rematches. Reads therefore go through
// readStored(), which
//   1. reads the document's version (`schemaVersion`; documents without one
//      are version 1, the shape they had before versioning),
//   2. refuses versions newer than this code knows (a newer replica wrote the
//      row during a rolling upgrade) instead of guessing,
//   3. upgrades older versions one step at a time through the format's
//      `upgrades` table, and
//   4. validates the result against the current protocol schema (and its
//      semantic rules, when the format has any).
//
// Changing a stored shape incompatibly: bump the format's `current` (for
// MatchSetup, MATCH_SETUP_SCHEMA_VERSION in @glob2/protocol, which writers
// stamp into each document) and add `upgrades[old] = (doc) => newDoc`. Formats
// without a version field gain one at that point: absent still means 1, so
// existing rows take the 1 → 2 upgrade. Add the old shape as a fixture to
// packages/play/test/stored.test.ts so the upgrade stays covered.
import { Type, type Static, type TSchema } from 'typebox';
import type { MapPoolEntry } from '@glob2/core';
import {
  GenerateMapResult,
  GeneratorDescriptor,
  MATCH_SETUP_SCHEMA_VERSION,
  MatchSetup,
  RegionRtts,
  RelayMatchEnded,
  RenderPreviewResult,
  ValidateMapResult,
  VerifyVerdict,
  matchSetupProblems,
  schemaIssues,
  type ValidationIssue,
} from '@glob2/protocol';

/** A stored JSON document of a known format, and how to bring old versions up to date. */
export interface StoredFormat<T extends TSchema> {
  /** Names the column for errors and logs, e.g. 'matches.setup'. */
  what: string;
  schema: T;
  /** The version this code writes and reads. */
  current: number;
  /** upgrades[n] turns a version-n document into a version n+1 document. */
  upgrades?: Readonly<Record<number, (document: Record<string, unknown>) => unknown>>;
  /** Cross-field rules the JSON Schema cannot express. */
  semantic?: (value: Static<T>) => ValidationIssue[];
}

/** A stored document that is missing, from an unknown version, or does not match its schema. */
export class StoredDataError extends Error {
  readonly what: string;
  readonly version: number | undefined;
  readonly issues: ValidationIssue[];
  constructor(what: string, message: string, issues: ValidationIssue[] = [], version?: number) {
    super(
      `${what}: ${message}${
        issues.length > 0
          ? `: ${issues
              .slice(0, 5)
              .map((issue) => `${issue.path} ${issue.message}`)
              .join('; ')}`
          : ''
      }`,
    );
    this.name = 'StoredDataError';
    this.what = what;
    this.version = version;
    this.issues = issues;
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/** The document's `schemaVersion`; 1 when it has none (and for arrays and scalars). */
export function storedVersion(document: unknown): number | undefined {
  if (!isRecord(document) || !('schemaVersion' in document)) return 1;
  const version = document['schemaVersion'];
  return typeof version === 'number' && Number.isInteger(version) && version >= 1
    ? version
    : undefined;
}

/**
 * Decodes a stored document: checks its version, upgrades older versions and
 * validates the result. Throws StoredDataError when that is impossible.
 */
export function readStored<T extends TSchema>(
  format: StoredFormat<T>,
  document: unknown,
): Static<T> {
  if (document === null || document === undefined) {
    throw new StoredDataError(format.what, 'missing');
  }
  let value: unknown = document;
  let version = storedVersion(value);
  if (version === undefined) throw new StoredDataError(format.what, 'bad schemaVersion');
  if (version > format.current) {
    throw new StoredDataError(
      format.what,
      `version ${version} is newer than this platform reads (${format.current})`,
      [],
      version,
    );
  }
  while (version < format.current) {
    const upgrade = format.upgrades?.[version];
    if (!upgrade || !isRecord(value)) {
      throw new StoredDataError(format.what, `no upgrade from version ${version}`, [], version);
    }
    value = upgrade(value);
    const next = storedVersion(value);
    if (next !== version + 1) {
      throw new StoredDataError(
        format.what,
        `upgrade from version ${version} produced version ${String(next)}`,
        [],
        version,
      );
    }
    version = next;
  }
  const issues = schemaIssues(format.schema, value);
  if (issues.length > 0) {
    throw new StoredDataError(format.what, 'does not match the schema', issues, version);
  }
  const problems = format.semantic?.(value as Static<T>) ?? [];
  if (problems.length > 0) {
    throw new StoredDataError(format.what, 'breaks a semantic rule', problems, version);
  }
  return value as Static<T>;
}

/** readStored() that returns null for a SQL NULL instead of throwing. */
export function readStoredOrNull<T extends TSchema>(
  format: StoredFormat<T>,
  document: unknown,
): Static<T> | null {
  return document === null || document === undefined ? null : readStored(format, document);
}

/**
 * readStored() for read-only views that should degrade rather than fail (a
 * list page with one damaged row): the error is returned, not thrown.
 */
export function tryReadStored<T extends TSchema>(
  format: StoredFormat<T>,
  document: unknown,
): { ok: true; value: Static<T> } | { ok: false; error: StoredDataError } {
  try {
    return { ok: true, value: readStored(format, document) };
  } catch (error) {
    if (error instanceof StoredDataError) return { ok: false, error };
    throw error;
  }
}

// ------------------------------------------------------------------ formats

/** matches.setup and engine_jobs.payload.setup. */
export const STORED_MATCH_SETUP: StoredFormat<typeof MatchSetup> = {
  what: 'matches.setup',
  schema: MatchSetup,
  current: MATCH_SETUP_SCHEMA_VERSION,
  // No older MatchSetup versions exist yet: version 1 is the first.
  upgrades: {},
  semantic: matchSetupProblems,
};

/** matches.end_report: the relay's RelayMatchEnded, as received. */
export const STORED_END_REPORT: StoredFormat<typeof RelayMatchEnded> = {
  what: 'matches.end_report',
  schema: RelayMatchEnded,
  current: 1,
};

/** engine_jobs.result of verify-match jobs. */
export const STORED_VERIFY_VERDICT: StoredFormat<typeof VerifyVerdict> = {
  what: 'engine_jobs.result (verify-match)',
  schema: VerifyVerdict,
  current: 1,
};

/** engine_jobs.result of generate-map jobs. */
export const STORED_GENERATE_MAP_RESULT: StoredFormat<typeof GenerateMapResult> = {
  what: 'engine_jobs.result (generate-map)',
  schema: GenerateMapResult,
  current: 1,
};

/** engine_jobs.result of validate-map jobs. */
export const STORED_VALIDATE_MAP_RESULT: StoredFormat<typeof ValidateMapResult> = {
  what: 'engine_jobs.result (validate-map)',
  schema: ValidateMapResult,
  current: 1,
};

/** engine_jobs.result of render-preview jobs. */
export const STORED_RENDER_PREVIEW_RESULT: StoredFormat<typeof RenderPreviewResult> = {
  what: 'engine_jobs.result (render-preview)',
  schema: RenderPreviewResult,
  current: 1,
};

/** generated_maps.descriptor, map_versions.generator. */
export const STORED_GENERATOR: StoredFormat<typeof GeneratorDescriptor> = {
  what: 'generator descriptor',
  schema: GeneratorDescriptor,
  current: 1,
};

const MapPoolEntrySchema = Type.Omit(GeneratorDescriptor, ['seed']);

/** match_proposals.map: the queue's map pool entry. */
export const STORED_MAP_POOL_ENTRY: StoredFormat<typeof MapPoolEntrySchema> = {
  what: 'match_proposals.map',
  schema: MapPoolEntrySchema,
  current: 1,
};

/** queue_tickets.region_rtts and room_members.region_rtts. */
export const STORED_REGION_RTTS: StoredFormat<typeof RegionRtts> = {
  what: 'region_rtts',
  schema: RegionRtts,
  current: 1,
};

/** A stored map pool entry (match_proposals.map). */
export function readMapPoolEntry(document: unknown): MapPoolEntry {
  return readStored(STORED_MAP_POOL_ENTRY, document);
}

/**
 * Stored region round trips. Rows written before the client measured any
 * (NULL) read as no measurements.
 */
export function readRegionRtts(document: unknown): Static<typeof RegionRtts> {
  return document === null || document === undefined
    ? []
    : readStored(STORED_REGION_RTTS, document);
}
