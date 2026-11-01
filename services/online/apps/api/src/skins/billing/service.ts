import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import { recordPaymentFact, type ProviderDisputeFact } from '@glob2/billing';
import { apiError } from '../../errors.ts';

export const PRODUCTS = {
  designer: { name: 'Skin designer', entitlement: 'skins:designer', env: 'STRIPE_PRICE_DESIGNER' },
  stripes: { name: 'Colony stripes', entitlement: 'skins:stripes', env: 'STRIPE_PRICE_STRIPES' },
  spots: { name: 'Colony spots', entitlement: 'skins:spots', env: 'STRIPE_PRICE_SPOTS' },
} as const;
export type Sku = keyof typeof PRODUCTS;
export type PaymentState = 'pending' | 'paid' | 'refunded' | 'disputed' | 'failed';
export interface PaymentSnapshot {
  monetary?: {
    paid: number;
    refunded: number;
    disputed: boolean;
    currency: string;
    live: boolean;
    createdAt: number;
    refunds?: { id: string; amount: number; at: Date }[];
    disputes?: ProviderDisputeFact[];
  };
  purchaseId: string;
  accountId: string;
  sessionId: string;
  priceId: string;
  paymentIntentId: string | null;
  state: PaymentState;
}
export interface Payments {
  checkout(
    purchaseId: string,
    accountId: string,
    priceId: string,
    origin: string,
    expiresAt: number,
    existingSessionId?: string,
  ): Promise<{ id: string; url: string }>;
  recover(
    purchaseId: string,
    accountId: string,
    createdAt: Date,
    cursor?: string,
  ): Promise<{ sessionId: string | null; nextCursor: string | null }>;
  inspect(sessionId: string): Promise<PaymentSnapshot>;
}

export class SkinBilling {
  private db: Kysely<Database>;
  private payments: Payments;
  private prices: Partial<Record<Sku, string>>;
  private origin: string;
  constructor(
    db: Kysely<Database>,
    payments: Payments,
    prices: Partial<Record<Sku, string>>,
    origin: string,
  ) {
    this.db = db;
    this.payments = payments;
    this.prices = prices;
    this.origin = origin;
  }
  async checkout(
    accountId: string,
    sku: Sku,
    requestId: string,
  ): Promise<{ purchaseId: string; url: string }> {
    const product = PRODUCTS[sku],
      price = this.prices[sku];
    if (!price) throw apiError('unavailable', 'This product is not available for purchase yet.');
    const purchase = await this.db.transaction().execute(async (trx) => {
      const account = await trx
        .selectFrom('accounts')
        .select(['kind', 'status'])
        .where('id', '=', accountId)
        .forUpdate()
        .executeTakeFirst();
      if (account?.kind !== 'registered' || account.status !== 'active')
        throw apiError('forbidden', 'Link an active recoverable account before purchasing.');
      const existing = await trx
        .selectFrom('skin_purchases')
        .selectAll()
        .where('account_id', '=', accountId)
        .where('request_id', '=', requestId)
        .executeTakeFirst();
      if (existing) {
        if (existing.sku !== sku)
          throw apiError('conflict', 'This checkout request was already used for another product.');
        return existing;
      }
      const pending = await trx
        .selectFrom('skin_purchases')
        .selectAll()
        .where('account_id', '=', accountId)
        .where('sku', '=', sku)
        .where('status', '=', 'pending')
        .orderBy('created_at')
        .executeTakeFirst();
      if (pending) return pending;
      const grant = await trx
        .selectFrom('entitlements')
        .select('id')
        .where('account_id', '=', accountId)
        .where('entitlement', '=', product.entitlement)
        .where('revoked_at', 'is', null)
        .where((eb) =>
          eb.or([eb('expires_at', 'is', null), eb('expires_at', '>', sql<Date>`now()`)]),
        )
        .executeTakeFirst();
      if (grant) throw apiError('conflict', 'You already own this product.');
      return trx
        .insertInto('skin_purchases')
        .values({
          account_id: accountId,
          sku,
          request_id: requestId,
          entitlement: product.entitlement,
          price_id: price,
        })
        .returningAll()
        .executeTakeFirstOrThrow();
    });
    if (purchase.status !== 'pending')
      throw apiError('conflict', 'This checkout is already finished.');
    if (purchase.checkout_id) {
      const current = await this.reconcile(purchase.id, accountId);
      if (current.status === 'failed' && purchase.request_id !== requestId)
        return this.checkout(accountId, sku, requestId);
      if (current.status !== 'pending')
        throw apiError(
          'conflict',
          current.status === 'paid'
            ? 'You already own this product.'
            : 'This checkout is already finished.',
        );
    }
    return this.db.transaction().execute(async (trx) => {
      const current = await trx
        .selectFrom('skin_purchases')
        .selectAll()
        .where('id', '=', purchase.id)
        .forUpdate()
        .executeTakeFirstOrThrow();
      if (current.status !== 'pending')
        throw apiError('conflict', 'This checkout is already finished.');
      const created = new Date(current.created_at).getTime();
      if (!current.checkout_id && Date.now() - created >= 22 * 3600000)
        throw apiError(
          'unavailable',
          'This checkout is being recovered. Check your purchase status before trying again.',
        );
      const session = await this.payments.checkout(
        current.id,
        accountId,
        current.price_id,
        this.origin,
        Math.floor(created / 1000) + 23 * 3600,
        current.checkout_id ?? undefined,
      );
      await trx
        .updateTable('skin_purchases')
        .set({ checkout_id: session.id })
        .where('id', '=', current.id)
        .execute();
      return { purchaseId: current.id, url: session.url };
    });
  }

