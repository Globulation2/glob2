// Warm map pool (plan F): pre-generated maps per quick-match queue, map pool
// entry and sim version, so starting a match never waits for map generation.
//
// The pool is a layer over generated_maps (play/maps.ts), the one map
// generation cache: a warm map is a generated map requested ahead of time with
// a fresh seed. Generation, its engine job, the result and any failure live in
// generated_maps and go through the same submission (requestGeneration) and
// result path (applyMapJobResult) as rooms and on-demand queue starts.
// warm_maps keeps only the pool bookkeeping: which queue entry the map is for,
// and when (and for which match) it was taken.
//
// - WarmMapPool.refill() runs on the scheduler leader. For every queue, every
//   sim version some engine agent serves, and every pool entry, it keeps
//   `perEntry` untaken maps pending or ready (more while the entry is busy: as
//   many as were taken in the last DEMAND_WINDOW_SECONDS, up to
//   `maxPerEntry`), requesting generated maps with fresh seeds for the
//   shortfall. Entries that keep failing back off.
// - takeWarmMap() hands one ready map to a match starter and marks it taken;
//   the next refill replaces it.
import { createHash, randomInt } from 'node:crypto';
import { sql, type Kysely, type RawBuilder } from 'kysely';
import {
  freshAgentSimVersions,
  modeSeats,
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
import { canonicalJson, descriptorHash, requestGeneration } from './play/maps.ts';
import { STORED_GENERATE_MAP_RESULT, STORED_GENERATOR, readStored } from './stored.ts';

type Db = Kysely<Database>;

/** A pool map still generating after this long is given up (and replaced). */
export const WARM_MAP_GENERATION_TIMEOUT_SECONDS = 1800;
/** After this many failures within the window, an entry is not retried until the window passes. */
export const WARM_MAP_FAILURE_LIMIT = 3;
export const WARM_MAP_FAILURE_WINDOW_SECONDS = 600;
/** Taken and failed pool maps are kept this long (for diagnosis), then deleted. */
export const WARM_MAP_RETENTION_HOURS = 24;
/** Maps taken from an entry within this window raise its target (burst demand). */
export const DEMAND_WINDOW_SECONDS = 900;
/** Default maps kept per entry (WARM_MAPS_PER_ENTRY) and the demand-driven ceiling. */
export const DEFAULT_WARM_MAPS_PER_ENTRY = 2;
export const DEFAULT_WARM_MAPS_MAX_PER_ENTRY = 8;

/** Stable identity of a map pool entry (the generator descriptor without its seed). */
export function poolEntryKey(entry: MapPoolEntry): string {
  const { generatorId, revision, params, candidates, startingUnitLevel } = entry;
  return createHash('sha256')
    .update(canonicalJson({ generatorId, revision, params, candidates, startingUnitLevel }))
    .digest('hex');
}

/** Sim version keys with a fresh engine agent that runs generate-map. */
export function servedSimVersions(db: Db): Promise<string[]> {
  return freshAgentSimVersions(db, { kind: 'generate-map' });
}

export interface WarmMapPoolOptions {
  db: Db;
  queues: readonly ResolvedQueue[];
  /** Maps kept ready (or generating) per queue, entry and sim version; 0 disables the pool. */
  perEntry: number;
  /** Ceiling of the demand-driven target (default max(perEntry, 8)). */
  maxPerEntry?: number;
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
    const { db, queues, perEntry, logger } = this.options;
    const housekeeping = await this.housekeeping();
    let submitted = 0;
    if (perEntry <= 0 || queues.length === 0) return { submitted, ...housekeeping };
    const simVersions = await servedSimVersions(db);
    for (const simKey of simVersions) {
      if (!parseSimVersionKey(simKey)) continue;
      for (const q of queues) {
        const counts = await this.counts(q.id, simKey);
        for (const entry of q.mapPool) {
          const key = poolEntryKey(entry);
          const count = counts.get(key) ?? { open: 0, recentFailures: 0, recentlyTaken: 0 };
          if (count.recentFailures >= WARM_MAP_FAILURE_LIMIT) continue;
          const ceiling = Math.max(
            perEntry,
            this.options.maxPerEntry ?? DEFAULT_WARM_MAPS_MAX_PER_ENTRY,
          );
          const target = Math.min(ceiling, Math.max(perEntry, count.recentlyTaken));
          for (let i = count.open; i < target; i++) {
            // One team per seat, as the on-demand path in play/start.ts generates.
            const generator: GeneratorDescriptor = {
              ...entry,
              params: { ...entry.params, teams: modeSeats(q.mode) },
              seed: this.seed(),
            };
            let failure: unknown;
            try {
              await requestGeneration(db, generator, simKey);
            } catch (error) {
              // The generated map is now 'failed'; the pool row below counts
              // it towards the entry's backoff.
              failure = error;
            }
            await db
              .insertInto('warm_maps')
              .values({
                queue_id: q.id,
                sim_version: simKey,
                entry_key: key,
                descriptor_hash: descriptorHash(generator),
              })
              .execute();
            if (failure !== undefined) {
              logger?.warn(
                { err: failure, queue: q.id, entry: entry.generatorId },
                'warm map job not submitted',
              );
              break;
            }
            submitted++;
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
  ): Promise<Map<string, { open: number; recentFailures: number; recentlyTaken: number }>> {
    const rows = await sql<{
      entry_key: string;
      open: string;
      recent_failures: string;
      recently_taken: string;
    }>`
      SELECT w.entry_key,
        count(*) FILTER (WHERE w.taken_at IS NULL AND g.status IN ('pending', 'ready')) AS open,
        count(*) FILTER (
          WHERE g.status = 'failed'
            AND w.created_at > now() - make_interval(secs => ${WARM_MAP_FAILURE_WINDOW_SECONDS})
        ) AS recent_failures,
        count(*) FILTER (
          WHERE w.taken_at > now() - make_interval(secs => ${DEMAND_WINDOW_SECONDS})
        ) AS recently_taken
      FROM warm_maps w
      LEFT JOIN generated_maps g
        ON g.descriptor_hash = w.descriptor_hash AND g.sim_version = w.sim_version
      WHERE w.queue_id = ${queueId} AND w.sim_version = ${simKey}
      GROUP BY w.entry_key`.execute(this.options.db);
    return new Map(
      rows.rows.map((r) => [
        r.entry_key,
        {
          open: Number(r.open),
          recentFailures: Number(r.recent_failures),
          recentlyTaken: Number(r.recently_taken),
        },
      ]),
    );
  }

  /**
   * Gives up pool maps still generating after the timeout, drops untaken maps
   * of entries no longer configured, and deletes old taken and failed rows.
   */
  private async housekeeping(): Promise<{ expired: number; deleted: number }> {
    const { db, queues } = this.options;
    const expired = await this.dropUntaken(
      sql<boolean>`g.status = 'pending'
        AND w.created_at < now() - make_interval(secs => ${WARM_MAP_GENERATION_TIMEOUT_SECONDS})`,
    );
    const failed = await this.dropUntaken(
      sql<boolean>`g.status = 'failed'
        AND w.created_at < now() - make_interval(hours => ${WARM_MAP_RETENTION_HOURS})`,
    );
    const taken = await db
      .deleteFrom('warm_maps')
      .where('taken_at', 'is not', null)
      .where(
        'created_at',
        '<',
        sql<Date>`now() - make_interval(hours => ${WARM_MAP_RETENTION_HOURS})`,
      )
      .executeTakeFirst();
    // Maps for entries (or whole queues) that are no longer configured.
    const configured = queues.flatMap((q) => q.mapPool.map((e) => `${q.id}/${poolEntryKey(e)}`));
    const unconfigured = await this.dropUntaken(
      sql<boolean>`g.status IN ('pending', 'ready')
        AND (w.queue_id || '/' || w.entry_key) <> ALL(${configured}::text[])`,
    );
    return {
      expired,
      deleted: failed + Number(taken.numDeletedRows) + unconfigured,
    };
  }

  /**
   * Deletes untaken pool maps matching `condition` (over warm_maps w and
   * generated_maps g) together with their generated maps: nothing else asks
   * for a descriptor with the pool's random seed, so the map's blob becomes
   * collectable. A map that was handed out keeps its generated map, like any
   * map a match was played on. Returns how many were deleted.
   */
  private async dropUntaken(condition: RawBuilder<boolean>): Promise<number> {
    const result = await sql`
      DELETE FROM generated_maps g
      USING warm_maps w
      WHERE w.descriptor_hash = g.descriptor_hash
        AND w.sim_version = g.sim_version
        AND w.taken_at IS NULL
        AND ${condition}
        AND NOT EXISTS (
          SELECT 1 FROM warm_maps t
          WHERE t.descriptor_hash = g.descriptor_hash
            AND t.sim_version = g.sim_version
            AND t.taken_at IS NOT NULL
        )`.execute(this.options.db);
    // The pool rows go with them (warm_maps_generated_fkey cascades).
    return Number(result.numAffectedRows ?? 0n);
  }
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
  const entryKey = options.entry ? poolEntryKey(options.entry) : null;
  const taken = await sql<{
    id: string;
    queue_id: string;
    sim_version: string;
    descriptor: unknown;
    map_hash: string;
    result: unknown;
  }>`
    WITH pick AS (
      SELECT w.id
      FROM warm_maps w
      JOIN generated_maps g
        ON g.descriptor_hash = w.descriptor_hash AND g.sim_version = w.sim_version
      WHERE w.queue_id = ${queueId}
        AND w.sim_version = ${simVersionKey}
        AND w.taken_at IS NULL
        AND g.status = 'ready'
        AND (${entryKey}::text IS NULL OR w.entry_key = ${entryKey})
      ORDER BY g.completed_at, w.id
      LIMIT 1
      FOR UPDATE OF w SKIP LOCKED
    )
    UPDATE warm_maps w
    SET taken_at = now(), match_id = ${options.matchId ?? null}
    FROM pick, generated_maps g
    LEFT JOIN engine_jobs j ON j.id = g.job_id
    WHERE w.id = pick.id
      AND g.descriptor_hash = w.descriptor_hash
      AND g.sim_version = w.sim_version
    RETURNING w.id, w.queue_id, w.sim_version, g.descriptor, g.map_hash, j.result`.execute(db);
  const row = taken.rows[0];
  if (!row?.map_hash) return undefined;
  return {
    id: row.id,
    queueId: row.queue_id,
    simVersion: row.sim_version,
    generator: readStored(STORED_GENERATOR, row.descriptor),
    mapHash: row.map_hash,
    facts: readStored(STORED_GENERATE_MAP_RESULT, row.result),
  };
}
