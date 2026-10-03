// Engine jobs end to end: the platform records a job for one sim version in
// engine_jobs; an engine agent of that version leases it through platform-api
// (apps/api/src/routes/engine.ts), runs it and reports the result, which is
// enqueued for apps/worker to apply. Agents never touch the database. See the
// protocol package's jobs.ts.
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import {
  ENGINE_RESULT_TASK,
  EngineJob,
  EngineJobResult,
  checkDocument,
  engineJobs,
  parse,
  parseSimVersionKey,
  schemaIssues,
  simVersionKey,
  type EngineJobKind,
  type EngineJobPayload,
  type EngineJobReport,
  type EngineLease,
  type SimVersion,
} from '@glob2/protocol';

export interface SubmitEngineJob<K extends EngineJobKind> {
  kind: K;
  simVersion: SimVersion;
  payload: EngineJobPayload<K>;
  /** Leases before the job is failed (default 3). */
  maxAttempts?: number;
  /** Job id to use (default: a new UUID), for callers that record it before submitting. */
  jobId?: string;
}

/**
 * Records an engine job; an agent of its sim version leases it from there.
 * One insert, so a job submitted inside a transaction exists exactly when the
 * transaction commits. Returns its id.
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
  await db
    .insertInto('engine_jobs')
    .values({
      id: job.jobId,
      kind: request.kind,
      sim_version: simVersionKey(request.simVersion),
      payload: JSON.stringify(request.payload),
      max_attempts: request.maxAttempts ?? 3,
    })
    .execute();
  return job.jobId;
}

/** Parses a job an engine agent received. */
export function parseEngineJob(payload: unknown): EngineJob {
  return parse(EngineJob, payload, 'engine job') as EngineJob;
}

const hashToken = (token: string) => createHash('sha256').update(token).digest('hex');

export interface LeaseRequest {
  agentId: string;
  simVersion: SimVersion;
  kinds: readonly EngineJobKind[];
  leaseSeconds: number;
}

/**
 * Hands the oldest leasable job of the agent's sim version and kinds to the
 * agent: queued, not reported, retries left, and not leased (or its lease ran
 * out). Concurrent callers never get the same job (SKIP LOCKED).
 */
export async function leaseEngineJob(
  db: Kysely<Database>,
  request: LeaseRequest,
): Promise<EngineLease | undefined> {
  const token = randomBytes(32).toString('base64url');
  const row = await sql<{
    id: string;
    kind: EngineJobKind;
    sim_version: string;
    payload: unknown;
    attempts: number;
    max_attempts: number;
    lease_expires_at: Date;
  }>`
    WITH next AS (
      SELECT id FROM engine_jobs
      WHERE status = 'queued' AND reported_at IS NULL
        AND sim_version = ${simVersionKey(request.simVersion)}
        AND kind = ANY(${[...request.kinds]}::text[])
        AND attempts < max_attempts
        AND (lease_expires_at IS NULL OR lease_expires_at < now())
      ORDER BY created_at, id
      LIMIT 1
      FOR UPDATE SKIP LOCKED
    )
    UPDATE engine_jobs j
    SET attempts = j.attempts + 1,
        leased_by = ${request.agentId},
        lease_token_hash = ${hashToken(token)},
        lease_expires_at = now() + make_interval(secs => ${request.leaseSeconds})
    FROM next WHERE j.id = next.id
    RETURNING j.id, j.kind, j.sim_version, j.payload, j.attempts, j.max_attempts, j.lease_expires_at`
    .execute(db)
    .then((r) => r.rows[0]);
  if (!row) return undefined;
  const simVersion = parseSimVersionKey(row.sim_version);
  if (!simVersion) throw new Error(`engine job ${row.id} has an invalid sim version`);
  return {
    job: parseEngineJob({ jobId: row.id, kind: row.kind, simVersion, payload: row.payload }),
    leaseToken: token,
    attempt: row.attempts,
    maxAttempts: row.max_attempts,
    leaseExpiresAt: row.lease_expires_at.toISOString(),
  };
}

/** The job a lease token currently holds (unexpired, not reported), if any. */
export async function leasedJob(
  db: Kysely<Database>,
  token: string,
  jobId?: string,
): Promise<{ id: string; kind: EngineJobKind; leased_by: string | null } | undefined> {
  let query = db
    .selectFrom('engine_jobs')
    .select(['id', 'kind', 'leased_by'])
    .where('lease_token_hash', '=', hashToken(token))
    .where('status', '=', 'queued')
    .where('reported_at', 'is', null)
    .where('lease_expires_at', '>', sql<Date>`now()`);
  if (jobId) query = query.where('id', '=', jobId);
  return query.executeTakeFirst();
}

