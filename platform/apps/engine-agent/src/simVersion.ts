// Which sim version this agent serves, learned from its binary so a mislabelled
// image cannot verify (or generate maps for) games of another version.
//
// - VERSION_MINOR and NET_PROTOCOL_VERSION come from `--headless-catalog`
//   (`save_version`, `protocol_version`), which every current binary has.
// - The data hash comes from `--sim-version` when the binary supports it (the
//   flag is being added with the engine integration work). Until then it must
//   be supplied: ENGINE_DATA_HASH, or a full ENGINE_SIM_VERSION key.
// - An ENGINE_SIM_VERSION that disagrees with the binary is a startup error.
import { parseSimVersionKey, simVersionKey, type SimVersion } from '@glob2/protocol';
import { supportsSimVersionFlag, type EngineCatalog } from './engineCli.ts';
import type { GlobEngine } from './engine.ts';

export class SimVersionError extends Error {
  override name = 'SimVersionError';
}

export interface SimVersionSources {
  catalog: Pick<EngineCatalog, 'versionMinor' | 'netProtocol'>;
  /** What `--sim-version` printed, if the binary supports it. */
  reported?: SimVersion | undefined;
  env: SimVersionEnv;
}

export interface SimVersionEnv {
  ENGINE_DATA_HASH?: string | undefined;
  ENGINE_SIM_VERSION?: string | undefined;
  /** "1" runs `--sim-version` even when the catalog does not advertise it. */
  ENGINE_PROBE_SIM_VERSION?: string | undefined;
}

export interface ResolvedSimVersion {
  simVersion: SimVersion;
  /** Where the data hash came from. */
  dataHashSource: 'binary' | 'ENGINE_DATA_HASH' | 'ENGINE_SIM_VERSION';
}

export function resolveSimVersion(sources: SimVersionSources): ResolvedSimVersion {
  const { catalog, reported, env } = sources;
  if (
    reported &&
    (reported.versionMinor !== catalog.versionMinor || reported.netProtocol !== catalog.netProtocol)
  ) {
    throw new SimVersionError(
      `--sim-version reports ${reported.versionMinor}/${reported.netProtocol} but the catalog says ${catalog.versionMinor}/${catalog.netProtocol}`,
    );
  }
  let override: SimVersion | undefined;
  if (env.ENGINE_SIM_VERSION) {
    override = parseSimVersionKey(env.ENGINE_SIM_VERSION);
    if (!override) {
      throw new SimVersionError(
        'ENGINE_SIM_VERSION must be a sim version key <minor>-<net>-<sha256>',
      );
    }
    if (
      override.versionMinor !== catalog.versionMinor ||
      override.netProtocol !== catalog.netProtocol
    ) {
      throw new SimVersionError(
        `ENGINE_SIM_VERSION ${env.ENGINE_SIM_VERSION} does not match the binary (VERSION_MINOR ${catalog.versionMinor}, NET_PROTOCOL_VERSION ${catalog.netProtocol})`,
      );
    }
  }
  const envHash = env.ENGINE_DATA_HASH?.trim().toLowerCase();
  if (envHash !== undefined && envHash !== '' && !/^[0-9a-f]{64}$/.test(envHash)) {
    throw new SimVersionError('ENGINE_DATA_HASH must be 64 lowercase hex digits');
  }
  const candidates: [ResolvedSimVersion['dataHashSource'], string][] = [];
  if (reported) candidates.push(['binary', reported.dataHash]);
  if (envHash) candidates.push(['ENGINE_DATA_HASH', envHash]);
  if (override) candidates.push(['ENGINE_SIM_VERSION', override.dataHash]);
  const first = candidates[0];
  if (!first) {
    throw new SimVersionError(
      'cannot determine the data hash: the binary has no --sim-version; set ENGINE_DATA_HASH or ENGINE_SIM_VERSION',
    );
  }
  for (const [source, hash] of candidates) {
    if (hash !== first[1]) {
      throw new SimVersionError(`data hash from ${source} disagrees with ${first[0]}`);
    }
  }
  const simVersion = {
    versionMinor: catalog.versionMinor,
    netProtocol: catalog.netProtocol,
    dataHash: first[1],
  };
  return { simVersion, dataHashSource: first[0] };
}

export function describeSimVersion(resolved: ResolvedSimVersion): string {
  return `${simVersionKey(resolved.simVersion)} (data hash from ${resolved.dataHashSource})`;
}

/** Asks the binary (catalog `data_hash`, then `--sim-version`) and applies the environment fallbacks. */
export async function detectSimVersion(
  engine: Pick<GlobEngine, 'reportedSimVersion'>,
  catalog: EngineCatalog,
  env: SimVersionEnv,
): Promise<ResolvedSimVersion> {
  let reported: SimVersion | undefined;
  if (catalog.dataHash) {
    reported = {
      versionMinor: catalog.versionMinor,
      netProtocol: catalog.netProtocol,
      dataHash: catalog.dataHash,
    };
  } else if (supportsSimVersionFlag(catalog) || env.ENGINE_PROBE_SIM_VERSION === '1') {
    reported = await engine.reportedSimVersion();
  }
  return resolveSimVersion({ catalog, reported, env });
}
