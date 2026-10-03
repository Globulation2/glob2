import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { LeaderElection, PgPubSub } from '../src/index.ts';
import { createTestDatabase, type TestDatabase } from './support.ts';

let database: TestDatabase;

beforeAll(async () => {
  database = await createTestDatabase({ migrate: false });
});

afterAll(async () => {
  await database?.drop();
});

async function until(condition: () => boolean, timeoutMs = 10_000): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  while (!condition()) {
    if (Date.now() > deadline) throw new Error('timed out waiting for condition');
    await new Promise((resolve) => setTimeout(resolve, 20));
  }
}

async function terminate(applicationName: string): Promise<number> {
  const result = await database.pool.query<{ count: string }>(
    `SELECT count(pg_terminate_backend(pid)) AS count FROM pg_stat_activity
     WHERE application_name = $1 AND datname = current_database()`,
    [applicationName],
  );
  return Number(result.rows[0]?.count ?? 0);
}

describe('PgPubSub', () => {
  it('fans a notification out to every subscribed process', async () => {
    const replicaA = new PgPubSub({ connectionString: database.url });
    const replicaB = new PgPubSub({ connectionString: database.url });
    const received: [string, unknown][] = [];
    try {
      await replicaA.subscribe('room:42', (payload) => received.push(['A', payload]));
      await replicaB.subscribe('room:42', (payload) => received.push(['B', payload]));
      await replicaA.subscribe('room:43', (payload) => received.push(['A43', payload]));
      await replicaA.publish(database.pool, 'room:42', { revision: 7 });
      await until(() => received.length === 2);
      expect(received.sort()).toEqual([
        ['A', { revision: 7 }],
        ['B', { revision: 7 }],
      ]);
      await expect(replicaA.publish(database.pool, 'room:42', 'x'.repeat(9000))).rejects.toThrow(
        /exceeds/,
      );
      await expect(replicaA.subscribe('Bad Channel', () => undefined)).rejects.toThrow(/invalid/);
    } finally {
      await replicaA.close();
      await replicaB.close();
    }
  });

  it('stops delivering after unsubscribe', async () => {
    const bus = new PgPubSub({ connectionString: database.url });
    const received: unknown[] = [];
    try {
      const unsubscribe = await bus.subscribe('queue', (p) => received.push(p));
      await bus.publish(database.pool, 'queue', 1);
      await until(() => received.length === 1);
      await unsubscribe();
      await bus.publish(database.pool, 'queue', 2);
      await new Promise((resolve) => setTimeout(resolve, 200));
      expect(received).toEqual([1]);
    } finally {
      await bus.close();
    }
  });

  it('reconnects and listens again after losing its connection', async () => {
    let reconnects = 0;
    const bus = new PgPubSub({
      connectionString: database.url,
      reconnectDelayMs: 20,
      onReconnect: () => reconnects++,
    });
    const received: unknown[] = [];
    try {
      await bus.subscribe('match:1', (p) => received.push(p));
      expect(await terminate('glob2-pubsub')).toBe(1);
      await until(() => reconnects === 1);
      await bus.publish(database.pool, 'match:1', 'after');
      await until(() => received.length === 1);
      expect(received).toEqual(['after']);
    } finally {
      await bus.close();
    }
  });
});

describe('LeaderElection', () => {
  it('elects exactly one leader and fails over when it stops or dies', async () => {
    const leading = new Set<string>();
    let maxConcurrent = 0;
    const make = (id: string) =>
      new LeaderElection({
        connectionString: database.url,
        name: 'matchmaker-test',
        retryMs: 50,
        lead: async (signal) => {
          leading.add(id);
          maxConcurrent = Math.max(maxConcurrent, leading.size);
          await new Promise<void>((resolve) => signal.addEventListener('abort', () => resolve()));
          leading.delete(id);
        },
      });
    const a = make('a');
    const b = make('b');
    try {
      a.start();
      await until(() => leading.size === 1);
      b.start();
      await new Promise((resolve) => setTimeout(resolve, 300));
      expect([...leading]).toEqual(['a']);
      expect(a.isLeader && !b.isLeader).toBe(true);

      // Graceful hand-over.
      await a.stop();
      await until(() => leading.has('b'));

      // Crash: the leader's session dies, Postgres releases the lock.
      a.start();
      await new Promise((resolve) => setTimeout(resolve, 200));
      expect([...leading]).toEqual(['b']);
      expect(await terminate('glob2-leader-matchmaker-test')).toBe(2);
      await until(() => leading.size === 1, 10_000);
      expect(maxConcurrent).toBe(1);
    } finally {
      await a.stop();
      await b.stop();
    }
  });
});
