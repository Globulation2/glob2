// Map sources for matches: generated maps (generate-map jobs, shared by
// descriptor), uploaded maps and saves (validate-map jobs), and the warm pool
// of pre-generated queue maps. Every source ends in a blob addressed by the
// SHA-256 of the bytes clients load; clients download it by that hash.
import { createHash } from 'node:crypto';
import { sql, type Kysely } from 'kysely';
import { contentKey, submitEngineJob, type JobQueue, type MapPoolEntry } from '@glob2/core';
import type { Database } from '@glob2/db';
import {
  parseSimVersionKey,
  type ResourceExperimentDefinitions,
  type BuildingCatalog,
  type GeneratorDescriptor,
} from '@glob2/protocol';
import {
  STORED_GENERATE_MAP_RESULT,
  STORED_RENDER_PREVIEW_RESULT,
  STORED_VALIDATE_MAP_RESULT,
  readStoredOrNull,
} from '../stored.ts';
import { applyCatalogPreview, applyCatalogValidation } from './catalog.ts';
import { notifyMapJob } from './notify.ts';

type Db = Kysely<Database>;

export const MAP_CONTENT_TYPE = 'application/x-glob2-map';
export const SAVE_CONTENT_TYPE = 'application/x-glob2-save';

/** JSON with object keys sorted, so equal values have equal text. */
export function canonicalJson(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(canonicalJson).join(',')}]`;
  if (value && typeof value === 'object') {
    const entries = Object.entries(value as Record<string, unknown>)
      .filter(([, v]) => v !== undefined)
      .sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0));
    return `{${entries.map(([k, v]) => `${JSON.stringify(k)}:${canonicalJson(v)}`).join(',')}}`;
  }
  return JSON.stringify(value);
}

export function descriptorHash(generator: GeneratorDescriptor): string {
  return createHash('sha256').update(canonicalJson(generator)).digest('hex');
}

// ------------------------------------------------------------ generated maps

export type GeneratedMapState =
  | { status: 'pending'; jobId: string | null }
  | {
      status: 'ready';
      mapHash: string;
      teamCount: number | null;
      jobId: string | null;
      buildingCatalog?: BuildingCatalog;
      resourceExperiments: ResourceExperimentDefinitions;
      requiredResourceExperiments: string[];
    }
  | { status: 'failed'; failure: string; jobId: string | null };

function stateOf(row: {
  status: 'pending' | 'ready' | 'failed';
  map_hash: string | null;
  team_count: number | null;
  failure: string | null;
  job_id: string | null;
  building_catalog: unknown;
  resource_experiments: ResourceExperimentDefinitions;
  required_resource_experiments: string[];
}): GeneratedMapState {
  if (row.status === 'ready' && row.map_hash) {
    return {
      status: 'ready',
      mapHash: row.map_hash,
      teamCount: row.team_count,
      resourceExperiments: row.resource_experiments,
      requiredResourceExperiments: row.required_resource_experiments,
      jobId: row.job_id,
      ...(row.building_catalog ? { buildingCatalog: row.building_catalog as BuildingCatalog } : {}),
    };
  }
  if (row.status === 'failed') {
    return { status: 'failed', failure: row.failure ?? 'generation failed', jobId: row.job_id };
  }
  return { status: 'pending', jobId: row.job_id };
}

export async function generatedMapState(
  db: Db,
  generator: GeneratorDescriptor,
  simVersion: string,
): Promise<GeneratedMapState | undefined> {
  const row = await db
    .selectFrom('generated_maps')
    .select([
      'status',
      'map_hash',
      'team_count',
      'failure',
      'job_id',
      'building_catalog',
      'resource_experiments',
      'required_resource_experiments',
    ])
    .where('descriptor_hash', '=', descriptorHash(generator))
    .where('sim_version', '=', simVersion)
    .executeTakeFirst();
  return row ? stateOf(row) : undefined;
}

/** A failed generation may be retried after this long (agents may have been down). */
const GENERATION_RETRY_SECONDS = 60;

/**
 * Returns the state of a generated map, starting its generate-map job when
 * this descriptor has never been generated for the sim version (or failed a
 * while ago). Concurrent callers share one job.
 */
export async function requestGeneratedMap(
  db: Db,
  _jobs: JobQueue,
  generator: GeneratorDescriptor,
  simVersion: string,
): Promise<GeneratedMapState> {
  return requestGeneration(db, generator, simVersion);
}

/**
 * requestGeneratedMap without a job queue (engine jobs are rows of
 * engine_jobs): the one way every map source starts a generate-map job,
 * including the warm pool. A failed submission marks the row failed and throws.
 */
export async function requestGeneration(
  db: Db,
  generator: GeneratorDescriptor,
  simVersion: string,
): Promise<GeneratedMapState> {
  const version = parseSimVersionKey(simVersion);
  if (!version) throw new Error(`bad sim version ${simVersion}`);
  const key = descriptorHash(generator);
  const claimed =
    (await db
      .insertInto('generated_maps')
      .values({
        descriptor_hash: key,
        sim_version: simVersion,
        descriptor: JSON.stringify(generator),
      })
      .onConflict((oc) => oc.columns(['descriptor_hash', 'sim_version']).doNothing())
      .returning('descriptor_hash')
      .executeTakeFirst()) ??
    (await db
      .updateTable('generated_maps')
      .set({ status: 'pending', failure: null, job_id: null, completed_at: null })
      .where('descriptor_hash', '=', key)
      .where('sim_version', '=', simVersion)
      .where('status', '=', 'failed')
      .where(
        'completed_at',
        '<',
        sql<Date>`now() - make_interval(secs => ${GENERATION_RETRY_SECONDS})`,
      )
      .returning('descriptor_hash')
      .executeTakeFirst());
  if (claimed) {
    try {
      const jobId = await submitEngineJob(db, {
        kind: 'generate-map',
        simVersion: version,
        payload: { generator },
      });
      // The job may already have finished; only fill in the id.
      await db
        .updateTable('generated_maps')
        .set({ job_id: jobId })
        .where('descriptor_hash', '=', key)
        .where('sim_version', '=', simVersion)
        .execute();
    } catch (error) {
      await db
        .updateTable('generated_maps')
        .set({ status: 'failed', failure: String(error), completed_at: sql<Date>`now()` })
        .where('descriptor_hash', '=', key)
        .where('sim_version', '=', simVersion)
        .where('status', '=', 'pending')
        .execute();
      throw error;
    }
  }
  const state = await generatedMapState(db, generator, simVersion);
  if (!state) throw new Error('generated map row vanished');
  return state;
}

/** Waits until a requested generated map is ready or failed. */
export async function waitForGeneratedMap(
  db: Db,
  generator: GeneratorDescriptor,
  simVersion: string,
  options: { timeoutMs: number; pollMs?: number; signal?: AbortSignal },
): Promise<GeneratedMapState> {
  const deadline = Date.now() + options.timeoutMs;
  for (;;) {
    const state = await generatedMapState(db, generator, simVersion);
    if (state && state.status !== 'pending') return state;
    if (Date.now() >= deadline || options.signal?.aborted) {
      return state ?? { status: 'pending', jobId: null };
    }
    await new Promise((resolve) => setTimeout(resolve, options.pollMs ?? 200));
  }
}

// ------------------------------------------------------------------ warm pool

/** A pre-generated map handed to a match starter. */
export interface PooledMap {
  /** The descriptor the map was generated from (seed included). */
  generator: GeneratorDescriptor;
  mapHash: string;
}

/**
 * Pre-generated queue maps: the warm pool (`takeWarmMap(db, queueId,
 * simVersionKey, { entry })` in warmMaps.ts, itself a layer over generated
 * maps). Without a pool, queue matches generate on demand.
 */
export interface WarmMapSource {
  /** Takes one ready map of the queue, sim version (and pool entry) out of the pool, if any. */
  takeWarmMap(
    queueId: string,
    simVersionKey: string,
    options?: { entry?: MapPoolEntry },
  ): Promise<PooledMap | undefined>;
}

export const noWarmMaps: WarmMapSource = { takeWarmMap: async () => undefined };

// --------------------------------------------------------------- job results

/**
 * Applies a completed generate-map, validate-map or render-preview job to the
 * map tables (called in the transaction that completes the engine job), and
 * notifies the API replicas so rooms waiting for the map update.
 */
export async function applyMapJobResult(db: Db, jobId: string): Promise<boolean> {
  const job = await db
    .selectFrom('engine_jobs')
    .select(['kind', 'status', 'payload', 'result', 'error', 'sim_version'])
    .where('id', '=', jobId)
    .executeTakeFirst();
  if (!job || job.status === 'queued') return false;
  const failure = job.status === 'failed' ? errorText(job.error) : undefined;

  if (job.kind === 'generate-map') {
    const generator = (job.payload as { generator: GeneratorDescriptor }).generator;
    const result = readStoredOrNull(STORED_GENERATE_MAP_RESULT, job.result);
    const where = {
      descriptor_hash: descriptorHash(generator),
      sim_version: job.sim_version,
    };
    if (failure === undefined && result) {
      await insertBlob(db, result.mapHash, result.size, MAP_CONTENT_TYPE, 'public');
      await db
        .updateTable('generated_maps')
        .set({
          status: 'ready',
          map_hash: result.mapHash,
          width: result.map.width,
          height: result.map.height,
          team_count: result.map.teamCount,
          resource_experiments: JSON.stringify(result.map.resourceExperiments ?? []),
          required_resource_experiments: JSON.stringify(
            result.map.requiredResourceExperiments ?? [],
          ),
          building_catalog: result.map.buildingCatalog
            ? JSON.stringify(result.map.buildingCatalog)
            : null,
          job_id: jobId,
          completed_at: sql<Date>`now()`,
        })
        .where('descriptor_hash', '=', where.descriptor_hash)
        .where('sim_version', '=', where.sim_version)
        .execute();
    } else {
      await db
        .updateTable('generated_maps')
        .set({
          status: 'failed',
          failure: failure ?? 'no result',
          job_id: jobId,
          completed_at: sql<Date>`now()`,
        })
        .where('descriptor_hash', '=', where.descriptor_hash)
        .where('sim_version', '=', where.sim_version)
        .where('status', '=', 'pending')
        .execute();
    }
    await notifyMapJob(db, { jobId, kind: 'generate-map' });
    return true;
  }

  if (job.kind === 'validate-map') {
    const payload = job.payload as { blobHash: string; format: 'map' | 'save' };
    const result = readStoredOrNull(STORED_VALIDATE_MAP_RESULT, job.result);
    const base = db
      .updateTable('map_uploads')
      .where('blob_sha256', '=', payload.blobHash)
      .where('format', '=', payload.format)
      .where('sim_version', '=', job.sim_version)
      .where('status', '=', 'pending');
    if (failure === undefined && result?.valid === true && result.mapHash === payload.blobHash) {
      await base
        .set({
          status: 'valid',
          width: result.map.width,
          height: result.map.height,
          team_count: result.map.teamCount,
          resource_experiments: JSON.stringify(result.map.resourceExperiments ?? []),
          required_resource_experiments: JSON.stringify(
            result.map.requiredResourceExperiments ?? [],
          ),
          building_catalog: result.map.buildingCatalog
            ? JSON.stringify(result.map.buildingCatalog)
            : null,
          version_minor: result.versionMinor,
          title: result.title ?? null,
          players: result.players ? JSON.stringify(result.players) : null,
          completed_at: sql<Date>`now()`,
        })
        .execute();
    } else {
      const reason =
        failure ??
        (result?.valid === false
          ? result.reason
          : 'This file could not be checked. Upload it again; if that fails too, save it again in the game first.');
      await base
        .set({ status: 'invalid', failure: reason.slice(0, 2000), completed_at: sql<Date>`now()` })
        .execute();
    }
    await applyCatalogValidation(db, jobId, payload.blobHash, result ?? undefined, failure);
    await notifyMapJob(db, { jobId, kind: 'validate-map' });
    return true;
  }

  if (job.kind === 'render-preview') {
    const result = readStoredOrNull(STORED_RENDER_PREVIEW_RESULT, job.result);
    if (failure === undefined && result) {
      await insertBlob(db, result.previewHash, null, 'image/png', 'private');
    }
    await applyCatalogPreview(db, jobId, failure === undefined && result ? result : undefined);
    return true;
  }
  return false;
}

function errorText(error: unknown): string {
  if (error && typeof error === 'object' && 'message' in error) {
    return String((error as { message: unknown }).message).slice(0, 2000);
  }
  return 'engine job failed';
}

/** Records a blob an engine agent stored (content-addressed; repeated inserts are no-ops). */
export async function insertBlob(
  db: Db,
  sha256: string,
  size: number | null,
  contentType: string,
  visibility: 'public' | 'private',
  owner: string | null = null,
): Promise<void> {
  await db
    .insertInto('blobs')
    .values({
      sha256,
      size: size ?? 0,
      content_type: contentType,
      storage_key: contentKey(sha256),
      visibility,
      owner_account_id: owner,
    })
    .onConflict((oc) => oc.column('sha256').doNothing())
    .execute();
}
