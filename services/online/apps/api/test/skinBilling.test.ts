import { randomUUID } from 'node:crypto';
import { beforeEach, afterEach, expect, it } from 'vitest';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { SkinBilling, type Payments, type PaymentSnapshot } from '../src/skins/billing/service.ts';
let database: TestDatabase;
beforeEach(async () => {
  database = await createTestDatabase();
});
afterEach(async () => {
  await database?.drop();
});
it('fulfills once, rejects mismatches, and reconciles delayed/refunded/disputed payments', async () => {
  const db = database.db;
  const owner = await db
    .insertInto('accounts')
    .values({ kind: 'registered', display_name: 'Buyer' })
    .returning('id')
    .executeTakeFirstOrThrow();
  const guest = await db
    .insertInto('accounts')
    .values({ kind: 'guest', display_name: 'Guest' })
    .returning('id')
    .executeTakeFirstOrThrow();
  const sessions = new Map<string, PaymentSnapshot>();
  const payments: Payments = {
    async recover() {
      throw new Error('Unexpected recovery');
    },
    async checkout(purchaseId, accountId, priceId) {
      const id = `cs_${purchaseId}`;
      if (!sessions.has(id))
        sessions.set(id, {
          sessionId: id,
          purchaseId,
          accountId,
          priceId,
          paymentIntentId: 'pi_test',
          state: 'pending',
        });
      return { id, url: `https://checkout.stripe.com/${id}` };
    },
    async inspect(id) {
      const s = sessions.get(id);
      if (!s) throw new Error('Missing session');
      return { ...s };
    },
  };
  const billing = new SkinBilling(
    db,
    payments,
    { designer: 'price_designer' },
    'https://play.test',
  );
  await expect(billing.checkout(guest.id, 'designer', randomUUID())).rejects.toThrow('recoverable');
  const [one, two] = await Promise.all([
    billing.checkout(owner.id, 'designer', randomUUID()),
    billing.checkout(owner.id, 'designer', randomUUID()),
  ]);
  expect(one.purchaseId).toBe(two.purchaseId);
  const snapshot = sessions.get(`cs_${one.purchaseId}`);
  if (!snapshot) throw new Error('No checkout');
  expect(await billing.reconcile(one.purchaseId, owner.id)).toEqual({ status: 'pending' });
  expect(await db.selectFrom('entitlements').selectAll().execute()).toHaveLength(0);
  snapshot.state = 'paid';
  snapshot.priceId = 'price_wrong';
  await expect(billing.reconcile(one.purchaseId, owner.id)).rejects.toThrow('does not match');
  snapshot.priceId = 'price_designer';
  await Promise.all([
    billing.reconcile(one.purchaseId, owner.id),
    billing.reconcile(one.purchaseId, owner.id),
  ]);
  let grants = await db.selectFrom('entitlements').selectAll().execute();
  expect(grants).toHaveLength(1);
  expect(grants[0]?.revoked_at).toBeNull();
  snapshot.state = 'disputed';
  await billing.reconcile(one.purchaseId);
  grants = await db.selectFrom('entitlements').selectAll().execute();
  expect(grants[0]?.revoked_at).toBeInstanceOf(Date);
  snapshot.state = 'paid';
  await billing.reconcile(one.purchaseId);
  grants = await db.selectFrom('entitlements').selectAll().execute();
  expect(grants).toHaveLength(1);
  expect(grants[0]?.revoked_at).toBeNull();
  snapshot.state = 'refunded';
  await billing.reconcile(one.purchaseId);
  await billing.reconcile(one.purchaseId); // late completed event still reads current refund
  grants = await db.selectFrom('entitlements').selectAll().execute();
  expect(grants[0]?.revoked_at).toBeInstanceOf(Date);
  await expect(billing.reconcile(one.purchaseId, guest.id)).rejects.toThrow('not found');
});

