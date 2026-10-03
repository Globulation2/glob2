// Warm map pool (plan F): pre-generated maps per quick-match queue, map pool
// entry and sim version, so starting a match never waits for map generation.
//
// - WarmMapPool.refill() runs on the scheduler leader. For every queue, every
//   sim version some engine agent serves, and every pool entry, it keeps
//   `perEntry` maps 'ready' or 'generating', submitting generate-map jobs with
//   fresh seeds for the shortfall. Entries that keep failing back off.
// - recordWarmMapResult() completes a row when its job's result arrives
//   (called from handleEngineJobResult, in the result's transaction).
// - takeWarmMap() hands one ready map to a match starter and marks it taken;
//   the next refill replaces it.
import { createHash, randomInt, randomUUID } from 'node:crypto';
import { sql, type Kysely } from 'kysely';
import {
  modeSeats,
  submitEngineJob,
  type JobQueue,
  type Logger,
  type MapPoolEntry,
  type ResolvedQueue,
} from '@glob2/core';
import type { Database } from '@glob2/db';
import {
  parseSimVersionKey,
  type GenerateMapResult,
  type GeneratorDescriptor,
} from '@glob2/protocol';

type Db = Kysely<Database>;

/** Engine agents seen this recently count as serving their sim version. */
export const AGENT_FRESH_SECONDS = 300;
/** A generation job with no result after this long is given up. */
export const WARM_MAP_GENERATION_TIMEOUT_SECONDS = 1800;
/** After this many failures within the window, an entry is not retried until the window passes. */
export const WARM_MAP_FAILURE_LIMIT = 3;
export const WARM_MAP_FAILURE_WINDOW_SECONDS = 600;
/** Taken and failed rows are kept this long (for diagnosis), then deleted. */
export const WARM_MAP_RETENTION_HOURS = 24;