  async reconcile(purchaseId: string, accountId?: string, sessionId?: string) {
    return this.db.transaction().execute(async (trx) => {
      const purchase = await trx
        .selectFrom('skin_purchases')
        .selectAll()
        .where('id', '=', purchaseId)
        .forUpdate()
        .executeTakeFirst();
      if (!purchase || (accountId && purchase.account_id !== accountId))
        throw apiError('not_found', 'Purchase not found.');
      let checkout = purchase.checkout_id ?? sessionId;
      if (!checkout) {
        // The fixed creation/expiry window is closed before scanning. Pages can
        // then resume across crashes without skipping newer sessions.
        if (Date.now() - new Date(purchase.created_at).getTime() < 24 * 3600000)
          return { status: purchase.status };
        const recovered = await this.payments.recover(
          purchase.id,
          purchase.account_id,
          new Date(purchase.created_at),
          purchase.recovery_cursor ?? undefined,
        );
        checkout = recovered.sessionId ?? undefined;
        if (!checkout) {
          const status = recovered.nextCursor ? purchase.status : ('failed' as const);
          await trx
            .updateTable('skin_purchases')
            .set({
              recovery_cursor: recovered.nextCursor,
              status,
              reconcile_after: recovered.nextCursor
                ? sql`now() + interval '1 minute'`
                : sql`now() + interval '24 hours'`,
            })
            .where('id', '=', purchase.id)
            .execute();
          return { status };
        }
      }
      // Read Stripe inside the row lock. Old/out-of-order webhooks cannot race a
      // refund reconciliation and restore a grant from an earlier snapshot.
      const current = await this.payments.inspect(checkout);
      if (
        current.purchaseId !== purchase.id ||
        current.accountId !== purchase.account_id ||
        current.priceId !== purchase.price_id ||
        current.sessionId !== checkout
      )
        throw apiError('conflict', 'Checkout does not match this purchase.');
      let grantId = purchase.entitlement_id;
      if (current.monetary && current.monetary.paid > 0 && current.paymentIntentId) {
        const m = current.monetary;
        await recordPaymentFact(trx, {
          product: 'skins',
          purchaseId: purchase.id,
          providerId: current.paymentIntentId,
          mode: m.live ? 'live' : 'test',
          currency: m.currency,
          paid: m.paid,
          refunded: m.refunded,
          disputed: m.disputed,
          occurredAt: new Date(),
          paymentAt: new Date(m.createdAt * 1000),
          refunds: m.refunds,
          disputes: m.disputes,
        });
      }
      if (current.state === 'paid' && !grantId) {
        const grant = await trx
          .insertInto('entitlements')
          .values({
            account_id: purchase.account_id,
            entitlement: purchase.entitlement,
            source: `stripe:${purchase.id}`,
          })
          .returning('id')
          .executeTakeFirstOrThrow();
        grantId = grant.id;
      }
      if (grantId)
        await trx
          .updateTable('entitlements')
          .set({ revoked_at: current.state === 'paid' ? null : sql`now()` })
          .where('id', '=', grantId)
          .execute();
      await trx
        .updateTable('skin_purchases')
        .set({
          checkout_id: checkout,
          recovery_cursor: null,
          payment_intent_id: current.paymentIntentId,
          status: current.state,
          entitlement_id: grantId,
          updated_at: sql`now()`,
          reconcile_after:
            current.state === 'pending' || current.state === 'disputed'
              ? sql`now() + interval '15 minutes'`
              : sql`now() + interval '24 hours'`,
        })
        .where('id', '=', purchase.id)
        .execute();
      return { status: current.state };
    });
  }
  // Claim a bounded batch durably before contacting Stripe. Concurrent API
  // replicas skip leased rows; a crashed process becomes retryable in 10 min.
  async reconcileDue(
    limit = 5,
    onError: (error: unknown, purchaseId: string) => void = () => {},
    stopping: () => boolean = () => false,
  ) {
    if (!Number.isInteger(limit) || limit < 1 || limit > 50)
      throw new Error('Invalid reconciliation batch size');
    const purchases = await this.db.transaction().execute(async (trx) => {
      const rows = await trx
        .selectFrom('skin_purchases')
        .select('id')
        .where((eb) =>
          eb.or([
            eb('checkout_id', 'is not', null),
            eb.and([
              eb('status', '=', 'pending'),
              eb('created_at', '<', sql<Date>`now() - interval '24 hours'`),
            ]),
          ]),
        )
        .where('reconcile_after', '<=', sql<Date>`now()`)
        .orderBy('reconcile_after')
        .orderBy('id')
        .limit(limit)
        .forUpdate()
        .skipLocked()
        .execute();
      if (rows.length)
        await trx
          .updateTable('skin_purchases')
          .set({ reconcile_after: sql`now() + interval '10 minutes'` })
          .where(
            'id',
            'in',
            rows.map((row) => row.id),
          )
          .execute();
      return rows;
    });
    let checked = 0,
      failed = 0;
    for (const purchase of purchases) {
      if (stopping()) break;
      try {
        await this.reconcile(purchase.id);
        checked++;
      } catch (error) {
        failed++;
        onError(error, purchase.id);
      }
    }
    return { checked, failed };
  }
}