/** Extends a held lease; false if the lease was lost (expired and taken, or reported). */
export async function extendEngineLease(
  db: Kysely<Database>,
  jobId: string,
  token: string,
  leaseSeconds: number,
): Promise<boolean> {
  const result = await db
    .updateTable('engine_jobs')
    .set({ lease_expires_at: sql<Date>`now() + make_interval(secs => ${leaseSeconds})` })
    .where('id', '=', jobId)
    .where('lease_token_hash', '=', hashToken(token))
    .where('status', '=', 'queued')
    .where('reported_at', 'is', null)
    .executeTakeFirst();
  return Number(result.numUpdatedRows) === 1;
}

/** Gives a job back for a retry after `retryAfterSeconds` (the attempt stays counted). */
export async function releaseEngineLease(
  db: Kysely<Database>,
  jobId: string,
  token: string,
  retryAfterSeconds: number,
): Promise<boolean> {
  const result = await db
    .updateTable('engine_jobs')
    .set({
      lease_token_hash: null,
      lease_expires_at: sql<Date>`now() + make_interval(secs => ${retryAfterSeconds})`,
    })
    .where('id', '=', jobId)
    .where('lease_token_hash', '=', hashToken(token))
    .where('status', '=', 'queued')
    .where('reported_at', 'is', null)
    .executeTakeFirst();
  return Number(result.numUpdatedRows) === 1;
}

/** Enqueues a result for apps/worker in the caller's transaction. */
async function enqueueResult(db: Kysely<Database>, result: EngineJobResult): Promise<void> {
  parse(EngineJobResult, result, 'engine job result');
  await sql`SELECT graphile_worker.add_job(
      identifier => ${ENGINE_RESULT_TASK}::text,
      payload => ${JSON.stringify(result)}::json,
      max_attempts => 10)`.execute(db);
}

export type ReportOutcome = 'accepted' | 'duplicate' | 'lost';

/**
 * Takes an agent's report for the job its lease token holds and enqueues the
 * EngineJobResult for apps/worker, atomically. A repeated report with the
 * same token (a retry after a lost response) is a duplicate; a report without
 * a current lease is lost (the job was re-leased or failed meanwhile).
 */
export async function reportEngineJob(
  db: Kysely<Database>,
  jobId: string,
  token: string,
  report: EngineJobReport,
): Promise<ReportOutcome> {
  return db.transaction().execute(async (trx) => {
    const row = await trx
      .updateTable('engine_jobs')
      .set({ reported_at: sql<Date>`now()` })
      .where('id', '=', jobId)
      .where('lease_token_hash', '=', hashToken(token))
      .where('status', '=', 'queued')
      .where('reported_at', 'is', null)
      .returning(['kind', 'leased_by'])
      .executeTakeFirst();
    if (!row) {
      const seen = await trx
        .selectFrom('engine_jobs')
        .select('id')
        .where('id', '=', jobId)
        .where('lease_token_hash', '=', hashToken(token))
        .where('reported_at', 'is not', null)
        .executeTakeFirst();
      return seen ? 'duplicate' : 'lost';
    }
    const agent = row.leased_by ?? 'unknown';
    await enqueueResult(
      trx,
      report.ok
        ? { jobId, kind: row.kind, agent, ok: true, result: report.result }
        : { jobId, kind: row.kind, agent, ok: false, error: report.error },
    );
    return 'accepted';
  });
}

/**
 * Fails jobs whose last lease ran out without a report (the agent died on the
 * final attempt), so whatever waits for them hears about it. Run by the
 * worker's scheduler.
 */
export async function failAbandonedEngineJobs(db: Kysely<Database>): Promise<number> {
  return db.transaction().execute(async (trx) => {
    const rows = await trx
      .updateTable('engine_jobs')
      .set({ reported_at: sql<Date>`now()` })
      .where('status', '=', 'queued')
      .where('reported_at', 'is', null)
      .where(sql<boolean>`attempts >= max_attempts`)
      .where('lease_expires_at', '<', sql<Date>`now()`)
      .returning(['id', 'kind', 'leased_by', 'attempts'])
      .execute();
    for (const row of rows) {
      await enqueueResult(trx, {
        jobId: row.id,
        kind: row.kind,
        agent: row.leased_by ?? 'unknown',
        ok: false,
        error: {
          code: 'internal',
          message: `no result after ${row.attempts} attempts (the engine agent stopped answering)`,
        },
      });
    }
    return rows.length;
  });
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
