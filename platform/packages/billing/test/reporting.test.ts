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
  const written = meteredUsage({ input: 10, cachedInput: 2, cacheWrite: 3, output: 1 });
  expect(written).toEqual({ input: 10, cached: 2, cacheWrite: 3, output: 1 });
  expect(estimateMicros(written, rate)).toBeUndefined();
  expect(meteredUsage({ input: 10, cachedInput: 9, cacheWrite: 3, output: 1 })).toBeUndefined();
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

it('replaces covered refund snapshots with provider facts without losing newer totals', async () => {
  const f = { ...fact, purchaseId: 'mixed-refunds', refunded: 400 };
  await record(f);
  const one = { id: 're_partial', amount: 250, at: new Date('2026-10-07T12:00:00Z') };
  await record({ ...f, refunded: 250, refunds: [one] });
  const total = async () =>
    (
      await db.db
        .selectFrom('admin_financial_events')
        .select('amount')
        .where('purchase_id', '=', f.purchaseId)
        .where('kind', '=', 'refund')
        .execute()
    ).reduce((n, r) => n + r.amount, 0);
  expect(await total()).toBe(400);
  const two = { id: 're_remaining', amount: 150, at: new Date('2026-10-08T12:00:00Z') };
  await Promise.all([record({ ...f, refunds: [one, two] }), record({ ...f, refunds: [one, two] })]);
  await record({ ...f, refunded: 250, refunds: [one] });
  expect(await total()).toBe(400);
  expect(
    (
      await db.db
        .selectFrom('admin_financial_events')
        .select('provider_id')
        .where('purchase_id', '=', f.purchaseId)
        .where('kind', '=', 'refund')
        .execute()
    )
      .map((r) => r.provider_id)
      .sort(),
  ).toEqual(['re_partial', 're_remaining']);
  await expect(
    record({
      ...f,
      refunds: [
        { ...one, amount: 251 },
        { ...two, amount: 149 },
      ],
    }),
  ).rejects.toThrow('monetary fact changed');
});

it('uses actual partial dispute amounts and identities after legacy snapshots', async () => {
  const f = { ...fact, purchaseId: 'partial-dispute', disputed: true };
  await record(f);
  const d = { id: 'dp_partial', amount: 350, at: new Date('2026-10-07T12:00:00Z'), active: true };
  await record({ ...f, disputes: [d] });
  await record({ ...f, disputed: false, disputes: [{ ...d, active: false }] });
  await record({ ...f, disputed: false, disputes: [{ ...d, active: false }] });
  const events = await db.db
    .selectFrom('admin_financial_events')
    .select(['kind', 'amount', 'provider_id'])
    .where('purchase_id', '=', f.purchaseId)
    .where('kind', 'in', ['dispute', 'dispute_closed'])
    .execute();
  expect(events).toHaveLength(2);
  expect(events.every((e) => e.amount === 350 && e.provider_id === 'dp_partial')).toBe(true);
});

it('replaces observed dispute closure time with a verified provider closure event', async () => {
  const f = {
    ...fact,
    purchaseId: 'dispute-closure',
    disputed: false,
    occurredAt: new Date('2026-10-10T12:00:00Z'),
  };
  const d = { id: 'dp_closed', amount: 250, at: new Date('2026-10-07T12:00:00Z'), active: false };
  await record({ ...f, disputes: [d] });
  await record({ ...f, disputes: [{ ...d, closedAt: new Date('2026-10-09T12:00:00Z') }] });
  const closure = await db.db
    .selectFrom('admin_financial_events')
    .select('occurred_at')
    .where('purchase_id', '=', f.purchaseId)
    .where('kind', '=', 'dispute_closed')
    .executeTakeFirstOrThrow();
  expect(closure.occurred_at.toISOString()).toBe('2026-10-09T12:00:00.000Z');
});
