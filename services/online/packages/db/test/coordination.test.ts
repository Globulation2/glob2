import { afterAll, beforeAll, describe, expect, it, vi } from 'vitest';
import pg from 'pg';
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

  it.each(['connect', 'listen'] as const)(
    'discards failed subscriptions after a %s failure and recovers cleanly',
    async (failure) => {
      const bus = new PgPubSub({ connectionString: database.url });
      const stale = vi.fn(),
        live = vi.fn();
      const end = vi.spyOn(pg.Client.prototype, 'end');
      const fail =
        failure === 'connect'
          ? vi
              .spyOn(pg.Client.prototype, 'connect')
              .mockRejectedValueOnce(new Error('connect failed'))
          : vi
              .spyOn(pg.Client.prototype, 'query')
              .mockRejectedValueOnce(new Error('listen failed'));
      try {
        await expect(bus.subscribe('failed-listen', stale)).rejects.toThrow(`${failure} failed`);
        expect(bus.connected).toBe(false);
        expect(end).toHaveBeenCalledOnce();
        fail.mockRestore();
        const unsubscribe = await bus.subscribe('failed-listen', live);
        await bus.publish(database.pool, 'failed-listen', 'recovered');
        await until(() => live.mock.calls.length === 1);
        expect(stale).not.toHaveBeenCalled();
        await unsubscribe();
        await bus.publish(database.pool, 'failed-listen', 'after-unsubscribe');
        await new Promise((resolve) => setTimeout(resolve, 50));
        expect(live).toHaveBeenCalledOnce();
        expect(stale).not.toHaveBeenCalled();
      } finally {
        fail.mockRestore();
        end.mockRestore();
        await bus.close();
      }
    },
  );

  it.each(['resolve', 'reject'] as const)(
    'shares pending LISTEN readiness with concurrent subscribers: %s',
    async (outcome) => {
      const bus = new PgPubSub({ connectionString: database.url });
      const firstHandler = vi.fn(),
        secondHandler = vi.fn();
      let resolve!: () => void, reject!: (error: Error) => void;
      const pending = new Promise<void>((yes, no) => {
        resolve = yes;
        reject = no;
      });
      await bus.subscribe('warmup', () => undefined);
      const query = vi.spyOn(pg.Client.prototype, 'query').mockImplementationOnce(() => pending);
      try {
        const first = bus.subscribe('concurrent-listen', firstHandler);
        const second = bus.subscribe('concurrent-listen', secondHandler);
        const firstSettled = vi.fn(),
          secondSettled = vi.fn();
        void first.then(firstSettled, firstSettled);
        void second.then(secondSettled, secondSettled);
        await vi.waitFor(() => expect(query).toHaveBeenCalledOnce());
        expect(firstSettled).not.toHaveBeenCalled();
        expect(secondSettled).not.toHaveBeenCalled();
        if (outcome === 'resolve') resolve();
        else reject(new Error('LISTEN unavailable'));
        const results = await Promise.allSettled([first, second]);
        expect(results.map((result) => result.status)).toEqual(
          outcome === 'resolve' ? ['fulfilled', 'fulfilled'] : ['rejected', 'rejected'],
        );
        query.mockRestore();
        if (outcome === 'resolve') {
          for (const result of results) if (result.status === 'fulfilled') await result.value();
        }
        const recovered = vi.fn();
        const remove = await bus.subscribe('concurrent-listen', recovered);
        await bus.publish(database.pool, 'concurrent-listen', 'recovered');
        await until(() => recovered.mock.calls.length === 1);
        expect(firstHandler).not.toHaveBeenCalled();
        expect(secondHandler).not.toHaveBeenCalled();
        await remove();
      } finally {
        query.mockRestore();
        await bus.close();
      }
    },
  );

  it.each(['resolve', 'reject'] as const)(
    'stops reconnecting when an in-flight connection settles after close: %s',
    async (outcome) => {
      vi.useFakeTimers();
      const clients: pg.Client[] = [];
      let resolve!: () => void, reject!: (error: Error) => void;
      const pending = new Promise<void>((yes, no) => {
        resolve = yes;
        reject = no;
      });
      const connect = vi.spyOn(pg.Client.prototype, 'connect').mockImplementation(function (
        this: pg.Client,
      ) {
        clients.push(this);
        return clients.length === 1 ? Promise.resolve() : pending;
      });
      const query = vi.spyOn(pg.Client.prototype, 'query').mockResolvedValue(undefined);
      const end = vi.spyOn(pg.Client.prototype, 'end').mockResolvedValue(undefined);
      const onReconnect = vi.fn();
      const bus = new PgPubSub({
        connectionString: database.url,
        reconnectDelayMs: 20,
        onReconnect,
      });
      try {
        await bus.subscribe('shutdown-race', () => undefined);
        clients[0]!.emit('error', new Error('connection lost'));
        await vi.advanceTimersByTimeAsync(20);
        expect(connect).toHaveBeenCalledTimes(2);
        await bus.close();
        if (outcome === 'resolve') resolve();
        else reject(new Error('reconnect failed'));
        await vi.advanceTimersByTimeAsync(0);
        expect(bus.connected).toBe(false);
        expect(bus.reconnectCount).toBe(0);
        expect(onReconnect).not.toHaveBeenCalled();
        expect(end).toHaveBeenCalledTimes(2);
        expect(vi.getTimerCount()).toBe(0);
        await vi.advanceTimersByTimeAsync(60000);
        expect(connect).toHaveBeenCalledTimes(2);
      } finally {
        await bus.close();
        connect.mockRestore();
        query.mockRestore();
        end.mockRestore();
        vi.useRealTimers();
      }
    },
  );

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
    let leaderships = 0;
    const make = (id: string) =>
      new LeaderElection({
        connectionString: database.url,
        name: 'matchmaker-test',
        retryMs: 50,
        lead: async (signal) => {
          ++leaderships;
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
      // Both dedicated sessions must exist before simulating their crash;
      // a restarted follower can take longer than a fixed sleep to connect.
      await vi.waitFor(
        async () => {
          const result = await database.pool.query<{ count: string }>(
            `SELECT count(*) AS count FROM pg_stat_activity
             WHERE application_name = $1 AND datname = current_database()`,
            ['glob2-leader-matchmaker-test'],
          );
          expect(Number(result.rows[0]?.count ?? 0)).toBe(2);
        },
        { timeout: 10_000, interval: 20 },
      );
      expect([...leading]).toEqual(['b']);
      const beforeCrash = leaderships;
      expect(await terminate('glob2-leader-matchmaker-test')).toBe(2);
      await until(() => leading.size === 1 && leaderships > beforeCrash, 10_000);
      expect(maxConcurrent).toBe(1);
    } finally {
      await a.stop();
      await b.stop();
    }
  });
});
