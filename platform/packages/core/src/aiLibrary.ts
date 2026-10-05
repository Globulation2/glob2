import { randomUUID } from 'node:crypto';
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import {
  AI_VALIDATION_SUITE,
  pendingAiReport,
  passedAiReport,
  parseSimVersionKey,
  simVersionKey,
  type AiValidationReport,
  type SimVersion,
} from '@glob2/protocol';
import { submitEngineJob } from './engineJobs.ts';

/** Call inside a transaction. Uniqueness coalesces identical validation work. */
export async function ensureAiValidation(
  db: Kysely<Database>,
  hash: string,
  sim: SimVersion,
  retry = false,
) {
  const simKey = simVersionKey(sim);
  const inserted = await db
    .insertInto('ai_validations')
    .values({
      hash,
      sim_version: simKey,
      suite: AI_VALIDATION_SUITE,
      report: JSON.stringify(pendingAiReport(hash, simKey)),
    })
    .onConflict((oc) => oc.columns(['hash', 'sim_version', 'suite']).doNothing())
    .returning('id')
    .executeTakeFirst();
  const row = await db
    .selectFrom('ai_validations')
    .selectAll()
    .where('hash', '=', hash)
    .where('sim_version', '=', simKey)
    .where('suite', '=', AI_VALIDATION_SUITE)
    .forUpdate()
    .executeTakeFirstOrThrow();
  if (inserted || (retry && row.status === 'error')) {
    const jobId = randomUUID();
    await submitEngineJob(db, {
      jobId,
      kind: 'validate-ai',
      simVersion: sim,
      payload: { blobHash: hash, suite: AI_VALIDATION_SUITE },
    });
    await db
      .updateTable('ai_validations')
      .set({
        job_id: jobId,
        status: 'pending',
        error: null,
        report: JSON.stringify(pendingAiReport(hash, simKey)),
      })
      .where('id', '=', row.id)
      .execute();
  }
  return row.id;
}
export async function applyAiValidation(db: Kysely<Database>, jobId: string) {
  const job = await db
    .selectFrom('engine_jobs')
    .selectAll()
    .where('id', '=', jobId)
    .where('kind', '=', 'validate-ai')
    .executeTakeFirst();
  if (!job) return;
  const row = await db
    .selectFrom('ai_validations')
    .selectAll()
    .where('job_id', '=', jobId)
    .executeTakeFirst();
  if (!row) return;
  const report = job.result as AiValidationReport | null;
  const matches =
    job.status === 'succeeded' &&
    report &&
    report.sourceHash === row.hash &&
    report.simVersion === row.sim_version &&
    report.suite === row.suite;
  if (matches)
    await db
      .updateTable('ai_validations')
      .set({
        status: passedAiReport(report) ? 'valid' : 'invalid',
        report: JSON.stringify(report),
        error: null,
      })
      .where('id', '=', row.id)
      .execute();
  else
    await db
      .updateTable('ai_validations')
      .set({ status: 'error', error: 'Validation could not finish. Please retry.' })
      .where('id', '=', row.id)
      .execute();
}
/** Bounded scheduler work; historical passing reports are never replaced. */
export async function maintainAiLibrary(db: Kysely<Database>) {
  await sql`DELETE FROM ai_uploads WHERE id IN (SELECT id FROM ai_uploads WHERE expires_at < now() LIMIT 1000)`.execute(
    db,
  );
  await sql`DELETE FROM ai_validations v WHERE v.created_at < now() - interval '7 days' AND v.status <> 'pending'
    AND NOT EXISTS (SELECT 1 FROM ai_uploads u WHERE u.validation_id=v.id)
    AND NOT EXISTS (SELECT 1 FROM ai_versions r WHERE r.hash=v.hash)`.execute(db);
  const rows = await sql<{
    hash: string;
    sim_version: string;
  }>`SELECT DISTINCT v.hash, a.sim_version FROM ai_versions v CROSS JOIN engine_agents a
    WHERE a.last_seen_at > now() - interval '2 minutes' AND 'validate-ai' = ANY(a.kinds)
    AND (NOT EXISTS (SELECT 1 FROM ai_validations c WHERE c.hash=v.hash AND c.sim_version=a.sim_version AND c.suite=${AI_VALIDATION_SUITE}) OR EXISTS (SELECT 1 FROM ai_validations c JOIN engine_jobs j ON j.id=c.job_id WHERE c.hash=v.hash AND c.sim_version=a.sim_version AND c.suite=${AI_VALIDATION_SUITE} AND c.status='error' AND j.created_at < now() - interval '1 hour')) LIMIT 20`.execute(
    db,
  );
  for (const row of rows.rows) {
    const sim = parseSimVersionKey(row.sim_version);
    if (sim) await db.transaction().execute((trx) => ensureAiValidation(trx, row.hash, sim, true));
  }
}
