import { sql, type Kysely } from 'kysely';
import type { Account, Database } from '@glob2/db';

/** Replica cache bounds writes; database uniqueness is authoritative across replicas. */
export class AccountActivity {
  private readonly seen = new Map<string, string>();
  private readonly db: Kysely<Database>;
  private readonly enabled: boolean;
  constructor(db: Kysely<Database>, enabled = true) {
    this.db = db;
    this.enabled = enabled;
  }
  async record(account: Pick<Account, 'id' | 'kind' | 'status'>, now = new Date()) {
    if (!this.enabled || account.status !== 'active') return;
    const day = now.toISOString().slice(0, 10),
      key = account.id + ':' + account.kind;
    if (this.seen.get(key) === day) return;
    await sql`WITH active AS (SELECT id,kind FROM accounts WHERE id=${account.id}::uuid AND status='active' AND (SELECT collection FROM admin_analytics_settings WHERE id) FOR SHARE) INSERT INTO account_activity_days(account_id,day,kind) SELECT id,${day}::date,kind FROM active WHERE true ON CONFLICT(account_id,day) DO UPDATE SET kind=excluded.kind WHERE account_activity_days.kind IS DISTINCT FROM excluded.kind`.execute(
      this.db,
    );
    if (this.seen.size >= 10000) {
      const oldest = this.seen.keys().next().value;
      if (oldest) this.seen.delete(oldest);
    }
    this.seen.set(key, day);
  }
}

export async function retainAnalytics(db: Kysely<Database>, now = new Date()) {
  await sql`WITH old AS (SELECT account_id,day FROM account_activity_days WHERE day < (${now}::timestamptz AT TIME ZONE 'UTC')::date - 89 ORDER BY day LIMIT 5000) DELETE FROM account_activity_days a USING old WHERE a.account_id=old.account_id AND a.day=old.day`.execute(
    db,
  );
  await sql`WITH old AS (SELECT day,metric,dimension FROM admin_daily_metrics WHERE day < ((${now}::timestamptz AT TIME ZONE 'UTC')::date - interval '24 months')::date ORDER BY day LIMIT 5000) DELETE FROM admin_daily_metrics a USING old WHERE (a.day,a.metric,a.dimension)=(old.day,old.metric,old.dimension)`.execute(
    db,
  );
}
