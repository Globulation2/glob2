// Stale engine jobs. An engine_jobs row stays 'queued' until its result is
// applied. Agents lease jobs from the row itself (packages/core engineJobs.ts),
// and a job whose last lease ran out without a report is failed by
// failAbandonedEngineJobs. Two cases remain that would otherwise wait forever:
//
//   lost      the agent reported (reported_at is set), but the result task
//             that carries the report to the worker is gone (it ran out of
//             attempts, or the queue lost it), so nothing will apply it;
//   unserved  no agent of the job's sim version has been seen for a while,
//             so nobody will lease it.
//
// The sweep gives such jobs up through the normal result path
// (handleEngineJobResult with an `internal` failure), so verify-match marks its
// match 'failed' (an operator can re-run it), generated maps and uploads fail
// visibly, and warm maps are replaced. Runs on the scheduler leader.
//
// It reads graphile-worker's job table (graphile_worker._private_jobs, schema
// of graphile-worker 0.18): a pending result is an ENGINE_RESULT_TASK job
// naming the engine job id in its payload.
import { sql, type Kysely } from 'kysely';
import { ENGINE_AGENT_FRESH_SECONDS, type Logger } from '@glob2/core';
import type { Database } from '@glob2/db';
import { ENGINE_RESULT_TASK, type EngineJobKind } from '@glob2/protocol';
import { handleEngineJobResult } from '../ratings/apply.ts';

type Db = Kysely<Database>;

/** A reported job whose result has not been applied is only considered lost after this long. */
export const STALE_ENGINE_JOB_SECONDS = 600;
/** Without any agent of its sim version, a queued job is given up after this long. */
export const UNSERVED_ENGINE_JOB_SECONDS = 6 * 3600;

export interface StaleJob {
  jobId: string;
  kind: EngineJobKind;
  reason: 'lost' | 'unserved';
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
      CASE WHEN j.reported_at IS NOT NULL THEN 'lost' ELSE 'unserved' END AS reason
    FROM engine_jobs j
    WHERE j.status = 'queued'
      AND (
        (
          j.reported_at IS NOT NULL
          AND j.reported_at < now() - make_interval(secs => ${stale})
          -- A result on its way (the worker has not applied it yet).
          AND NOT EXISTS (
            SELECT 1 FROM graphile_worker._private_jobs r
            JOIN graphile_worker._private_tasks t ON t.id = r.task_id
            WHERE t.identifier = ${ENGINE_RESULT_TASK}
              AND r.payload ->> 'jobId' = j.id::text
              AND r.attempts < r.max_attempts
          )
        )
        OR (
          j.reported_at IS NULL
          AND j.created_at < now() - make_interval(secs => ${unserved})
          AND (j.lease_expires_at IS NULL OR j.lease_expires_at < now())
          AND NOT EXISTS (
            SELECT 1 FROM engine_agents a
            WHERE a.sim_version = j.sim_version
              AND a.last_seen_at > now() - make_interval(secs => ${ENGINE_AGENT_FRESH_SECONDS})
          )
        )
      )
    ORDER BY j.created_at
    LIMIT ${options.limit ?? 100}`.execute(db);
  return rows.rows.map((r) => ({ jobId: r.id, kind: r.kind, reason: r.reason }));
}

const MESSAGES: Record<StaleJob['reason'], string> = {
  lost: 'the engine agent reported a result, but it was lost before it was applied',
  unserved: 'no engine agent of this sim version took the job',
};

/** Gives up stale engine jobs; returns them. */
export async function sweepStaleEngineJobs(
  db: Db,
  options: SweepOptions = {},
): Promise<StaleJob[]> {
  const stale = await findStaleEngineJobs(db, options);
  const given: StaleJob[] = [];
  for (const job of stale) {
    if (job.reason === 'unserved') {
      // Close the job to leases first: an agent that appears now must not run
      // it after it was given up.
      const claimed = await db
        .updateTable('engine_jobs')
        .set({ reported_at: sql<Date>`now()` })
        .where('id', '=', job.jobId)
        .where('status', '=', 'queued')
        .where('reported_at', 'is', null)
        .where((eb) =>
          eb.or([
            eb('lease_expires_at', 'is', null),
            eb('lease_expires_at', '<', sql<Date>`now()`),
          ]),
        )
        .executeTakeFirst();
      if (Number(claimed.numUpdatedRows) !== 1) continue;
    }
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
    given.push(job);
  }
  return given;
}
