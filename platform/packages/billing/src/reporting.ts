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
  refunds?: ProviderMoneyFact[];
  disputes?: ProviderDisputeFact[];
}

export interface ProviderMoneyFact {
  id: string;
  amount: number;
  at: Date;
}

export interface ProviderDisputeFact extends ProviderMoneyFact {
  active: boolean;
  /** Verified closure event time; absent when only current state is available. */
  closedAt?: Date;
}

function validProviderFacts(facts: ProviderMoneyFact[], paid: number): boolean {
  return (
    facts.length <= 100 &&
    new Set(facts.map((f) => f.id)).size === facts.length &&
    facts.every(
      (f) =>
        !!f.id &&
        f.id.length <= 200 &&
        !f.id.includes('\0') &&
        Number.isSafeInteger(f.amount) &&
        f.amount >= 0 &&
        f.amount <= paid &&
        Number.isFinite(f.at.getTime()),
    )
  );
}

async function insertEvent(
  db: Transaction<Database>,
  fact: PaymentFact,
  event: { id: string; providerId: string; kind: string; amount: number; at: Date },
): Promise<boolean> {
  const row = await sql`INSERT INTO admin_financial_events
    (id,product,purchase_id,provider_id,mode,currency,kind,amount,occurred_at)
    VALUES(${event.id},${fact.product},${fact.purchaseId},${event.providerId},${fact.mode},
      ${fact.currency},${event.kind},${event.amount},${event.at})
    ON CONFLICT DO NOTHING RETURNING id`.execute(db);
  if (row.rows.length) return true;
  const existing = await db
    .selectFrom('admin_financial_events')
    .select(['amount', 'currency', 'kind'])
    .where('id', '=', event.id)
    .executeTakeFirstOrThrow();
  if (
    existing.amount !== event.amount ||
    existing.currency !== fact.currency ||
    existing.kind !== event.kind
  )
    throw new Error('Verified provider monetary fact changed.');
  return false;
}

/** Replace only the covered portion of snapshot refunds when provider IDs arrive.
 * Older lists must leave the still-unidentified cumulative refund amount intact. */
async function recordRefunds(db: Transaction<Database>, fact: PaymentFact) {
  const prefix = `${fact.product}:${fact.purchaseId}:refund:`;
  let identified = 0;
  for (const refund of fact.refunds ?? []) {
    if (
      await insertEvent(db, fact, {
        id: prefix + refund.id,
        providerId: refund.id,
        kind: 'refund',
        amount: refund.amount,
        at: refund.at,
      })
    )
      identified += refund.amount;
  }
  if (!identified) return;
  await sql`WITH snapshots AS (
    SELECT id,coalesce(sum(amount) OVER (ORDER BY occurred_at,id
      ROWS BETWEEN UNBOUNDED PRECEDING AND 1 PRECEDING),0) AS covered
    FROM admin_financial_events WHERE product=${fact.product} AND purchase_id=${fact.purchaseId}
      AND kind='refund' AND left(id,length(${prefix}))<>${prefix}
  ) UPDATE admin_financial_events e SET amount=greatest(0,e.amount-greatest(0,${identified}-s.covered))
    FROM snapshots s WHERE e.id=s.id`.execute(db);
  await sql`DELETE FROM admin_financial_events WHERE product=${fact.product}
    AND purchase_id=${fact.purchaseId} AND kind='refund' AND amount=0
    AND left(id,length(${prefix}))<>${prefix}`.execute(db);
}

async function recordDisputes(db: Transaction<Database>, fact: PaymentFact) {
  const prefix = `${fact.product}:${fact.purchaseId}:dispute:`;
  for (const dispute of fact.disputes ?? []) {
    const id = prefix + dispute.id;
    // A closed dispute is still evidence that the actual disputed amount existed.
    await insertEvent(db, fact, {
      id,
      providerId: dispute.id,
      kind: 'dispute',
      amount: dispute.amount,
      at: dispute.at,
    });
    if (!dispute.active) {
      await insertEvent(db, fact, {
        id: id + ':closed',
        providerId: dispute.id,
        kind: 'dispute_closed',
        amount: dispute.amount,
        at: dispute.closedAt ?? fact.occurredAt,
      });
      if (dispute.closedAt)
        await db
          .updateTable('admin_financial_events')
          .set({ occurred_at: dispute.closedAt })
          .where('id', '=', id + ':closed')
          .execute();
    }
  }
  if (fact.disputes?.length) {
    // The canonical list replaces legacy full-purchase dispute estimates with
    // the provider's actual partial amounts, without adding another dispute.
    await sql`DELETE FROM admin_financial_events WHERE product=${fact.product}
      AND purchase_id=${fact.purchaseId} AND kind IN ('dispute','dispute_closed')
      AND left(id,length(${prefix}))<>${prefix}`.execute(db);
  }
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
    (fact.paymentAt !== undefined && !Number.isFinite(fact.paymentAt.getTime())) ||
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
      !validProviderFacts(fact.refunds, fact.paid) ||
      fact.refunds.reduce((sum, r) => sum + r.amount, 0) !== fact.refunded
    )
      throw new Error('Incomplete verified refund facts.');
    await recordRefunds(db, fact);
  } else if (refunded > previous.refunded_amount) {
    events.push(['refund', refunded - previous.refunded_amount]);
  }
  if (fact.disputes) {
    if (
      !validProviderFacts(fact.disputes, fact.paid) ||
      fact.disputes.some(
        (d) => d.closedAt !== undefined && !Number.isFinite(d.closedAt.getTime()),
      ) ||
      fact.disputes.some((d) => d.active) !== fact.disputed
    )
      throw new Error('Incomplete verified dispute facts.');
    await recordDisputes(db, fact);
  } else if (fact.disputed !== previous.disputed) {
    // Compatibility for callers without itemized provider dispute facts.
    events.push([fact.disputed ? 'dispute' : 'dispute_closed', fact.paid]);
  }
  for (const [kind, amount] of events)
    await insertEvent(db, fact, {
      id: `${fact.product}:${fact.purchaseId}:${revision}:${kind}`,
      providerId: fact.providerId,
      kind,
      amount,
      at: kind === 'payment' ? (fact.paymentAt ?? fact.occurredAt) : fact.occurredAt,
    });
  await sql`UPDATE admin_payment_totals SET provider_id=${fact.providerId},mode=${fact.mode},currency=${fact.currency},paid_amount=${paid},refunded_amount=${refunded},disputed=${fact.disputed},revision=${revision},historical=false WHERE product=${fact.product} AND purchase_id=${fact.purchaseId}`.execute(
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
  // Monetary rates have no cache-write tier yet; retain those known tokens
  // without publishing a plain-input estimate as a confirmed cost.
  if (!usage || usage.cacheWrite) return undefined;
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