it('durably schedules missed-payment recovery across concurrent replicas and backs off failures', async () => {
  const db = database.db;
  const sessions = new Map<string, PaymentSnapshot>();
  const calls = new Map<string, number>();
  let failingSession = '';
  const payments: Payments = {
    async recover() {
      throw new Error('Unexpected recovery');
    },
    async checkout(purchaseId, accountId, priceId) {
      const id = `cs_${purchaseId}`;
      sessions.set(id, {
        sessionId: id,
        purchaseId,
        accountId,
        priceId,
        paymentIntentId: `pi_${purchaseId}`,
        state: 'paid',
      });
      return { id, url: `https://checkout.stripe.com/${id}` };
    },
    async inspect(id) {
      calls.set(id, (calls.get(id) ?? 0) + 1);
      if (id === failingSession) throw new Error('Temporary provider outage');
      const snapshot = sessions.get(id);
      if (!snapshot) throw new Error('Unknown session');
      return { ...snapshot };
    },
  };
  const make = () =>
    new SkinBilling(db, payments, { stripes: 'price_stripes' }, 'https://play.test');
  const billing = make();
  const ids: string[] = [];
  for (let i = 0; i < 3; i++) {
    const account = await db
      .insertInto('accounts')
      .values({ kind: 'registered', display_name: `Recovery ${i}` })
      .returning('id')
      .executeTakeFirstOrThrow();
    ids.push((await billing.checkout(account.id, 'stripes', randomUUID())).purchaseId);
  }
  failingSession = `cs_${ids[2]}`;
  const errors: string[] = [];
  const results = await Promise.all([
    billing.reconcileDue(2, (_error, id) => errors.push(id)),
    make().reconcileDue(2, (_error, id) => errors.push(id)),
  ]);
  expect(results.reduce((n, r) => n + r.checked, 0)).toBe(2);
  expect(results.reduce((n, r) => n + r.failed, 0)).toBe(1);
  expect(errors).toEqual([ids[2]]);
  expect([...calls.values()]).toEqual([1, 1, 1]);
  const rows = await db.selectFrom('skin_purchases').selectAll().where('id', 'in', ids).execute();
  expect(rows.filter((r) => r.status === 'paid')).toHaveLength(2);
  expect(rows.every((r) => new Date(r.reconcile_after).getTime() > Date.now() + 9 * 60000)).toBe(
    true,
  );
  expect(await make().reconcileDue()).toEqual({ checked: 0, failed: 0 });
  await db
    .updateTable('skin_purchases')
    .set({ reconcile_after: new Date(0) })
    .where('id', '=', ids[2]!)
    .execute();
  failingSession = '';
  expect(await make().reconcileDue()).toEqual({ checked: 1, failed: 0 });
  expect(
    (
      await db
        .selectFrom('skin_purchases')
        .select('status')
        .where('id', '=', ids[2]!)
        .executeTakeFirstOrThrow()
    ).status,
  ).toBe('paid');
});

it('reuses a known pending session and replaces expired checkout only for a new request', async () => {
  const db = database.db;
  const account = await db
    .insertInto('accounts')
    .values({ kind: 'registered', display_name: 'Expired checkout' })
    .returning('id')
    .executeTakeFirstOrThrow();
  const snapshots = new Map<string, PaymentSnapshot>();
  const resumed: string[] = [];
  const payments: Payments = {
    async recover() {
      throw new Error('Unexpected recovery');
    },
    async checkout(purchaseId, accountId, priceId, _origin, _expiresAt, existingSessionId) {
      const id = existingSessionId ?? `cs_${purchaseId}`;
      if (existingSessionId) resumed.push(id);
      else
        snapshots.set(id, {
          purchaseId,
          accountId,
          priceId,
          sessionId: id,
          paymentIntentId: `pi_${purchaseId}`,
          state: 'pending',
        });
      return { id, url: `https://checkout.stripe.com/${id}` };
    },
    async inspect(id) {
      const snapshot = snapshots.get(id);
      if (!snapshot) throw new Error('Missing session');
      return { ...snapshot };
    },
  };
  const billing = new SkinBilling(db, payments, { spots: 'price_spots' }, 'https://play.test');
  const request = randomUUID();
  const first = await billing.checkout(account.id, 'spots', request);
  expect((await billing.checkout(account.id, 'spots', randomUUID())).purchaseId).toBe(
    first.purchaseId,
  );
  expect(resumed).toEqual([`cs_${first.purchaseId}`]);
  snapshots.get(`cs_${first.purchaseId}`)!.state = 'failed';
  await expect(billing.checkout(account.id, 'spots', request)).rejects.toThrow('finished');
  const replacement = await billing.checkout(account.id, 'spots', randomUUID());
  expect(replacement.purchaseId).not.toBe(first.purchaseId);
  expect(snapshots.size).toBe(2);
});

