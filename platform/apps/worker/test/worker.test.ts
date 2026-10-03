import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { createTestDatabase, type TestDatabase } from '@glob2/db/testing';
import { createLogger } from '@glob2/core';
import { runMaintenance } from '../src/maintenance.ts';
import { runScheduler } from '../src/scheduler.ts';

const SIM = `125-49-${'ab'.repeat(32)}`;
let database: TestDatabase;

beforeAll(async () => {
  database = await createTestDatabase({ role: 'worker' });
});

afterAll(async () => {
  await database?.drop();
});

describe('maintenance', () => {
  it('expires stale sign-ins and queue tickets and purges old refresh tokens', async () => {
    // Fixtures are written as the API writes them; maintenance runs as the worker.
    const db = database.as('api').db;
    const account = await db
      .insertInto('accounts')
      .values({ kind: 'guest', display_name: 'Guest 1' })
      .returning('id')
      .executeTakeFirstOrThrow();
    const other = await db
      .insertInto('accounts')
      .values({ kind: 'guest', display_name: 'Guest 2' })
      .returning('id')
      .executeTakeFirstOrThrow();
    const hour = 3_600_000;
    await db
      .insertInto('signin_attempts')
      .values([
        { confirmation_code: 'OLD111', expires_at: new Date(Date.now() - hour) },
        { confirmation_code: 'NEW222', expires_at: new Date(Date.now() + hour) },
        {
          confirmation_code: 'GONE33',
          created_at: new Date(Date.now() - 8 * 24 * hour - hour),
          expires_at: new Date(Date.now() - 8 * 24 * hour),
        },
      ])
      .execute();
    await db
      .insertInto('rate_limits')
      .values([
        { bucket: 'auth:guest', key: '192.0.2.1', window_start: new Date(Date.now() - 25 * hour) },
        { bucket: 'auth:guest', key: '192.0.2.2', window_start: new Date() },
      ])
      .execute();
    await db
      .insertInto('queue_tickets')
      .values([
        {
          queue_id: 'q',
          account_id: account.id,
          sim_version: SIM,
          created_at: new Date(Date.now() - 2 * hour),
        },
        { queue_id: 'q', account_id: other.id, sim_version: SIM },
      ])
      .execute();
    await db
      .insertInto('refresh_tokens')
      .values([
        {
          account_id: account.id,
          family_id: crypto.randomUUID(),
          token_hash: '11'.repeat(32),
          expires_at: new Date(Date.now() - 40 * 24 * hour),
        },
        {
          account_id: account.id,
          family_id: crypto.randomUUID(),
          token_hash: '22'.repeat(32),
          expires_at: new Date(Date.now() - 24 * hour),
        },
      ])
      .execute();

    await db
      .insertInto('auth_flows')
      .values(
        [2, -48].map((hours, i) => ({
          state_hash: String(i + 3).repeat(64),
          provider: 'google',
          code_verifier: 'v',
          nonce: 'n',
          purpose: 'web' as const,
          expires_at: new Date(Date.now() + hours * hour),
        })),
      )
      .execute();
    await db
      .insertInto('web_sessions')
      .values([
        {
          account_id: account.id,
          token_hash: '55'.repeat(32),
          expires_at: new Date(Date.now() + hour),
        },
        {
          account_id: account.id,
          token_hash: '66'.repeat(32),
          expires_at: new Date(Date.now() - 31 * 24 * hour),
        },
      ])
      .execute();

    expect(await runMaintenance(database.db)).toMatchObject({
      expiredSigninAttempts: 2,
      expiredQueueTickets: 1,
      deletedRefreshTokens: 1,
      deletedAuthFlows: 1,
      deletedWebSessions: 1,
      deletedSigninAttempts: 1,
      deletedRateLimits: 1,
      deletedGuests: 0,
    });
    expect(await runMaintenance(database.db)).toMatchObject({
      expiredSigninAttempts: 0,
      expiredQueueTickets: 0,
      deletedRefreshTokens: 0,
      deletedAuthFlows: 0,
      deletedWebSessions: 0,
      deletedSigninAttempts: 0,
      deletedRateLimits: 0,
    });
  });
});

describe('scheduler', () => {
  it('repeats tasks until aborted and survives failures', async () => {
    const controller = new AbortController();
    let ok = 0;
    let failing = 0;
    const done = runScheduler(
      [
        { name: 'ok', intervalMs: 10, run: async () => ok++ },
        {
          name: 'failing',
          intervalMs: 10,
          run: async () => {
            failing++;
            throw new Error('boom');
          },
        },
      ],
      controller.signal,
      createLogger('test', 'silent'),
    );
    await new Promise((resolve) => setTimeout(resolve, 100));
    controller.abort();
    await done;
    expect(ok).toBeGreaterThan(2);
    expect(failing).toBeGreaterThan(2);
  });
});
