// Age-based restriction of social features. An account's age band is
// 'unknown' until the player answers the one-time age question or a platform
// age signal (Apple Declared Age Range, Google Play Age Signals) reports it.
// Social features need '13_plus'; 'unknown' is treated like 'under_13'.
// See docs/multiplayer/identity.md#age-bands.
import { sql } from 'kysely';
import type { Kysely } from 'kysely';
import type { Account, AgeBand, AgeBandSource, Database } from '@glob2/db';
import { apiError } from '../errors.ts';

type Db = Kysely<Database>;

/** `details.reason` of every refusal made because of the account's age band. */
export const AGE_RESTRICTED = 'age_restricted';

/** The tables whose rows an owner shares by `visibility`, made private for under-13 accounts. */
const SHARED_CONTENT = [
  { table: 'maps', owner: 'owner_account_id' },
  { table: 'ais', owner: 'owner_account_id' },
  { table: 'generators', owner: 'owner_account_id' },
  { table: 'asset_sets', owner: 'owner_account_id' },
  { table: 'building_families', owner: 'owner_account_id' },
] as const;

/** Whether an account may use chat and share content: only a 13_plus band does. */
export function socialFeatures(account: Pick<Account, 'age_band'>): boolean {
  return account.age_band === '13_plus';
}

/** Throws forbidden (details.reason "age_restricted") unless the account has social features. */
export function requireSocialFeatures(
  account: Pick<Account, 'age_band'>,
  message = 'This needs a player aged 13 or over.',
): void {
  if (!socialFeatures(account)) {
    throw apiError('forbidden', message, { reason: AGE_RESTRICTED, ageBand: account.age_band });
  }
}

/** Sharing beyond `private` needs social features. */
export function requireShareable(account: Pick<Account, 'age_band'>, visibility: string): void {
  if (visibility !== 'private') {
    requireSocialFeatures(account, 'Players under 13 can keep content private but not share it.');
  }
}

/** Reads the band fresh: it may have changed after the socket or token was issued. */
export async function currentAgeBand(db: Db, accountId: string): Promise<AgeBand> {
  const row = await db
    .selectFrom('accounts')
    .select('age_band')
    .where('id', '=', accountId)
    .executeTakeFirst();
  return row?.age_band ?? 'unknown';
}

/** Whether every one of these accounts exists and has social features (one query). */
export async function allHaveSocialFeatures(
  db: Db,
  accountIds: readonly string[],
): Promise<boolean> {
  const ids = [...new Set(accountIds)];
  if (ids.length === 0) return true;
  const row = await db
    .selectFrom('accounts')
    .select((eb) => eb.fn.countAll<string>().as('n'))
    .where('id', 'in', ids)
    .where('age_band', '=', '13_plus')
    .executeTakeFirstOrThrow();
  return Number(row.n) === ids.length;
}

/**
 * Whether `source` may move an account from `from` (set by `fromSource`) to
 * `to`. Moving to under_13 is always allowed. Leaving under_13 needs a
 * moderator, or the same platform signal that set it (a child who turns 13 on
 * a parent-managed Apple or Google account); a self-declared under_13 stays,
 * so a child cannot simply answer again.
 */
export function mayChangeAgeBand(
  from: AgeBand,
  fromSource: AgeBandSource | null,
  to: Exclude<AgeBand, 'unknown'>,
  source: AgeBandSource,
): boolean {
  if (to === 'under_13' || from !== 'under_13') return true;
  if (source === 'moderator') return true;
  return source !== 'self' && source === fromSource;
}

/**
 * Sets the account's age band. Returns the updated account. Moving to
 * under_13 also makes the account's shared content private, in the same
 * transaction.
 */
export async function setAgeBand(
  db: Db,
  accountId: string,
  band: Exclude<AgeBand, 'unknown'>,
  source: AgeBandSource,
): Promise<Account> {
  return db.transaction().execute(async (trx) => {
    const current = await trx
      .selectFrom('accounts')
      .selectAll()
      .where('id', '=', accountId)
      .forUpdate()
      .executeTakeFirst();
    if (!current) throw apiError('not_found', 'No such account.');
    if (!mayChangeAgeBand(current.age_band, current.age_band_source, band, source)) {
      throw apiError(
        'forbidden',
        'This account is set as under 13. A parent can ask us to change it.',
        { reason: 'age_band_locked', ageBand: current.age_band },
      );
    }
    if (current.age_band === band && current.age_band_source === source) return current;
    // Under 13 again: keep who set it. A relayed platform claim must not turn a
    // self-declared or moderator lock into one the same claim could lift.
    if (current.age_band === 'under_13' && band === 'under_13') return current;
    const updated = await trx
      .updateTable('accounts')
      .set({
        age_band: band,
        age_band_source: source,
        age_band_at: sql<Date>`now()`,
        updated_at: sql<Date>`now()`,
      })
      .where('id', '=', accountId)
      .returningAll()
      .executeTakeFirstOrThrow();
    if (band === 'under_13') {
      // No chosen picture, and nothing shared beyond the owner.
      await trx
        .updateTable('accounts')
        .set({ avatar_source: 'initials' })
        .where('id', '=', accountId)
        .where('avatar_source', '!=', 'initials')
        .execute();
      await trx
        .updateTable('music_releases')
        .set({ status: 'ready', updated_at: sql<Date>`now()` })
        .where('owner_id', '=', accountId)
        .where('status', '=', 'published')
        .execute();
      for (const { table, owner } of SHARED_CONTENT) {
        await sql`UPDATE ${sql.table(table)} SET visibility = 'private'
                  WHERE ${sql.ref(owner)} = ${accountId} AND visibility <> 'private'`.execute(trx);
      }
    }
    return band === 'under_13' ? { ...updated, avatar_source: 'initials' } : updated;
  });
}
