import type Stripe from 'stripe';
import type { Payments, PaymentSnapshot } from './service.ts';

export class StripePayments implements Payments {
  readonly stripe: Stripe;
  private live: boolean;
  constructor(stripe: Stripe, live: boolean) {
    this.stripe = stripe;
    this.live = live;
  }
  async checkout(
    purchaseId: string,
    accountId: string,
    priceId: string,
    origin: string,
    expiresAt: number,
    existingSessionId?: string,
  ) {
    const price = await this.stripe.prices.retrieve(priceId);
    if (
      !price.active ||
      price.type !== 'one_time' ||
      price.livemode !== this.live ||
      !price.unit_amount
    )
      throw new Error(
        'Skin price must be an active, fixed, one-time price in the configured mode.',
      );
    const metadata = { purchaseId, accountId };
    const session = existingSessionId
      ? await this.stripe.checkout.sessions.retrieve(existingSessionId)
      : await this.stripe.checkout.sessions.create(
          {
            mode: 'payment',
            expires_at: expiresAt,
            line_items: [{ price: priceId, quantity: 1 }],
            client_reference_id: purchaseId,
            metadata,
            payment_intent_data: { metadata },
            success_url: `${origin}/skins?purchase=${purchaseId}`,
            cancel_url: `${origin}/skins?checkout=cancelled`,
          },
          { idempotencyKey: `colony-skin:${purchaseId}` },
        );
    if (
      session.mode !== 'payment' ||
      session.livemode !== this.live ||
      session.client_reference_id !== purchaseId ||
      session.metadata?.purchaseId !== purchaseId ||
      session.metadata.accountId !== accountId
    )
      throw new Error('Unexpected skin checkout identity');
    if (!session.url || new URL(session.url).origin !== 'https://checkout.stripe.com')
      throw new Error('Stripe did not return a hosted checkout URL.');
    return { id: session.id, url: session.url };
  }
  async recover(purchaseId: string, accountId: string, createdAt: Date, cursor?: string) {
    const start = Math.floor(createdAt.getTime() / 1000);
    const page = await this.stripe.checkout.sessions.list({
      created: { gte: start - 300, lte: start + 86400 },
      limit: 100,
      ...(cursor ? { starting_after: cursor } : {}),
    });
    const matches = page.data.filter((session) => session.metadata?.purchaseId === purchaseId);
    if (matches.length > 1) throw new Error('Multiple checkout sessions for one purchase');
    const session = matches[0];
    if (session) {
      if (
        session.metadata?.accountId !== accountId ||
        session.client_reference_id !== purchaseId ||
        session.mode !== 'payment' ||
        session.livemode !== this.live
      )
        throw new Error('Invalid recovered checkout identity');
      return { sessionId: session.id, nextCursor: null };
    }
    const nextCursor = page.has_more ? page.data.at(-1)?.id : null;
    if (page.has_more && (!nextCursor || nextCursor === cursor))
      throw new Error('Checkout recovery pagination did not advance');
    return { sessionId: null, nextCursor: nextCursor ?? null };
  }
  async inspect(sessionId: string): Promise<PaymentSnapshot> {
    const session = await this.stripe.checkout.sessions.retrieve(sessionId, {
      expand: ['line_items', 'payment_intent.latest_charge'],
    });
    if (
      session.mode !== 'payment' ||
      session.livemode !== this.live ||
      session.line_items?.has_more ||
      session.line_items?.data.length !== 1
    )
      throw new Error('Unexpected skin checkout');
    const item = session.line_items.data[0];
    if (
      !item?.price ||
      item.quantity !== 1 ||
      !session.metadata?.purchaseId ||
      !session.metadata.accountId ||
      session.client_reference_id !== session.metadata.purchaseId
    )
      throw new Error('Invalid skin checkout identity');
    const payment = session.payment_intent;
    let state: PaymentSnapshot['state'] = session.status === 'expired' ? 'failed' : 'pending';
    if (session.payment_status === 'paid') state = 'paid';
    const latest =
      payment &&
      typeof payment !== 'string' &&
      payment.latest_charge &&
      typeof payment.latest_charge !== 'string'
        ? payment.latest_charge
        : null;
    let disputes: NonNullable<NonNullable<PaymentSnapshot['monetary']>['disputes']> = [];
    if (latest?.disputed) {
      const page = await this.stripe.disputes.list({ charge: latest.id, limit: 100 });
      if (page.has_more) throw new Error('Dispute history requires manual reconciliation.');
      if (page.data.some((d) => d.currency !== session.currency))
        throw new Error('Dispute currency does not match the purchase.');
      disputes = page.data.map((d) => ({
        id: d.id,
        amount: d.amount,
        at: new Date(d.created * 1000),
        active: d.status !== 'won' && d.status !== 'warning_closed',
      }));
    }
    const disputed = disputes.some((d) => d.active);
    // Entitlement rules keep their existing refund precedence; monetary facts
    // retain an overlapping dispute independently, including partial amounts.
    if (latest?.amount_refunded) state = 'refunded';
    else if (disputed) state = 'disputed';
    let refunds: { id: string; amount: number; at: Date }[] | undefined;
    if (latest?.amount_refunded) {
      const page = await this.stripe.refunds.list({ charge: latest.id, limit: 100 });
      if (page.has_more) throw new Error('Refund history requires manual reconciliation.');
      refunds = page.data
        .filter((r) => r.status === 'succeeded')
        .map((r) => ({ id: r.id, amount: r.amount, at: new Date(r.created * 1000) }));
    }
    return {
      purchaseId: session.metadata.purchaseId,
      monetary: {
        paid: session.payment_status === 'paid' ? (session.amount_total ?? 0) : 0,
        refunded: latest?.amount_refunded ?? 0,
        disputed,
        currency: session.currency ?? '',
        live: session.livemode,
        createdAt: latest?.created ?? session.created,
        refunds,
        disputes,
      },
      accountId: session.metadata.accountId,
      sessionId: session.id,
      priceId: item.price.id,
      paymentIntentId: typeof payment === 'string' ? payment : (payment?.id ?? null),
      state,
    };
  }
}
