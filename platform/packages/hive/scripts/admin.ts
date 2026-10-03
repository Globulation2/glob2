// Operator-only command. Requires direct database credentials, never served over HTTP.
import { randomUUID } from 'node:crypto';
import { createDatabase } from '@glob2/db';
import { Credits } from '../src/credits.ts';
const [action, account, id, a, b, c, evidence] = process.argv.slice(2);
if (!process.env['DATABASE_URL'] || !account || !id || !['grant', 'settle'].includes(action ?? ''))
  throw new Error(
    'Usage: DATABASE_URL=… node scripts/admin.ts grant ACCOUNT OPERATION_ID CREDITS | settle ACCOUNT CALL_ID INPUT CACHED_INPUT OUTPUT EVIDENCE_REFERENCE',
  );
const database = createDatabase({ connectionString: process.env['DATABASE_URL'] });
try {
  const credits = new Credits(database.db);
  if (action === 'grant') {
    if (process.env['HIVE_DEVELOPMENT_CREDITS'] !== '1')
      throw new Error('Development grants require HIVE_DEVELOPMENT_CREDITS=1.');
    const person = await database.db
      .selectFrom('accounts')
      .select(['kind', 'status'])
      .where('id', '=', account)
      .executeTakeFirstOrThrow();
    if (person.kind !== 'registered' || person.status !== 'active')
      throw new Error('Only active registered accounts can receive access.');
    await credits.adjust(account, id, Number(a), 'grant', { development: true });
    const exists = await database.db
      .selectFrom('entitlements')
      .select('id')
      .where('account_id', '=', account)
      .where('entitlement', '=', 'hive-mind')
      .where('revoked_at', 'is', null)
      .executeTakeFirst();
    if (!exists)
      await database.db
        .insertInto('entitlements')
        .values({
          id: randomUUID(),
          account_id: account,
          entitlement: 'hive-mind',
          source: 'development-grant',
        })
        .execute();
  } else {
    if (!evidence)
      throw new Error(
        'Reconciliation requires an evidence reference. Use zero usage only with evidence no usage was billed.',
      );
    await credits.settle(account, id, {
      input: Number(a),
      cachedInput: Number(b),
      cacheWrite: Number(process.env['HIVE_RECONCILE_CACHE_WRITE_TOKENS'] ?? 0),
      output: Number(c),
    });
    await credits.adjust(account, `reconciled:${id}`, 0, 'adjustment', { evidence });
  }
  console.log(JSON.stringify(await credits.balance(account)));
} finally {
  await database.close();
}
