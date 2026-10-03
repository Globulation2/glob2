import Stripe from 'stripe';
import { Type } from 'typebox';
import type { FastifyInstance } from 'fastify';
import { Strict, Uuid } from '@glob2/protocol';
import { requireAccount, type Identity } from '../../identity.ts';
import { body } from '../../http/validate.ts';
import { apiError } from '../../errors.ts';
import { PRODUCTS, SkinBilling, type Sku } from './service.ts';
import { StripePayments } from './stripe.ts';

const Checkout = Strict({
  sku: Type.Union([Type.Literal('designer'), Type.Literal('stripes'), Type.Literal('spots')]),
  requestId: Uuid,
});
const Purchase = Strict({ purchaseId: Uuid });
export async function skinBillingRoutes(app: FastifyInstance, identity: Identity) {
  const env = app.services.config.secrets ?? process.env;
  const key = env['STRIPE_SECRET_KEY'],
    webhookSecret = env['STRIPE_WEBHOOK_SECRET'];
  const live = env['STRIPE_LIVE_ENABLED'] === 'true';
  if (
    key &&
    (!webhookSecret ||
      (!live && !key.startsWith('sk_test_')) ||
      (live && !key.startsWith('sk_live_')))
  )
    throw new Error(
      'Stripe requires a matching test/live key and webhook secret; live mode must be explicitly enabled.',
    );
  const stripe = key ? new Stripe(key, { timeout: 10000, maxNetworkRetries: 2 }) : undefined;
  const prices: Partial<Record<Sku, string>> = {};
  for (const sku of Object.keys(PRODUCTS) as Sku[]) {
    const price = env[PRODUCTS[sku].env];
    if (price) prices[sku] = price;
  }
  const billing = stripe
    ? new SkinBilling(
        app.services.db,
        new StripePayments(stripe, live),
        prices,
        app.services.config.publicOrigin,
      )
    : undefined;
  if (billing) {
    let closing = false;
    let pending: Promise<unknown> | undefined;
    const timer = setInterval(() => {
      if (closing || pending) return;
      pending = billing
        .reconcileDue(
          5,
          (err, purchaseId) =>
            app.log.error({ err, purchaseId }, 'Skin payment reconciliation failed'),
          () => closing,
        )
        .catch((err) => app.log.error({ err }, 'Skin payment reconciliation sweep failed'))
        .finally(() => {
          pending = undefined;
        });
    }, 60000);
    timer.unref();
    app.addHook('onClose', async () => {
      closing = true;
      clearInterval(timer);
      await pending;
    });
  }
  app.get('/api/v1/skins/products', async () => ({
    testMode: !live,
    items: await Promise.all(
      (Object.keys(PRODUCTS) as Sku[]).map(async (sku) => {
        const priceId = prices[sku];
        const price = stripe && priceId ? await stripe.prices.retrieve(priceId) : undefined;
        const available =
          !!price &&
          price.active &&
          price.livemode === live &&
          price.type === 'one_time' &&
          !!price.unit_amount;
        return {
          sku,
          name: PRODUCTS[sku].name,
          available,
          amount: available ? price?.unit_amount : null,
          currency: available ? price?.currency : null,
        };
      }),
    ),
  }));
  app.get('/api/v1/skins/purchases', async (request) => {
    const { account } = await requireAccount(identity, request);
    const items = await app.services.db
      .selectFrom('skin_purchases')
      .select(['id', 'sku', 'status', 'created_at as createdAt'])
      .where('account_id', '=', account.id)
      .orderBy('created_at', 'desc')
      .limit(100)
      .execute();
    return { items };
  });
  app.post('/api/v1/skins/checkout', async (request) => {
    const { account } = await requireAccount(identity, request);
    const input = body(Checkout, request.body);
    if (!billing) throw apiError('unavailable', 'Purchases are not configured on this server yet.');
    return billing.checkout(account.id, input.sku, input.requestId);
  });
  app.post('/api/v1/skins/purchases/reconcile', async (request) => {
    const { account } = await requireAccount(identity, request);
    const input = body(Purchase, request.body);
    if (!billing) throw apiError('unavailable', 'Purchases are not configured on this server yet.');
    return billing.reconcile(input.purchaseId, account.id);
  });
  // Scoped parser preserves the exact signed bytes without changing other API JSON routes.
  await app.register(async (webhook) => {
    webhook.removeContentTypeParser('application/json');
    webhook.addContentTypeParser(
      'application/json',
      { parseAs: 'buffer' },
      (_request, payload, done) => done(null, payload),
    );
    webhook.post('/api/v1/skins/stripe-webhook', { bodyLimit: 262144 }, async (request) => {
      if (!stripe || !billing || !webhookSecret)
        throw apiError('unavailable', 'Payments are not configured.');
      const signature = request.headers['stripe-signature'];
      if (typeof signature !== 'string' || !Buffer.isBuffer(request.body))
        throw apiError('bad_request', 'Invalid webhook.');
      let event: Stripe.Event;
      try {
        event = stripe.webhooks.constructEvent(request.body, signature, webhookSecret);
      } catch {
        throw apiError('bad_request', 'Invalid webhook signature.');
      }
      if (event.livemode !== live) throw apiError('bad_request', 'Webhook mode mismatch.');
      if (
        await app.services.db
          .selectFrom('skin_payment_events')
          .select('id')
          .where('id', '=', event.id)
          .executeTakeFirst()
      )
        return { received: true };
      let purchaseId: string | undefined, sessionId: string | undefined;
      if (
        event.type === 'checkout.session.completed' ||
        event.type === 'checkout.session.async_payment_succeeded' ||
        event.type === 'checkout.session.async_payment_failed' ||
        event.type === 'checkout.session.expired'
      ) {
        const session = event.data.object;
        purchaseId = session.metadata?.purchaseId;
        sessionId = session.id;
      } else if (
        event.type === 'charge.refunded' ||
        event.type === 'charge.dispute.created' ||
        event.type === 'charge.dispute.updated' ||
        event.type === 'charge.dispute.closed'
      ) {
        const object = event.data.object;
        const charge =
          object.object === 'charge'
            ? object
            : await stripe.charges.retrieve(
                typeof object.charge === 'string' ? object.charge : object.charge.id,
              );
        const pi = charge.payment_intent;
        if (pi) {
          const payment = typeof pi === 'string' ? await stripe.paymentIntents.retrieve(pi) : pi;
          purchaseId = payment.metadata.purchaseId;
          if (purchaseId) {
            const sessions = await stripe.checkout.sessions.list({
              payment_intent: payment.id,
              limit: 1,
            });
            sessionId = sessions.data[0]?.id;
          }
        }
      }
      if (purchaseId) {
        if (!sessionId)
          throw apiError('unavailable', 'Checkout is not available yet; retry this event.');
        await billing.reconcile(purchaseId, undefined, sessionId);
      }
      await app.services.db
        .insertInto('skin_payment_events')
        .values({ id: event.id, event_type: event.type, purchase_id: purchaseId ?? null })
        .onConflict((oc) => oc.column('id').doNothing())
        .execute();
      return { received: true };
    });
  });
}
