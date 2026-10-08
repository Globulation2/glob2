import type { Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import { parse, ValidateSetResult, SET_VALIDATION_SUITE } from '@glob2/protocol';

/** Apply only to the draft revision that still owns this exact validation job. */
export async function applySetJobResult(db: Kysely<Database>, jobId: string) {
  const job = await db
    .selectFrom('engine_jobs')
    .selectAll()
    .where('id', '=', jobId)
    .where('kind', '=', 'validate-set')
    .executeTakeFirst();
  if (!job) return;
  const draft = await db
    .selectFrom('set_drafts')
    .selectAll()
    .where('validation_job_id', '=', jobId)
    .forUpdate()
    .executeTakeFirst();
  if (!draft || draft.status !== 'pending') return;
  if (job.status === 'succeeded') {
    const report = parse(ValidateSetResult, job.result, 'set validation');
    if (
      report.hash !== draft.hash ||
      report.suite !== SET_VALIDATION_SUITE ||
      job.sim_version !== draft.sim_version
    )
      throw Error('Set validation does not match the draft');
    await db
      .updateTable('set_drafts')
      .set({
        report: JSON.stringify(report),
        status: report.valid ? 'valid' : 'invalid',
        error: null,
      })
      .where('id', '=', draft.id)
      .execute();
  } else if (job.status === 'failed') {
    await db
      .updateTable('set_drafts')
      .set({ status: 'error', error: 'Validation could not finish. Retry checks.' })
      .where('id', '=', draft.id)
      .execute();
  }
}
