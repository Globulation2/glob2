// Disposable reporting fixtures expose missing-day chart gaps, payment units,
// and a recovery request without contacting providers or charging anyone.
import { randomUUID } from 'node:crypto';
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import { Credits, recordPaymentFact } from '@glob2/billing';
import type { SeededHistory } from '../../api/test/historySeed.ts';

export async function seedAdminReports(db: Kysely<Database>, seed: SeededHistory) {
  await sql`INSERT INTO admin_daily_metrics(day,metric,dimension,value) VALUES
    ((now() AT TIME ZONE 'UTC')::date-9,'activity','registered',7),
    ((now() AT TIME ZONE 'UTC')::date-8,'activity','registered',5),
    ((now() AT TIME ZONE 'UTC')::date-4,'activity','registered',3),
    ((now() AT TIME ZONE 'UTC')::date-9,'activity','guest',2)
    ON CONFLICT(day,metric,dimension) DO UPDATE SET value=excluded.value`.execute(db);
  const credits = new Credits(db),
    call = randomUUID();
  await credits.adjust(seed.accounts.bradley, 'browser-admin-recovery', 50, 'grant');
  await credits.reserve(seed.accounts.bradley, call, 12, {
    version: 'browser-fixture',
    model: 'browser-example',
    input: 1000000,
    cachedInput: 0,
    output: 1000000,
  });
  await credits.dispatch(call);
  await credits.uncertain(call);
  await db
    .updateTable('hive_calls')
    .set({ usage: JSON.stringify({ input: 5, cachedInput: 2, output: 1 }) })
    .where('id', '=', call)
    .execute();
  await db.transaction().execute(async (tx) => {
    await recordPaymentFact(tx, {
      product: 'maps',
      purchaseId: 'browser-payment-usd',
      providerId: 'browser-provider-usd',
      mode: 'live',
      currency: 'usd',
      paid: 12345,
      refunded: 1000,
      disputed: false,
      occurredAt: new Date(),
      refunds: [{ id: 'browser-refund-usd', amount: 1000, at: new Date() }],
    });
    await recordPaymentFact(tx, {
      product: 'music',
      purchaseId: 'browser-payment-jpy',
      providerId: 'browser-provider-jpy',
      mode: 'live',
      currency: 'jpy',
      paid: 500,
      refunded: 0,
      disputed: false,
      occurredAt: new Date(),
    });
  });
}