function canonical(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(canonical).join(',')}]`;
  if (value && typeof value === 'object') {
    const entries = Object.entries(value as Record<string, unknown>)
      .filter(([, v]) => v !== undefined)
      .sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0));
    return `{${entries.map(([k, v]) => `${JSON.stringify(k)}:${canonical(v)}`).join(',')}}`;
  }
  return JSON.stringify(value);
}

/** Stable identity of a map pool entry (the generator descriptor without its seed). */
export function poolEntryKey(entry: MapPoolEntry): string {
  const { generatorId, revision, params, candidates, startingUnitLevel } = entry;
  return createHash('sha256')
    .update(canonical({ generatorId, revision, params, candidates, startingUnitLevel }))
    .digest('hex');
}

/** Sim version keys with a fresh engine agent that runs generate-map. */
export async function servedSimVersions(
  db: Db,
  freshSeconds = AGENT_FRESH_SECONDS,
): Promise<string[]> {
  const rows = await db
    .selectFrom('engine_agents')
    .select('sim_version')
    .distinct()
    .where('last_seen_at', '>', sql<Date>`now() - make_interval(secs => ${freshSeconds})`)
    .where(sql<boolean>`'generate-map' = ANY(kinds)`)
    .orderBy('sim_version')
    .execute();
  return rows.map((r) => r.sim_version);
}

export interface WarmMapPoolOptions {
  db: Db;
  queue: JobQueue;
  queues: readonly ResolvedQueue[];
  /** Maps kept ready (or generating) per queue, entry and sim version; 0 disables the pool. */
  perEntry: number;
  logger?: Logger;
  /** Map seed source (uniform uint32); injectable for tests. */
  seed?: () => number;
}

export interface RefillResult {
  submitted: number;
  expired: number;
  deleted: number;
}

export class WarmMapPool {
  private readonly options: WarmMapPoolOptions;

  constructor(options: WarmMapPoolOptions) {
    this.options = options;
  }

  async refill(): Promise<RefillResult> {
    const { db, queue, queues, perEntry, logger } = this.options;
    const housekeeping = await this.housekeeping();
    let submitted = 0;
    if (perEntry <= 0 || queues.length === 0) return { submitted, ...housekeeping };
    const simVersions = await servedSimVersions(db);
    for (const simKey of simVersions) {
      const simVersion = parseSimVersionKey(simKey);
      if (!simVersion) continue;
      for (const q of queues) {
        const counts = await this.counts(q.id, simKey);
        for (const entry of q.mapPool) {
          const key = poolEntryKey(entry);
          const count = counts.get(key) ?? { open: 0, recentFailures: 0 };
          if (count.recentFailures >= WARM_MAP_FAILURE_LIMIT) continue;
          for (let i = count.open; i < perEntry; i++) {
            const jobId = randomUUID();
            // One team per seat, as the on-demand path in play/start.ts generates.
            const generator: GeneratorDescriptor = {
              ...entry,
              params: { ...entry.params, teams: modeSeats(q.mode) },
              seed: this.seed(),
            };
            await db
              .insertInto('warm_maps')
              .values({
                queue_id: q.id,
                sim_version: simKey,
                entry_key: key,
                generator: JSON.stringify(generator),
                job_id: jobId,
              })
              .execute();
            try {
              await submitEngineJob(db, queue, {
                kind: 'generate-map',
                simVersion,
                payload: { generator },
                jobId,
              });
              submitted++;
            } catch (error) {
              await db
                .updateTable('warm_maps')
                .set({ status: 'failed', failure: String((error as Error).message).slice(0, 2000) })
                .where('job_id', '=', jobId)
                .execute();
              logger?.warn(
                { err: error, queue: q.id, entry: entry.generatorId },
                'warm map job not submitted',
              );
              break;
            }
          }
        }
      }
    }
    if (submitted > 0) logger?.info({ submitted }, 'warm map generation submitted');
    return { submitted, ...housekeeping };
  }

  private seed(): number {
    return this.options.seed ? this.options.seed() : randomInt(0, 2 ** 32);
  }

  private async counts(
    queueId: string,
    simKey: string,
  ): Promise<Map<string, { open: number; recentFailures: number }>> {
    const rows = await this.options.db
      .selectFrom('warm_maps')
      .select((eb) => [
        'entry_key',
        eb.fn.count<string>('id').filterWhere('status', 'in', ['generating', 'ready']).as('open'),
        eb.fn
          .count<string>('id')
          .filterWhere((w) =>
            w.and([
              w('status', '=', 'failed'),
              w(
                'created_at',
                '>',
                sql<Date>`now() - make_interval(secs => ${WARM_MAP_FAILURE_WINDOW_SECONDS})`,
              ),
            ]),
          )
          .as('recent_failures'),
      ])
      .where('queue_id', '=', queueId)
      .where('sim_version', '=', simKey)
      .groupBy('entry_key')
      .execute();
    return new Map(
      rows.map((r) => [
        r.entry_key,
        { open: Number(r.open), recentFailures: Number(r.recent_failures) },
      ]),
    );
  }

  /** Expires lost jobs, drops maps of entries no longer configured, deletes old rows. */
  private async housekeeping(): Promise<{ expired: number; deleted: number }> {
    const { db, queues } = this.options;
    const expired = await db
      .updateTable('warm_maps')
      .set({ status: 'failed', failure: 'no generation result in time' })
      .where('status', '=', 'generating')
      .where(
        'created_at',
        '<',
        sql<Date>`now() - make_interval(secs => ${WARM_MAP_GENERATION_TIMEOUT_SECONDS})`,
      )
      .executeTakeFirst();
    const old = await db
      .deleteFrom('warm_maps')
      .where('status', 'in', ['taken', 'failed'])
      .where(
        'created_at',
        '<',
        sql<Date>`now() - make_interval(hours => ${WARM_MAP_RETENTION_HOURS})`,
      )
      .executeTakeFirst();
    // Ready maps for entries (or whole queues) that are no longer configured.
    const configured = queues.flatMap((q) => q.mapPool.map((e) => `${q.id}/${poolEntryKey(e)}`));
    let unconfigured = db.deleteFrom('warm_maps').where('status', 'in', ['ready', 'generating']);
    if (configured.length > 0) {
      unconfigured = unconfigured.where(
        sql<string>`queue_id || '/' || entry_key`,
        'not in',
        configured,
      );
    }
    const dropped = await unconfigured.executeTakeFirst();
    return {
      expired: Number(expired.numUpdatedRows),
      deleted: Number(old.numDeletedRows) + Number(dropped.numDeletedRows),
    };
  }
}

/**
 * Completes the warm map waiting for this generate-map job, if any: 'ready'
 * with the map hash on success, 'failed' otherwise. Returns whether a row
 * changed. Safe to call for any job id.
 */
export async function recordWarmMapResult(db: Db, jobId: string): Promise<boolean> {
  const job = await db
    .selectFrom('engine_jobs')
    .select(['kind', 'status', 'result', 'error'])
    .where('id', '=', jobId)
    .executeTakeFirst();
  if (!job || job.kind !== 'generate-map' || job.status === 'queued') return false;
  if (job.status === 'succeeded') {
    const result = job.result as unknown as GenerateMapResult;
    const updated = await db
      .updateTable('warm_maps')
      .set({
        status: 'ready',
        map_hash: result.mapHash,
        map_facts: JSON.stringify(result),
        ready_at: sql<Date>`now()`,
      })
      .where('job_id', '=', jobId)
      .where('status', '=', 'generating')
      .executeTakeFirst();
    return updated.numUpdatedRows > 0n;
  }
  const error = job.error as { message?: string } | null;
  const updated = await db
    .updateTable('warm_maps')
    .set({ status: 'failed', failure: (error?.message ?? 'generation failed').slice(0, 2000) })
    .where('job_id', '=', jobId)
    .where('status', '=', 'generating')
    .executeTakeFirst();
  return updated.numUpdatedRows > 0n;
}

export interface WarmMap {
  id: string;
  queueId: string;
  simVersion: string;
  /** The descriptor the map was generated from: MatchSetup map.generator. */
  generator: GeneratorDescriptor;
  /** SHA-256 of the decompressed map bytes: MatchSetup map.hash. */
  mapHash: string;
  /** The generate-map result (size, dimensions, team count, chosen seed). */
  facts: GenerateMapResult;
}

export interface TakeWarmMapOptions {
  /** Only a map of this pool entry (e.g. the one a proposal picked). */
  entry?: MapPoolEntry;
  /** Match the map is taken for, recorded on the row. */
  matchId?: string;
}

/**
 * Takes the oldest ready warm map of a queue and sim version, or undefined
 * when none is ready (the caller then submits its own generate-map job).
 * Concurrent callers never receive the same map.
 */
export async function takeWarmMap(
  db: Db,
  queueId: string,
  simVersionKey: string,
  options: TakeWarmMapOptions = {},
): Promise<WarmMap | undefined> {
  const entryKey = options.entry ? poolEntryKey(options.entry) : undefined;
  const row = await db
    .updateTable('warm_maps')
    .set({ status: 'taken', taken_at: sql<Date>`now()`, match_id: options.matchId ?? null })
    .where(
      'id',
      '=',
      db
        .selectFrom('warm_maps')
        .select('id')
        .where('queue_id', '=', queueId)
        .where('sim_version', '=', simVersionKey)
        .where('status', '=', 'ready')
        .$if(entryKey !== undefined, (qb) => qb.where('entry_key', '=', entryKey ?? ''))
        .orderBy('ready_at')
        .limit(1)
        .forUpdate()
        .skipLocked(),
    )
    .returningAll()
    .executeTakeFirst();
  if (!row || !row.map_hash) return undefined;
  return {
    id: row.id,
    queueId: row.queue_id,
    simVersion: row.sim_version,
    generator: row.generator as unknown as GeneratorDescriptor,
    mapHash: row.map_hash,
    facts: row.map_facts as unknown as GenerateMapResult,
  };
}
