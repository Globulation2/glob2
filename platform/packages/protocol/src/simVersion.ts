// A simulation version identifies which engine builds produce identical games.
// Rooms, queues, ratings of AI entities and verifiers are partitioned by it.
import { Type, type Static } from 'typebox';
import { Strict } from './common.ts';

export const SimVersion = Strict(
  {
    versionMinor: Type.Integer({
      minimum: 0,
      maximum: 65535,
      description: 'VERSION_MINOR in src/Version.h (save and simulation format).',
    }),
    netProtocol: Type.Integer({
      minimum: 0,
      maximum: 65535,
      description: 'NET_PROTOCOL_VERSION in src/Version.h.',
    }),
    dataHash: Type.String({
      pattern: '^[0-9a-f]{64}$',
      description:
        "Lowercase hex SHA-256 over the build's simulation data files, as computed by the engine.",
    }),
  },
  { description: 'Identity of a deterministic simulation build.' },
);
export type SimVersion = Static<typeof SimVersion>;

/**
 * Canonical string key for a sim version, e.g. `125-49-3f2a…` (full data hash).
 * Used in database columns, job task identifiers and URLs.
 */
export function simVersionKey(version: SimVersion): string {
  return `${version.versionMinor}-${version.netProtocol}-${version.dataHash}`;
}

const KEY_PATTERN = /^(\d{1,5})-(\d{1,5})-([0-9a-f]{64})$/;

export function parseSimVersionKey(key: string): SimVersion | undefined {
  const match = KEY_PATTERN.exec(key);
  if (!match) return undefined;
  return {
    versionMinor: Number(match[1]),
    netProtocol: Number(match[2]),
    dataHash: match[3] as string,
  };
}

export function sameSimVersion(a: SimVersion, b: SimVersion): boolean {
  return (
    a.versionMinor === b.versionMinor &&
    a.netProtocol === b.netProtocol &&
    a.dataHash === b.dataHash
  );
}
