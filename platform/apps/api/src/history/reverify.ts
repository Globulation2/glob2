// Operator re-run of a match's verification (POST
// /api/v1/admin/matches/:id/reverify, `platform matches reverify`), for
// matches whose verify job failed or was lost (verification 'failed'), that
// came out unverifiable, or that sit pending with no job in flight. Audited
// like the account actions; a null actor means the server's command line.
import type { Kysely } from 'kysely';
import type { Account, Database } from '@glob2/db';
import { reverifyMatch, type ReverifyOutcome } from '@glob2/worker';
import { apiError } from '../errors.ts';
import { UUID } from './summaries.ts';

const REFUSALS: Record<Exclude<ReverifyOutcome, { ok: true }>['reason'], string> = {
  not_found: 'No such match.',
  not_ended: 'The match has not ended.',
  record_missing: 'The match has no stored record to verify.',
  already_verified: 'The match was verified (or is not checked); there is nothing to re-run.',
  already_rated: 'Ratings were already applied for this match.',
  in_progress: 'A verify job is already queued for this match (use force to replace it).',
};

export async function reverify(
  db: Kysely<Database>,
  actor: Account | undefined,
  matchId: string,
  options: { force?: boolean } = {},
): Promise<{ jobId: string; previous: string }> {
  if (!UUID.test(matchId)) throw apiError('not_found', REFUSALS.not_found);
  const outcome = await reverifyMatch(db, matchId, options);
  if (!outcome.ok) {
    throw apiError(
      outcome.reason === 'not_found' ? 'not_found' : 'conflict',
      REFUSALS[outcome.reason],
      {
        reason: outcome.reason,
      },
    );
  }
  await db
    .insertInto('admin_audit_log')
    .values({
      actor_account_id: actor?.id ?? null,
      action: 'reverify-match',
      target_type: 'match',
      target_id: matchId,
      details: JSON.stringify({
        job: outcome.jobId,
        previous: outcome.previous,
        force: options.force ?? false,
      }),
    })
    .execute();
  return { jobId: outcome.jobId, previous: outcome.previous };
}
