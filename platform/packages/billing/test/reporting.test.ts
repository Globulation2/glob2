import { afterAll, beforeAll, expect, it } from 'vitest';
import { createTestDatabase, type TestDatabase } from '../../db/test/support.ts';
import {
  recordPaymentFact,
  meteredUsage,
  estimateMicros,
  type PaymentFact,
} from '../src/reporting.ts';
let db: TestDatabase;
beforeAll(async () => {
  db = await createTestDatabase();
});
afterAll(async () => {
  await db?.drop();
});
const fact: PaymentFact = {
  product: 'maps',
  purchaseId: 'purchase-1',
  providerId: 'pi_test',
  mode: 'test',
  currency: 'usd',
  paid: 1000,
  refunded: 0,
  disputed: false,
  occurredAt: new Date('2026-10-08T12:00:00Z'),
};
const record = (f: PaymentFact) => db.db.transaction().execute((trx) => recordPaymentFact(trx, f));
it('journals duplicate payments once and monotonic partial refunds', async () => {
  await Promise.all([record(fact), record(fact)]);
  await record({ ...fact, refunded: 250 });
  await record({ ...fact, refunded: 250 });
  await record({ ...fact, refunded: 100 });
  await record({ ...fact, refunded: 400 });
  const rows = await db.db.selectFrom('admin_financial_events').selectAll().execute();
  expect(rows.filter((r) => r.kind === 'payment')).toHaveLength(1);
  expect(rows.filter((r) => r.kind === 'refund').reduce((n, r) => n + r.amount, 0)).toBe(400);
});
it('keeps disputes separate from refunds and handles a dispute win', async () => {
  await record({ ...fact, refunded: 400, disputed: true });
  await record({ ...fact, refunded: 400, disputed: false });
  const rows = await db.db.selectFrom('admin_financial_events').selectAll().execute();
  expect(rows.filter((r) => r.kind === 'refund').reduce((n, r) => n + r.amount, 0)).toBe(400);
  expect(rows.filter((r) => r.kind === 'dispute')).toHaveLength(1);
  expect(rows.filter((r) => r.kind === 'dispute_closed')).toHaveLength(1);
  await record({ ...fact, purchaseId: 'purchase-cad', mode: 'live', currency: 'cad' });
  expect(
    await db.db
      .selectFrom('admin_financial_events')
      .selectAll()
      .where('currency', '=', 'cad')
      .where('mode', '=', 'live')
      .execute(),
  ).toHaveLength(1);
});
it('estimates only measured usage using monetary rates', () => {
  const rate = {
    version: 'v1',
    model: 'example',
    currency: 'usd',
    effectiveAt: '2026-10-08T00:00:00Z',
    inputMicros: 1000000,
    cachedInputMicros: 500000,
    outputMicros: 2000000,
    callMicros: 0,
  };
  expect(estimateMicros(meteredUsage({ input: 100, cachedInput: 20, output: 10 }), rate)).toBe(110);
  expect(estimateMicros(meteredUsage(null), rate)).toBeUndefined();
  expect(meteredUsage({ input: 10, cachedInput: 20, output: 0 })).toBeUndefined();
});

it('uses provider refund identities and timestamps across duplicate and out-of-order delivery', async () => {
  const f = {
    ...fact,
    purchaseId: 'refund-events',
    refunded: 250,
    refunds: [{ id: 're_one', amount: 250, at: new Date('2026-10-07T12:00:00Z') }],
  };
  await record(f);
  await record(f);
  await record({
    ...f,
    refunded: 400,
    refunds: [...f.refunds, { id: 're_two', amount: 150, at: new Date('2026-10-08T12:00:00Z') }],
  });
  await record(f);
  const events = await db.db
    .selectFrom('admin_financial_events')
    .selectAll()
    .where('purchase_id', '=', 'refund-events')
    .where('kind', '=', 'refund')
    .orderBy('occurred_at')
    .execute();
  expect(events.map((e) => [e.provider_id, e.amount, e.occurred_at.toISOString()])).toEqual([
    ['re_one', 250, '2026-10-07T12:00:00.000Z'],
    ['re_two', 150, '2026-10-08T12:00:00.000Z'],
  ]);
});
