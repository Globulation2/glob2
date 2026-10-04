import { randomUUID } from 'node:crypto';
import { sql, type Kysely, type Transaction } from 'kysely';
import type { Database } from '@glob2/db';
import Stripe from 'stripe';
import { HiveError, integer, type CreditProduct } from './credits.ts';
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
    return sql.table(`${this.product === 'hive' ? 'hive' : 'map'}_${name}`);
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
  async begin(account: string, packId: string) {
    const pack = this.packs.find((p) => p.id === packId);
    if (!pack) throw new HiveError('bad_request', 'Unknown credit pack.');
    const id = randomUUID();
    await sql`INSERT INTO ${this.table('purchases')}(id,account_id,pack) VALUES(${id},${account},${JSON.stringify(pack)}::jsonb)`.execute(
      this.db,
    );
    const session = await this.stripe.checkout.sessions.create(
      {
        mode: 'payment',
        line_items: [{ price: pack.priceId, quantity: 1 }],
        client_reference_id: id,
        metadata: { purchaseId: id, creditProduct: this.product },
        payment_intent_data: { metadata: { purchaseId: id, creditProduct: this.product } },
        success_url: `${this.origin}/${this.product === 'hive' ? 'commander' : 'map-studio'}?payment=returned`,
        cancel_url: `${this.origin}/${this.product === 'hive' ? 'commander' : 'map-studio'}?payment=cancelled`,
      },
      { idempotencyKey: `${this.product}:${id}` },
    );
    await sql`UPDATE ${this.table('purchases')} SET checkout_id=${session.id} WHERE id=${id}`.execute(
      this.db,
    );
    return { url: session.url };
  }
  async fulfill(session: Stripe.Checkout.Session, connection?: Transaction<Database>) {
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
      // Skin checkouts identify their account, while legacy Hive payments may
      // omit creditProduct. Do not mistake those skin payments for legacy Hive.
      if (!intent.metadata['creditProduct'] && intent.metadata['accountId']) return;
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
      await this.fulfill(session, lock);
      if (session.payment_status !== 'paid') return;
      const charges = await this.stripe.charges.list({ payment_intent: paymentId, limit: 100 });
      if (charges.has_more) throw new Error('Purchase requires manual reconciliation.');
      const disputes = await this.stripe.disputes.list({ payment_intent: paymentId, limit: 100 });
      if (disputes.has_more) throw new Error('Purchase requires manual reconciliation.');
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
    });
  }
}
