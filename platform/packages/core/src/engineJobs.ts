// Engine jobs end to end: the platform records and enqueues a job for one sim
// version; an engine agent of that version runs it and enqueues the result;
// apps/worker applies the result. See the protocol package's jobs.ts.
//
// The engine_jobs row and its graphile-worker job are written in one
// transaction (graphile_worker.add_job in SQL), so a job is never recorded
// without being queued or queued without being recorded.
import { randomUUID } from 'node:crypto';
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import {
  ENGINE_RESULT_TASK,
  EngineJob,
  EngineJobResult,
  checkDocument,
  engineJobs,
  engineTaskIdentifier,
  parse,
  schemaIssues,
  simVersionKey,
  type EngineJobKind,
  type EngineJobPayload,
  type SimVersion,
} from '@glob2/protocol';
import type { JobQueue } from './jobs.ts';

export interface SubmitEngineJob<K extends EngineJobKind> {
  kind: K;
  simVersion: SimVersion;
  payload: EngineJobPayload<K>;
  /** graphile-worker retry budget (default 3). */
  maxAttempts?: number;
  /** Job id to use (default: a new UUID), for callers that record it before submitting. */
  jobId?: string;
}

const UUID_PATTERN = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;

/** The match a job payload names (verify-match), for engine_jobs.match_id. */
function payloadMatchId(payload: unknown): string | null {
  const id = (payload as { matchId?: unknown } | null)?.matchId;
  return typeof id === 'string' && UUID_PATTERN.test(id) ? id : null;
}

/**
 * Records and enqueues an engine job atomically; returns its id. With a
 * transaction as `db`, both commit (or roll back) with it.
 */
export async function submitEngineJob<K extends EngineJobKind>(
  db: Kysely<Database>,
  request: SubmitEngineJob<K>,
): Promise<string> {
  const job = {
    jobId: request.jobId ?? randomUUID(),
    kind: request.kind,
    simVersion: request.simVersion,
    payload: request.payload,
  };
  const check = checkDocument('EngineJob', job);
  if (check.stage !== 'ok') {
    throw new Error(
      `invalid ${request.kind} job: ${check.issues.map((i) => `${i.path} ${i.message}`).join('; ')}`,
    );
  }
  const write = async (trx: Kysely<Database>) => {
    await trx
      .insertInto('engine_jobs')
      .values({
        id: job.jobId,
        kind: request.kind,
        sim_version: simVersionKey(request.simVersion),
        payload: JSON.stringify(request.payload),
        match_id: payloadMatchId(request.payload),
      })
      .execute();
    await sql`SELECT graphile_worker.add_job(
        identifier => ${engineTaskIdentifier(request.kind, request.simVersion)}::text,
        payload => ${JSON.stringify(job)}::json,
        max_attempts => ${request.maxAttempts ?? 3}::int,
        job_key => ${job.jobId}::text
      )`.execute(trx);
  };
  if (db.isTransaction) await write(db);
  else await db.transaction().execute(write);
  return job.jobId;
}

/** Parses a job an engine agent received. */
export function parseEngineJob(payload: unknown): EngineJob {
  return parse(EngineJob, payload, 'engine job') as EngineJob;
}

/** Enqueues the agent's result for apps/worker. */
export async function reportEngineJobResult(
  queue: JobQueue,
  result: EngineJobResult,
): Promise<void> {
  parse(EngineJobResult, result, 'engine job result');
  await queue.enqueue(ENGINE_RESULT_TASK, result, { maxAttempts: 10 });
}

/**
 * Applies an agent's result to the engine_jobs row. A successful result must
 * match its kind's result schema; one that does not is recorded as a failure.
 * Returns false when the job is unknown or already completed.
 */
export async function applyEngineJobResult(db: Kysely<Database>, raw: unknown): Promise<boolean> {
  const report = parse(EngineJobResult, raw, 'engine job result') as EngineJobResult;
  let status: 'succeeded' | 'failed' = 'failed';
  let result: string | null = null;
  let error: string | null = null;
  if (report.ok === true) {
    const issues = schemaIssues(engineJobs[report.kind].result, report.result);
    if (issues.length === 0) {
      status = 'succeeded';
      result = JSON.stringify(report.result);
    } else {
      error = JSON.stringify({
        code: 'internal',
        message: 'agent result does not match the contract',
        details: issues,
      });
    }
  } else {
    error = JSON.stringify(report.error);
  }
  const updated = await db
    .updateTable('engine_jobs')
    .set({
      status,
      result,
      error,
      agent_id: report.agent,
      completed_at: new Date(),
    })
    .where('id', '=', report.jobId)
    .where('kind', '=', report.kind)
    .where('status', '=', 'queued')
    .executeTakeFirst();
  return updated.numUpdatedRows > 0n;
}
