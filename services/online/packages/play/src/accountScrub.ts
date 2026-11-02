// Names of deleted accounts in match records the platform keeps. A deleted
// player's seats in stored MatchSetups (matches.setup, and the copy in its
// verify-match job) become "Deleted player"; ids stay, so history, ratings
// and verification still line up. A match still running or awaiting its
// verdict is scrubbed once it is settled (the verifier replays the setup as
// the relay recorded it), from account_name_scrubs by the scheduler.
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';

type Db = Kysely<Database>;

/** Matches older than this are scrubbed even if their verdict never came. */
export const NAME_SCRUB_GRACE_DAYS = 7;

/** True for a match whose setup nothing will replay any more. */
const settled = sql<boolean>`(m.status IN ('ended', 'cancelled') AND m.verification <> 'pending')
  OR m.created_at < now() - make_interval(days => ${NAME_SCRUB_GRACE_DAYS})`;

/**
 * Scrubs the account's seats in the given matches where they are settled and
 * queues the rest. Returns how many were scrubbed now.
 */
export async function scrubMatchNames(db: Db, accountId: string, matchIds: string[]) {
  if (matchIds.length === 0) return 0;
  const scrubbed = await sql<{ id: string }>`
    UPDATE matches m SET setup = scrub_match_setup(m.setup, ${accountId})
    WHERE m.id = ANY(${matchIds}::uuid[]) AND (${settled})
    RETURNING m.id`.execute(db);
  const done = scrubbed.rows.map((r) => r.id);
  if (done.length > 0) {
    await sql`
      UPDATE engine_jobs SET payload = jsonb_set(payload, '{setup}',
        scrub_match_setup(payload -> 'setup', ${accountId}))
      WHERE kind = 'verify-match' AND status <> 'queued'
        AND payload ->> 'matchId' = ANY(${done}::text[])`.execute(db);
  }
  const later = matchIds.filter((id) => !done.includes(id));
  if (later.length > 0) {
    await db
      .insertInto('account_name_scrubs')
      .values(later.map((match_id) => ({ account_id: accountId, match_id })))
      .onConflict((oc) => oc.doNothing())
      .execute();
  }
  return done.length;
}

/** Scrubs queued matches that have settled since. Run by the scheduler leader. */
export async function scrubSettledMatchNames(db: Db): Promise<number> {
  const due = await sql<{ account_id: string; match_id: string }>`
    SELECT s.account_id, s.match_id FROM account_name_scrubs s
    JOIN matches m ON m.id = s.match_id
    WHERE ${settled}
    LIMIT 500`.execute(db);
  let count = 0;
  for (const row of due.rows) {
    await db.transaction().execute(async (trx) => {
      count += await scrubMatchNames(trx, row.account_id, [row.match_id]);
      await trx
        .deleteFrom('account_name_scrubs')
        .where('account_id', '=', row.account_id)
        .where('match_id', '=', row.match_id)
        .execute();
    });
  }
  return count;
}
