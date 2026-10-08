import type { meteredUsage } from './usage.ts';
export { meteredUsage } from './usage.ts';
import { sql, type Kysely, type Transaction } from 'kysely';
import type { Database } from '@glob2/db';

export interface PaymentFact {
  product: string;
  purchaseId: string;
  providerId: string;
  mode: 'live' | 'test';
  currency: string;
  paid: number;
  refunded: number;
  disputed: boolean;
  occurredAt: Date;
  paymentAt?: Date;
  refunds?: { id: string; amount: number; at: Date }[];
}

/** Persist only metering, even when saving the player-facing result later fails. */
export async function recordAttemptUsage(
  db: Kysely<Database>,
  product: string,
  requestId: string,
  stage: string,
  usage: unknown,
) {
  if (usage === undefined || usage === null) return;
  await sql`UPDATE admin_provider_attempts SET usage=${JSON.stringify(usage)}::jsonb WHERE product=${product} AND request_id=${requestId} AND stage=${stage}`.execute(
    db,
  );
}
/** Call inside the verified canonical provider read's transaction, after purchase validation. */
export async function recordPaymentFact(db: Transaction<Database>, fact: PaymentFact) {
  if (
    !Number.isSafeInteger(fact.paid) ||
    !Number.isSafeInteger(fact.refunded) ||
    fact.paid < 0 ||
    fact.refunded < 0 ||
    fact.refunded > fact.paid ||
    !Number.isFinite(fact.occurredAt.getTime()) ||
    !/^[a-z]{3}$/.test(fact.currency)
  )
    throw new Error('Invalid financial reporting fact.');
  await sql`INSERT INTO admin_payment_totals(product,purchase_id,provider_id,mode,currency,paid_amount) VALUES(${fact.product},${fact.purchaseId},${fact.providerId},${fact.mode},${fact.currency},0) ON CONFLICT DO NOTHING`.execute(
    db,
  );
  const previous = (
    await sql<{
      paid_amount: number | null;
      refunded_amount: number;
      disputed: boolean;
      revision: number;
      mode: string;
      currency: string | null;
    }>`SELECT * FROM admin_payment_totals WHERE product=${fact.product} AND purchase_id=${fact.purchaseId} FOR UPDATE`.execute(
      db,
    )
  ).rows[0];
  if (!previous) throw new Error('Missing financial reporting purchase.');
  if (previous.currency && previous.currency !== fact.currency)
    throw new Error('Purchase currency changed.');
  if (previous.mode !== 'unclassified' && previous.mode !== fact.mode)
    throw new Error('Purchase payment mode changed.');
  const paid = Math.max(previous.paid_amount ?? 0, fact.paid),
    refunded = Math.max(previous.refunded_amount, fact.refunded),
    revision = previous.revision + 1;
  const events: [string, number][] = [];
  if (previous.mode === 'unclassified') {
    // Reclassify verified historical events without manufacturing a second payment.
    await sql`UPDATE admin_financial_events SET mode=${fact.mode} WHERE product=${fact.product} AND purchase_id=${fact.purchaseId} AND mode='unclassified'`.execute(
      db,
    );
  }
  if (paid > (previous.paid_amount ?? 0))
    events.push(['payment', paid - (previous.paid_amount ?? 0)]);
  if (fact.refunds) {
    if (
      fact.refunds.length > 100 ||
      fact.refunds.reduce((sum, r) => sum + r.amount, 0) !== fact.refunded ||
      fact.refunds.some(
        (r) =>
          !Number.isSafeInteger(r.amount) ||
          r.amount < 0 ||
          !r.id ||
          !Number.isFinite(r.at.getTime()),
      )
    )
      throw new Error('Incomplete verified refund facts.');
    for (const refund of fact.refunds)
      await sql`INSERT INTO admin_financial_events(id,product,purchase_id,provider_id,mode,currency,kind,amount,occurred_at) VALUES(${`${fact.product}:${fact.purchaseId}:refund:${refund.id}`},${fact.product},${fact.purchaseId},${refund.id},${fact.mode},${fact.currency},'refund',${refund.amount},${refund.at}) ON CONFLICT DO NOTHING`.execute(
        db,
      );
  } else if (refunded > previous.refunded_amount)
    events.push(['refund', refunded - previous.refunded_amount]);
  if (fact.disputed !== previous.disputed)
    events.push([fact.disputed ? 'dispute' : 'dispute_closed', fact.paid]);
  for (const [kind, amount] of events)
    await sql`INSERT INTO admin_financial_events(id,product,purchase_id,provider_id,mode,currency,kind,amount,occurred_at) VALUES(${`${fact.product}:${fact.purchaseId}:${revision}:${kind}`},${fact.product},${fact.purchaseId},${fact.providerId},${fact.mode},${fact.currency},${kind},${amount},${kind === 'payment' ? (fact.paymentAt ?? fact.occurredAt) : fact.occurredAt}) ON CONFLICT DO NOTHING`.execute(
      db,
    );
  await sql`UPDATE admin_payment_totals SET provider_id=${fact.providerId},mode=${fact.mode},currency=${fact.currency},paid_amount=${paid},refunded_amount=${refunded},disputed=${fact.disputed},revision=${revision} WHERE product=${fact.product} AND purchase_id=${fact.purchaseId}`.execute(
    db,
  );
}

export interface MonetaryRate {
  version: string;
  model: string;
  currency: string;
  effectiveAt: string;
  inputMicros: number;
  cachedInputMicros: number;
  outputMicros: number;
  callMicros: number;
}
export function estimateMicros(
  usage: ReturnType<typeof meteredUsage>,
  rate: MonetaryRate,
): number | undefined {
  if (!usage) return undefined;
  const amount =
    (BigInt(usage.input - usage.cached) * BigInt(rate.inputMicros) +
      BigInt(usage.cached) * BigInt(rate.cachedInputMicros) +
      BigInt(usage.output) * BigInt(rate.outputMicros) +
      999999n) /
      1000000n +
    BigInt(rate.callMicros);
  if (amount > BigInt(Number.MAX_SAFE_INTEGER)) return undefined;
  return Number(amount);
}
