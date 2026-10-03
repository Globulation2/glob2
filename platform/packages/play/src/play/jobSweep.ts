// Stale engine jobs. An engine_jobs row stays 'queued' until its result is
// applied; the result never comes when the queue job is lost or ran out of
// attempts without reporting (an agent killed during its last attempt), or when
// no agent serves the job's sim version any more. The sweep gives such jobs up
// through the normal result path (handleEngineJobResult with an `internal`
// failure), so verify-match marks its match 'failed' (an operator can re-run
// it), generated maps and uploads fail visibly, and warm maps are replaced.
// Runs on the scheduler leader.
//
// It reads graphile-worker's job table (graphile_worker._private_jobs, schema
// of graphile-worker 0.18): a job's key is the engine job id (submitEngineJob),
// and an agent's result is a separate ENGINE_RESULT_TASK job naming the
// engine job id in its payload.
import { sql, type Kysely } from 'kysely';
import type { Logger } from '@glob2/core';
import type { Database } from '@glob2/db';
import { ENGINE_RESULT_TASK, type EngineJobKind } from '@glob2/protocol';
import { handleEngineJobResult } from '../ratings/apply.ts';

type Db = Kysely<Database>;

/** A queued engine job is only considered stale after this long. */
export const STALE_ENGINE_JOB_SECONDS = 600;
/** Without any agent of its sim version, a queued job is given up after this long. */
export const UNSERVED_ENGINE_JOB_SECONDS = 6 * 3600;
/** Engine agents seen this recently serve their sim version. */
const AGENT_FRESH_SECONDS = 300;

export interface StaleJob {
  jobId: string;
  kind: EngineJobKind;
  reason: 'lost' | 'exhausted' | 'unserved';
}

export interface SweepOptions {
  logger?: Pick<Logger, 'warn' | 'error'>;
  staleSeconds?: number;
  unservedSeconds?: number;
  limit?: number;
}

/** Finds queued engine jobs that will never get a result. */
export async function findStaleEngineJobs(db: Db, options: SweepOptions = {}): Promise<StaleJob[]> {
  const stale = options.staleSeconds ?? STALE_ENGINE_JOB_SECONDS;
  const unserved = options.unservedSeconds ?? UNSERVED_ENGINE_JOB_SECONDS;
  const rows = await sql<{ id: string; kind: EngineJobKind; reason: StaleJob['reason'] }>`
    SELECT j.id, j.kind,
      CASE
        WHEN q.id IS NULL THEN 'lost'
        WHEN q.attempts >= q.max_attempts AND q.locked_at IS NULL THEN 'exhausted'
        ELSE 'unserved'
      END AS reason
    FROM engine_jobs j
    LEFT JOIN graphile_worker._private_jobs q ON q.key = j.id::text
    WHERE j.status = 'queued'
      AND j.created_at < now() - make_interval(secs => ${stale})
      AND (
        q.id IS NULL
        OR (q.attempts >= q.max_attempts AND q.locked_at IS NULL)
        OR (
          j.created_at < now() - make_interval(secs => ${unserved})
          AND q.locked_at IS NULL
          AND NOT EXISTS (
            SELECT 1 FROM engine_agents a
            WHERE a.sim_version = j.sim_version
              AND a.last_seen_at > now() - make_interval(secs => ${AGENT_FRESH_SECONDS})
          )
        )
      )
      -- A result on its way (the agent finished; the worker has not applied it yet).
      AND NOT EXISTS (
        SELECT 1 FROM graphile_worker._private_jobs r
        JOIN graphile_worker._private_tasks t ON t.id = r.task_id
        WHERE t.identifier = ${ENGINE_RESULT_TASK}
          AND r.payload ->> 'jobId' = j.id::text
          AND r.attempts < r.max_attempts
      )
    ORDER BY j.created_at
    LIMIT ${options.limit ?? 100}`.execute(db);
  return rows.rows.map((r) => ({ jobId: r.id, kind: r.kind, reason: r.reason }));
}

const MESSAGES: Record<StaleJob['reason'], string> = {
  lost: 'the queued job was lost before an engine agent reported a result',
  exhausted: 'the job ran out of attempts without a result (engine agent killed or crashed)',
  unserved: 'no engine agent of this sim version took the job',
};

/** Gives up stale engine jobs; returns them. */
export async function sweepStaleEngineJobs(
  db: Db,
  options: SweepOptions = {},
): Promise<StaleJob[]> {
  const stale = await findStaleEngineJobs(db, options);
  for (const job of stale) {
    // The queue job (if any) must not run after all once the row is failed.
    await sql`SELECT graphile_worker.remove_job(${job.jobId}::text)`.execute(db);
    await handleEngineJobResult(
      db,
      {
        jobId: job.jobId,
        kind: job.kind,
        ok: false,
        error: { code: 'internal', message: `gave up: ${MESSAGES[job.reason]}` },
        agent: 'platform-sweep',
      },
      options.logger ? { logger: options.logger } : {},
    );
    options.logger?.warn(
      { job: job.jobId, kind: job.kind, reason: job.reason },
      'gave up a stale engine job',
    );
  }
  return stale;
}
