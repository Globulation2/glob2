// Spilled NOTIFY payloads, reconnect listeners and leader fencing (migrated
// schema: notification_payloads, leader_leases).
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { sql } from 'kysely';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import {
  LeaderElection,
  LeaderLostError,
  MAX_NOTIFY_PAYLOAD_BYTES,
  PgPubSub,
  assertLease,
  notify,
  type LeaderLease,
} from '../src/index.ts';
import { createTestDatabase, type TestDatabase } from './support.ts';

let database: TestDatabase;

beforeAll(async () => {
  database = await createTestDatabase();
});

afterAll(async () => {
  await database?.drop();
});

async function until(condition: () => boolean | Promise<boolean>, timeoutMs = 10_000) {
  const deadline = Date.now() + timeoutMs;
  while (!(await condition())) {
    if (Date.now() > deadline) throw new Error('timed out waiting for condition');
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
}

describe('notify', () => {
  it('delivers payloads over the NOTIFY limit intact and in order', async () => {
    const bus = new PgPubSub({ connectionString: database.url });
    const received: unknown[] = [];
    try {
      await bus.subscribe('spill-test', (payload) => received.push(payload));
      const big = { text: 'é'.repeat(MAX_NOTIFY_PAYLOAD_BYTES) };
      await notify(database.db, 'spill-test', { n: 1 });
      await notify(database.db, 'spill-test', big);
      await notify(database.db, 'spill-test', { n: 3 });
      await bus.publish(database.pool, 'spill-test', { n: 4, big: big.text });
      await until(() => received.length === 4);
      expect(received).toEqual([{ n: 1 }, big, { n: 3 }, { n: 4, big: big.text }]);
      const stored = await database.db
        .selectFrom('notification_payloads')
        .select('channel')
        .execute();
      expect(stored.map((r) => r.channel)).toEqual(['spill-test', 'spill-test']);
    } finally {
      await bus.close();
    }
  });

  it('sends nothing when the surrounding transaction rolls back', async () => {
    const bus = new PgPubSub({ connectionString: database.url });
    const received: unknown[] = [];
    try {
      await bus.subscribe('rollback-test', (payload) => received.push(payload));
      await expect(
        database.db.transaction().execute(async (trx) => {
          await notify(trx, 'rollback-test', { lost: true });
          await notify(trx, 'rollback-test', { big: 'x'.repeat(10_000) });
          throw new Error('rolled back');
        }),
      ).rejects.toThrow('rolled back');
      await notify(database.db, 'rollback-test', { after: true });
      await until(() => received.length === 1);
      expect(received).toEqual([{ after: true }]);
    } finally {
      await bus.close();
    }
  });

  it('is the only place that sends NOTIFY', () => {
    const root = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..');
    const offenders: string[] = [];
    const walk = (dir: string) => {
      for (const name of readdirSync(dir)) {
        if (name === 'node_modules' || name === 'test' || name === 'fixtures') continue;
        const path = join(dir, name);
        if (statSync(path).isDirectory()) walk(path);
        else if (/\.tsx?$/.test(name) && /pg_notify/i.test(readFileSync(path, 'utf8'))) {
          offenders.push(relative(root, path));
        }
      }
    };
    for (const top of ['apps', 'packages']) walk(join(root, top));
    expect(offenders).toEqual(['packages/db/src/notify.ts']);
  });

  it('tells reconnect listeners when notifications may have been missed', async () => {
    const bus = new PgPubSub({ connectionString: database.url, reconnectDelayMs: 20 });
    let calls = 0;
    try {
      await bus.subscribe('reconnect-test', () => undefined);
      const remove = bus.addReconnectListener(() => calls++);
      expect(bus.connected).toBe(true);
      await sql`SELECT pg_terminate_backend(pid) FROM pg_stat_activity
                WHERE application_name = 'glob2-pubsub' AND datname = current_database()`.execute(
        database.db,
      );
      await until(() => calls === 1);
      expect(bus.connected).toBe(true);
      expect(bus.reconnectCount).toBe(1);
      remove();
    } finally {
      await bus.close();
    }
  });
});

describe('leader fencing', () => {
  it('bumps the epoch per leader and refuses writes from a stale lease', async () => {
    const leases: LeaderLease[] = [];
    const make = () =>
      new LeaderElection({
        connectionString: database.url,
        name: 'fencing-test',
        retryMs: 30,
        checkMs: 50,
        fencing: true,
        lead: async (signal, lease) => {
          leases.push(lease);
          await new Promise<void>((resolve) => signal.addEventListener('abort', () => resolve()));
        },
      });
    const a = make();
    const b = make();
    try {
      a.start();
      await until(() => a.isLeader);
      const first = a.currentLease!;
      await expect(
        database.db.transaction().execute((trx) => assertLease(trx, first)),
      ).resolves.toBeUndefined();
      b.start();

      // Another process takes over the lease (as a new leader would): the old
      // leader's fenced writes fail, and its lease check gives leadership up.
      await sql`UPDATE leader_leases SET epoch = epoch + 1 WHERE name = 'fencing-test'`.execute(
        database.db,
      );
      await expect(
        database.db.transaction().execute((trx) => assertLease(trx, first)),
      ).rejects.toBeInstanceOf(LeaderLostError);
      await until(() => !a.isLeader || leases.length > 1);
      await until(() => leases.length >= 2);
      const second = leases.at(-1)!;
      expect(second.epoch).toBeGreaterThan(first.epoch + 1);
      await expect(
        database.db.transaction().execute((trx) => assertLease(trx, second)),
      ).resolves.toBeUndefined();
      expect([a.isLeader, b.isLeader].filter(Boolean)).toHaveLength(1);
    } finally {
      await a.stop();
      await b.stop();
    }
  });

  it('verify() re-checks the lock and fails once the session is gone', async () => {
    const leader = new LeaderElection({
      connectionString: database.url,
      name: 'verify-test',
      retryMs: 5_000,
      checkMs: 60_000,
      fencing: true,
      lead: (signal) =>
        new Promise<void>((resolve) => signal.addEventListener('abort', () => resolve())),
    });
    try {
      leader.start();
      await until(() => leader.isLeader);
      await expect(leader.verify(0)).resolves.toBeUndefined();
      // Drop the lock behind the leader's back (as when Postgres ended the
      // session but the client never noticed).
      const [k1, k2] = await sql<{ pid: number; classid: number; objid: number }>`
        SELECT pid, classid::int8 AS classid, objid::int8 AS objid FROM pg_locks
        WHERE locktype = 'advisory' AND objsubid = 2
          -- pg_locks is cluster-wide; other test files hold leader locks too.
          AND database = (SELECT oid FROM pg_database WHERE datname = current_database())`
        .execute(database.db)
        .then((r) => [r.rows.length, r.rows[0]]);
      expect(k1).toBe(1);
      await sql`SELECT pg_terminate_backend(${(k2 as { pid: number }).pid})`.execute(database.db);
      await expect(leader.verify(0)).rejects.toBeInstanceOf(LeaderLostError);
      await until(() => !leader.isLeader);
    } finally {
      await leader.stop();
    }
  });
});
