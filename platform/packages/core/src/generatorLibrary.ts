import { isDeepStrictEqual } from 'node:util';
import { createHash, randomUUID } from 'node:crypto';
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import {
  GENERATOR_VALIDATION_SUITE,
  pendingGeneratorReport,
  passedGeneratorReport,
  parseSimVersionKey,
  simVersionKey,
  type GeneratorValidationReport,
  type SimVersion,
  type GeneratorSettings,
} from '@glob2/protocol';
import { submitEngineJob } from './engineJobs.ts';

/** Call inside a transaction. Uniqueness coalesces identical validation work. */
export async function ensureGeneratorValidation(
  db: Kysely<Database>,
  hash: string,
  sim: SimVersion,
  example: GeneratorSettings,
  retry = false,
) {
  const simKey = simVersionKey(sim);
  const requestHash = createHash('sha256')
    .update(
      JSON.stringify({
        seed: example.seed,
        candidates: example.candidates,
        startingUnitLevel: example.startingUnitLevel,
        params: Object.fromEntries(Object.entries(example.params).sort()),
      }),
    )
    .digest('hex');
  const inserted = await db
    .insertInto('generator_validations')
    .values({
      hash,
      sim_version: simKey,
      suite: GENERATOR_VALIDATION_SUITE,
      request_hash: requestHash,
      example: JSON.stringify(example),
      report: JSON.stringify(pendingGeneratorReport(hash, simKey)),
    })
    .onConflict((oc) => oc.columns(['hash', 'sim_version', 'suite', 'request_hash']).doNothing())
    .returning('id')
    .executeTakeFirst();
  const row = await db
    .selectFrom('generator_validations')
    .selectAll()
    .where('hash', '=', hash)
    .where('sim_version', '=', simKey)
    .where('suite', '=', GENERATOR_VALIDATION_SUITE)
    .where('request_hash', '=', requestHash)
    .forUpdate()
    .executeTakeFirstOrThrow();
  if (inserted || (retry && row.status === 'error')) {
    const jobId = randomUUID();
    await submitEngineJob(db, {
      jobId,
      kind: 'validate-generator',
      simVersion: sim,
      payload: { blobHash: hash, suite: GENERATOR_VALIDATION_SUITE, example },
    });
    await db
      .updateTable('generator_validations')
      .set({
        job_id: jobId,
        status: 'pending',
        error: null,
        report: JSON.stringify(pendingGeneratorReport(hash, simKey)),
      })
      .where('id', '=', row.id)
      .execute();
  }
  return row.id;
}
export async function applyGeneratorValidation(db: Kysely<Database>, jobId: string) {
  const job = await db
    .selectFrom('engine_jobs')
    .selectAll()
    .where('id', '=', jobId)
    .where('kind', '=', 'validate-generator')
    .executeTakeFirst();
  if (!job) return;
  const row = await db
    .selectFrom('generator_validations')
    .selectAll()
    .where('job_id', '=', jobId)
    .executeTakeFirst();
  if (!row) return;
  const report = job.result as GeneratorValidationReport | null;
  const matches =
    job.status === 'succeeded' &&
    report &&
    report.sourceHash === row.hash &&
    report.simVersion === row.sim_version &&
    report.suite === row.suite &&
    (!report.valid || isDeepStrictEqual(report.samples[0]?.settings, row.example));
  if (matches)
    await db
      .updateTable('generator_validations')
      .set({
        status: passedGeneratorReport(report) ? 'valid' : 'invalid',
        report: JSON.stringify(report),
        error: null,
      })
      .where('id', '=', row.id)
      .execute();
  else
    await db
      .updateTable('generator_validations')
      .set({ status: 'error', error: 'Validation could not finish. Please retry.' })
      .where('id', '=', row.id)
      .execute();
}
/** Collect staging-only evidence without interrupting running validation jobs. */
async function collectAbandonedGeneratorValidations(db: Kysely<Database>) {
  await db.transaction().execute(async (trx) => {
    const abandoned = await trx
      .selectFrom('generator_validations as v')
      .select(['v.id', 'v.job_id'])
      .where('v.created_at', '<', sql<Date>`now() - interval '7 days'`)
      .where(
        sql<boolean>`NOT EXISTS (SELECT 1 FROM generator_uploads u WHERE u.validation_id=v.id)`,
      )
      .where(
        sql<boolean>`NOT EXISTS (SELECT 1 FROM generator_versions r WHERE r.source_hash=v.hash)`,
      )
      .orderBy('v.created_at')
      .limit(1000)
      .forUpdate()
      .skipLocked()
      .execute();
    for (const validation of abandoned) {
      let queuedJob: string | undefined;
      if (validation.job_id) {
        // The result worker locks jobs before validations. Skip its lock instead
        // of waiting in the opposite order, and never interrupt a live lease.
        const job = await trx
          .selectFrom('engine_jobs')
          .select(['id', 'status', 'reported_at', 'lease_expires_at'])
          .where('id', '=', validation.job_id)
          .forUpdate()
          .skipLocked()
          .executeTakeFirst();
        if (!job) continue;
        if (job.status === 'queued') {
          if (job.reported_at || (job.lease_expires_at && job.lease_expires_at > new Date()))
            continue;
          queuedJob = job.id;
        }
      }
      await trx.deleteFrom('generator_validations').where('id', '=', validation.id).execute();
      // An agent can disappear before its first lease; those jobs never reach
      // the normal exhausted-attempt cleanup and would retain the blob forever.
      if (queuedJob) await trx.deleteFrom('engine_jobs').where('id', '=', queuedJob).execute();
    }
  });
}

/** Bounded scheduler work; historical passing reports are never replaced. */
export async function maintainGeneratorLibrary(db: Kysely<Database>) {
  await sql`DELETE FROM generator_uploads WHERE id IN (SELECT id FROM generator_uploads WHERE expires_at < now() AND NOT EXISTS (SELECT 1 FROM generator_studio_checks c WHERE c.upload_id=generator_uploads.id) LIMIT 1000)`.execute(
    db,
  );
  await collectAbandonedGeneratorValidations(db);
  const rows = await sql<{
    hash: string;
    example: GeneratorSettings;
    sim_version: string;
  }>`
    SELECT DISTINCT v.source_hash AS hash, v.example, a.sim_version
    FROM generator_versions v JOIN generators g ON g.id=v.generator_id CROSS JOIN engine_agents a
    WHERE g.deleted_at IS NULL AND a.last_seen_at > now() - interval '2 minutes'
      AND 'validate-generator' = ANY(a.kinds)
      AND (
        NOT EXISTS (
          SELECT 1 FROM generator_validations c
          WHERE c.hash = v.source_hash AND c.example=v.example AND c.sim_version = a.sim_version
            AND c.suite = ${GENERATOR_VALIDATION_SUITE}
        ) OR EXISTS (
          SELECT 1 FROM generator_validations c
          LEFT JOIN engine_jobs j ON j.id = c.job_id
          WHERE c.hash = v.source_hash AND c.example=v.example AND c.sim_version = a.sim_version
            AND c.suite = ${GENERATOR_VALIDATION_SUITE} AND c.status = 'error'
            AND (j.id IS NULL OR j.created_at < now() - interval '1 hour')
        )
      )
    LIMIT 20`.execute(db);
  for (const row of rows.rows) {
    const sim = parseSimVersionKey(row.sim_version);
    if (sim)
      await db
        .transaction()
        .execute((trx) => ensureGeneratorValidation(trx, row.hash, sim, row.example, true));
  }
}
