import { randomUUID } from 'node:crypto';
import { sql, type Kysely, type Transaction } from 'kysely';
import type { Database } from '@glob2/db';
import Stripe from 'stripe';
import { CREDIT_PRODUCTS, HiveError, integer, type CreditProduct } from './credits.ts';
import { recordPaymentFact } from './reporting.ts';
export interface CreditPack {
  id: string;
  priceId: string;
  credits: number;
  amount: number;
  currency: string;
}
export interface Purchase {
  id: string;
  account_id: string;
  checkout_id: string | null;
  payment_id: string | null;
  pack: CreditPack;
  paid: boolean;
  reversed: number;
}
export class Checkout {
  readonly product: CreditProduct;
  private table(name: string) {
    return sql.table(`${CREDIT_PRODUCTS[this.product].prefix}_${name}`);
  }
  readonly db: Kysely<Database>;
  readonly stripe: Stripe;
  readonly packs: readonly CreditPack[];
  readonly origin: string;
  readonly secret: string;
  constructor(
    db: Kysely<Database>,
    key: string,
    secret: string,
    origin: string,
    packs: readonly CreditPack[],
    product: CreditProduct = 'hive',
  ) {
    this.product = product;
    this.db = db;
    this.stripe = new Stripe(key, { maxNetworkRetries: 0 });
    this.secret = secret;
    this.origin = origin;
    this.packs = packs;
    if (!key || !secret || !origin.startsWith('https://'))
      throw new Error('Credit checkout requires dedicated credentials and HTTPS.');
    for (const p of packs) {
      integer(p.credits);
      integer(p.amount);
      if (
        !p.credits ||
        !p.amount ||
        !/^price_/.test(p.priceId) ||
        !['usd', 'cad', 'eur', 'gbp'].includes(p.currency)
      )
        throw new Error('Invalid credit pack.');
    }
  }
  async begin(account: string, packId: string, requestId: string = randomUUID()) {
    const load = async () =>
      (
        await sql<
          Purchase & { retryable: boolean }
        >`SELECT *,created_at>now()-interval '23 hours' AS retryable FROM ${this.table('purchases')} WHERE id=${requestId}`.execute(
          this.db,
        )
      ).rows[0];
    let purchase = await load();
    if (!purchase) {
      const configuredPack = this.packs.find((p) => p.id === packId);
      if (!configuredPack) throw new HiveError('bad_request', 'Unknown credit pack.');
      // The caller may retain this ID after a lost response. Keep the original
      // pack even if its price changes or the operator removes it from sale.
      await sql`INSERT INTO ${this.table('purchases')}(id,account_id,pack) VALUES(${requestId},${account},${JSON.stringify(configuredPack)}::jsonb) ON CONFLICT(id) DO NOTHING`.execute(
        this.db,
      );
      purchase = await load();
    }
    if (!purchase || purchase.account_id !== account || purchase.pack.id !== packId)
      throw new HiveError('conflict', 'Checkout identifier was already used.');
    if (purchase.paid) throw new HiveError('conflict', 'This purchase is already complete.');
    if (purchase.checkout_id) {
      const session = await this.stripe.checkout.sessions.retrieve(purchase.checkout_id);
      if (!session.url || session.status !== 'open')
        throw new HiveError('conflict', 'This checkout has ended. Start a new purchase.');
      return { url: session.url };
    }
    // Stripe can prune idempotency keys after 24h. Never redispatch an older
    // unknown outcome; an operator can reconcile it from the purchase metadata.
    if (!purchase.retryable)
      throw new HiveError('conflict', 'This checkout outcome needs reconciliation.');
    const pack = purchase.pack;
    const session = await this.stripe.checkout.sessions.create(
      {
        mode: 'payment',
        line_items: [{ price: pack.priceId, quantity: 1 }],
        client_reference_id: requestId,
        metadata: { purchaseId: requestId, creditProduct: this.product },
        payment_intent_data: { metadata: { purchaseId: requestId, creditProduct: this.product } },
        success_url: `${this.origin}/${CREDIT_PRODUCTS[this.product].path}?payment=returned`,
        cancel_url: `${this.origin}/${CREDIT_PRODUCTS[this.product].path}?payment=cancelled`,
      },
      { idempotencyKey: `${this.product}:${requestId}` },
    );
    await sql`UPDATE ${this.table('purchases')} SET checkout_id=${session.id} WHERE id=${requestId}`.execute(
      this.db,
    );
    if (!session.url) throw new HiveError('conflict', 'This checkout has ended.');
    return { url: session.url };
  }
  async fulfill(
    session: Stripe.Checkout.Session,
    connection?: Transaction<Database>,
    paymentAt?: Date,
  ) {
    if (
      session.mode !== 'payment' ||
      session.payment_status !== 'paid' ||
      !session.client_reference_id
    )
      return;
    const payment =
      typeof session.payment_intent === 'string'
        ? session.payment_intent
        : session.payment_intent?.id;
    if (!payment) throw new Error('Paid checkout has no payment intent.');
    await this.transaction(connection, async (db) => {
      const p = (
        await sql<Purchase>`SELECT * FROM ${this.table('purchases')} WHERE id=${session.client_reference_id} FOR UPDATE`.execute(
          db,
        )
      ).rows[0];
      if (!p) throw new Error('Unknown credit purchase.');
      if (
        (p.checkout_id && p.checkout_id !== session.id) ||
        session.currency !== p.pack.currency ||
        session.amount_total !== p.pack.amount ||
        session.metadata?.['purchaseId'] !== p.id ||
        (session.metadata?.['creditProduct'] ?? 'hive') !== this.product
      )
        throw new Error('Checkout does not match the purchase.');
      if (p.paid) return;
      await recordPaymentFact(db, {
        product: this.product,
        purchaseId: p.id,
        providerId: payment,
        mode: session.livemode ? 'live' : 'test',
        currency: p.pack.currency,
        paid: session.amount_total ?? p.pack.amount,
        refunded: 0,
        disputed: false,
        occurredAt: paymentAt ?? new Date(session.created * 1000),
      });
      await sql`INSERT INTO ${this.table('wallets')}(account_id) VALUES(${p.account_id}) ON CONFLICT DO NOTHING`.execute(
        db,
      );
      await sql`UPDATE ${this.table('wallets')} SET balance=balance+${p.pack.credits} WHERE account_id=${p.account_id}`.execute(
        db,
      );
      await sql`INSERT INTO ${this.table('ledger')}(id,account_id,amount,kind,details) VALUES(${`purchase:${p.id}`},${p.account_id},${p.pack.credits},'purchase',${JSON.stringify({ checkout: session.id, pack: p.pack })}::jsonb)`.execute(
        db,
      );
      if (this.product === 'hive')
        await sql`INSERT INTO entitlements(account_id,entitlement,source) VALUES(${p.account_id},'hive-mind',${`purchase:${p.id}`})`.execute(
          db,
        );
      await sql`UPDATE ${this.table('purchases')} SET paid=true,checkout_id=${session.id},payment_id=${payment} WHERE id=${p.id}`.execute(
        db,
      );
    });
  }
  async reverse(
    payment: string,
    amount: number,
    disputed: boolean,
    eventId: string,
    connection?: Transaction<Database>,
  ) {
    integer(amount);
    await this.transaction(connection, async (db) => {
      const p = (
        await sql<Purchase>`SELECT * FROM ${this.table('purchases')} WHERE payment_id=${payment} FOR UPDATE`.execute(
          db,
        )
      ).rows[0];
      if (!p?.paid) return;
      const desired = disputed
        ? p.pack.credits
        : Number(
            (BigInt(Math.min(amount, p.pack.amount)) * BigInt(p.pack.credits) +
              BigInt(p.pack.amount) -
              1n) /
              BigInt(p.pack.amount),
          );
      const delta = Number(p.reversed) - desired;
      if (!delta) return;
      const inserted = (
        await sql`INSERT INTO ${this.table('ledger')}(id,account_id,amount,kind) VALUES(${`reversal:${eventId}`},${p.account_id},${delta},${disputed ? 'dispute' : 'refund'}) ON CONFLICT DO NOTHING RETURNING id`.execute(
          db,
        )
      ).rows;
      if (!inserted.length) return;
      await sql`UPDATE ${this.table('wallets')} SET balance=balance+${delta} WHERE account_id=${p.account_id}`.execute(
        db,
      );
      await sql`UPDATE ${this.table('purchases')} SET reversed=${desired} WHERE id=${p.id}`.execute(
        db,
      );
    });
  }
  private transaction<T>(
    connection: Transaction<Database> | undefined,
    work: (db: Transaction<Database>) => Promise<T>,
  ) {
    return connection ? work(connection) : this.db.transaction().execute(work);
  }
  async webhook(raw: Buffer, signature: string) {
    const event = this.stripe.webhooks.constructEvent(raw, signature, this.secret);
    let payment: string | undefined;
    let session: Stripe.Checkout.Session | undefined;
    if (
      event.type === 'checkout.session.completed' ||
      event.type === 'checkout.session.async_payment_succeeded'
    ) {
      session = await this.stripe.checkout.sessions.retrieve(event.data.object.id);
      payment =
        typeof session.payment_intent === 'string'
          ? session.payment_intent
          : session.payment_intent?.id;
    } else if (
      ['charge.refunded', 'charge.dispute.created', 'charge.dispute.closed'].includes(event.type)
    ) {
      const object = event.data.object as Stripe.Charge | Stripe.Dispute;
      const chargeId =
        'charge' in object
          ? typeof object.charge === 'string'
            ? object.charge
            : object.charge.id
          : object.id;
      const charge = await this.stripe.charges.retrieve(chargeId);
      payment =
        typeof charge.payment_intent === 'string'
          ? charge.payment_intent
          : charge.payment_intent?.id;
    } else return;
    if (!payment) return;
    const paymentId = payment;
    // Serialize canonical Stripe reads as well as ledger writes. Delivery order
    // cannot restore credits using an older refund/dispute snapshot.
    await this.db.transaction().execute(async (lock) => {
      await sql`SELECT pg_advisory_xact_lock(hashtextextended(${paymentId},0))`.execute(lock);
      const intent = await this.stripe.paymentIntents.retrieve(paymentId);
      const purchaseId = intent.metadata['purchaseId'];
      if (!purchaseId || (intent.metadata['creditProduct'] ?? 'hive') !== this.product) return; // A different application using the same Stripe account.
      if (!session) {
        const list = await this.stripe.checkout.sessions.list({
          payment_intent: paymentId,
          limit: 1,
        });
        session = list.data[0];
      }
      if (!session) throw new Error('Purchase checkout is not yet available.');
      if (session.payment_status !== 'paid') return;
      const charges = await this.stripe.charges.list({ payment_intent: paymentId, limit: 100 });
      if (charges.has_more) throw new Error('Purchase requires manual reconciliation.');
      const successful = charges.data.filter((c) => c.paid);
      const paymentAt = successful.length
        ? new Date(Math.min(...successful.map((c) => c.created)) * 1000)
        : new Date(event.created * 1000);
      await this.fulfill(session, lock, paymentAt);
      const disputes = await this.stripe.disputes.list({ payment_intent: paymentId, limit: 100 });
      if (disputes.has_more) throw new Error('Purchase requires manual reconciliation.');
      if (disputes.data.some((d) => d.currency !== session?.currency))
        throw new Error('Dispute currency does not match the purchase.');
      const disputed = disputes.data.some(
        (d) => d.status !== 'won' && d.status !== 'warning_closed',
      );
      await this.reverse(
        paymentId,
        charges.data.reduce((sum, c) => sum + c.amount_refunded, 0),
        disputed,
        event.id,
        lock,
      );
      const refunded = charges.data.reduce((sum, c) => sum + c.amount_refunded, 0);
      let refunds: { id: string; amount: number; at: Date }[] | undefined;
      if (refunded) {
        const page = await this.stripe.refunds.list({ payment_intent: paymentId, limit: 100 });
        if (page.has_more) throw new Error('Refund history requires manual reconciliation.');
        refunds = page.data
          .filter((r) => r.status === 'succeeded')
          .map((r) => ({ id: r.id, amount: r.amount, at: new Date(r.created * 1000) }));
      }
      const closedEvent = event.type === 'charge.dispute.closed' ? event.data.object : undefined;
      await recordPaymentFact(lock, {
        product: this.product,
        purchaseId,
        providerId: paymentId,
        mode: session.livemode ? 'live' : 'test',
        currency: session.currency ?? '',
        paid: session.amount_total ?? 0,
        refunded,
        refunds,
        disputes: disputes.data.map((d) => ({
          id: d.id,
          amount: d.amount,
          at: new Date(d.created * 1000),
          active: d.status !== 'won' && d.status !== 'warning_closed',
          ...(closedEvent?.id === d.id ? { closedAt: new Date(event.created * 1000) } : {}),
        })),
        paymentAt,
        disputed,
        occurredAt: new Date(),
      });
    });
  }
}