it('recovers an unrecorded checkout across paginated restart without creating a second session', async () => {
  const db = database.db;
  const owner = await db
    .insertInto('accounts')
    .values({ kind: 'registered', display_name: 'Recovery after crash' })
    .returning('id')
    .executeTakeFirstOrThrow();
  const purchase = await db
    .insertInto('skin_purchases')
    .values({
      account_id: owner.id,
      request_id: randomUUID(),
      sku: 'stripes',
      entitlement: 'skins:stripes',
      price_id: 'price_recovery',
      created_at: new Date(Date.now() - 48 * 3600000),
    })
    .returningAll()
    .executeTakeFirstOrThrow();
  const cursors: (string | undefined)[] = [];
  const payments: Payments = {
    async checkout() {
      throw new Error('Must not create a second checkout');
    },
    async recover(id, accountId, createdAt, cursor) {
      expect(id).toBe(purchase.id);
      expect(accountId).toBe(owner.id);
      expect(createdAt.getTime()).toBe(new Date(purchase.created_at).getTime());
      cursors.push(cursor);
      return cursor
        ? { sessionId: 'cs_recovered', nextCursor: null }
        : { sessionId: null, nextCursor: 'cs_page_one' };
    },
    async inspect(id) {
      return {
        sessionId: id,
        purchaseId: purchase.id,
        accountId: owner.id,
        priceId: 'price_recovery',
        paymentIntentId: 'pi_recovered',
        state: 'paid',
      };
    },
  };
  const make = () =>
    new SkinBilling(db, payments, { stripes: 'price_recovery' }, 'https://play.test');
  await expect(make().checkout(owner.id, 'stripes', randomUUID())).rejects.toThrow(
    'being recovered',
  );
  expect(await make().reconcileDue()).toEqual({ checked: 1, failed: 0 });
  expect(
    (
      await db
        .selectFrom('skin_purchases')
        .select('recovery_cursor')
        .where('id', '=', purchase.id)
        .executeTakeFirstOrThrow()
    ).recovery_cursor,
  ).toBe('cs_page_one');
  await db
    .updateTable('skin_purchases')
    .set({ reconcile_after: new Date(0) })
    .where('id', '=', purchase.id)
    .execute();
  expect(await make().reconcileDue()).toEqual({ checked: 1, failed: 0 });
  expect(cursors).toEqual([undefined, 'cs_page_one']);
  const recovered = await db
    .selectFrom('skin_purchases')
    .selectAll()
    .where('id', '=', purchase.id)
    .executeTakeFirstOrThrow();
  expect(recovered.checkout_id).toBe('cs_recovered');
  expect(recovered.status).toBe('paid');
  expect(
    await db.selectFrom('entitlements').selectAll().where('account_id', '=', owner.id).execute(),
  ).toHaveLength(1);
  await expect(make().checkout(owner.id, 'stripes', randomUUID())).rejects.toThrow('already own');
});

it('allows a replacement only after a complete scan finds no old checkout', async () => {
  const db = database.db;
  const owner = await db
    .insertInto('accounts')
    .values({ kind: 'registered', display_name: 'Never created checkout' })
    .returning('id')
    .executeTakeFirstOrThrow();
  const purchase = await db
    .insertInto('skin_purchases')
    .values({
      account_id: owner.id,
      request_id: randomUUID(),
      sku: 'spots',
      entitlement: 'skins:spots',
      price_id: 'price_empty',
      created_at: new Date(Date.now() - 48 * 3600000),
    })
    .returningAll()
    .executeTakeFirstOrThrow();
  let created = 0;
  const payments: Payments = {
    async recover() {
      return { sessionId: null, nextCursor: null };
    },
    async inspect() {
      throw new Error('No old checkout');
    },
    async checkout(id, _account, _price, _origin, expiresAt) {
      created++;
      expect(expiresAt).toBeGreaterThan(Date.now() / 1000 + 22 * 3600);
      return { id: `cs_${id}`, url: `https://checkout.stripe.com/${id}` };
    },
  };
  const billing = new SkinBilling(db, payments, { spots: 'price_empty' }, 'https://play.test');
  expect(await billing.reconcile(purchase.id, owner.id)).toEqual({ status: 'failed' });
  const replacement = await billing.checkout(owner.id, 'spots', randomUUID());
  expect(replacement.purchaseId).not.toBe(purchase.id);
  expect(created).toBe(1);
});
