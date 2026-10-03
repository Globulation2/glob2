// Abuse limits that must hold across API replicas: shared sliding-window
// counters, password lockout per username and per address, browser sign-in
// attempts, and uploads checked against the quota before they are unpacked.
import { gzipSync } from 'node:zlib';
import { afterAll, beforeAll, beforeEach, describe, expect, it } from 'vitest';
import { sql } from 'kysely';
import { DEFAULT_INSTANCE_CONFIG, MAX_GZIP_RATIO, checkMapFileAsync } from '@glob2/core';
import { SharedLimit } from '../src/http/rateLimits.ts';
import { simVersionKey } from '@glob2/protocol';
import { serveSim } from './playSupport.ts';
import {
  RealtimeClient,
  SIM,
  createHarness,
  postJson,
  type Harness,
  type Instance,
} from './support.ts';

let harness: Harness;
let a: Instance;
let b: Instance;
const ORIGIN = 'http://abuse.test';

const limits = {
  authPerMinute: 10_000,
  guestsPerHour: 10_000,
  signinAttemptsPerHour: 2,
  signinAttemptsPerMinuteTotal: 10_000,
  passwordFailuresPerAccount: 3,
  passwordFailuresPerIp: 5,
};

beforeAll(async () => {
  harness = await createHarness();
  const instance = {
    limits,
    auth: { ...DEFAULT_INSTANCE_CONFIG.auth, local: { enabled: true, allowRegistration: true } },
  };
  // Two replicas of one instance on one database.
  a = await harness.start({ origin: ORIGIN, instance });
  b = await harness.start({ origin: ORIGIN, instance });
});

afterAll(async () => {
  await harness?.close();
});

beforeEach(async () => {
  await harness.database.db.deleteFrom('rate_limits').execute();
});

async function register(instance: Instance, username: string) {
  const response = await postJson(`${instance.url}/api/v1/auth/local/register`, {
    username,
    password: 'correct horse battery',
    platform: 'desktop',
  });
  expect(response.status).toBe(200);
}

async function signIn(instance: Instance, username: string, password: string) {
  const response = await postJson(`${instance.url}/api/v1/auth/local/sign-in`, {
    username,
    password,
    platform: 'desktop',
  });
  return { status: response.status, body: (await response.json()) as { code?: string } };
}

describe('shared limits', () => {
  it('count a sliding window in Postgres', async () => {
    const db = harness.database.db;
    const limit = new SharedLimit(db, 'test-window', 3, 60_000);
    const taken = [];
    for (let i = 0; i < 4; i++) taken.push((await limit.take('k')).allowed);
    expect(taken).toEqual([true, true, true, false]);
    expect(await limit.exhausted('k')).toBe(true);
    expect(await limit.exhausted('other')).toBe(false);
    // Half a window later, half of the previous window still counts: 3 × 0.5.
    await sql`UPDATE rate_limits SET window_start = window_start - interval '90 seconds'
              WHERE bucket = 'test-window'`.execute(harness.database.as('migrator').db);
    const after = await limit.take('k');
    expect(after.allowed).toBe(true);
    expect(after.estimate).toBeCloseTo(2.5, 1);
    await limit.reset('k');
    expect((await limit.take('k')).estimate).toBe(1);
  });
});

describe('local passwords', () => {
  it('lock a username after repeated failures on any replica, even for the right password', async () => {
    await register(a, 'victim_1');
    const failures = [];
    for (const instance of [a, b, a]) {
      failures.push((await signIn(instance, 'victim_1', 'wrong password!')).status);
    }
    expect(failures).toEqual([401, 401, 401]);
    const locked = await signIn(b, 'victim_1', 'correct horse battery');
    expect(locked.status).toBe(429);
    expect(locked.body.code).toBe('rate_limited');
    // Once the window has passed, the right password works and clears the count.
    await sql`UPDATE rate_limits SET window_start = window_start - interval '1 hour'
              WHERE bucket = 'password-fail-account'`.execute(harness.database.as('migrator').db);
    expect((await signIn(a, 'victim_1', 'correct horse battery')).status).toBe(200);
    expect(
      await harness.database.db
        .selectFrom('rate_limits')
        .select('key')
        .where('bucket', '=', 'password-fail-account')
        .execute(),
    ).toEqual([]);
  });

  it('lock an address that guesses many usernames', async () => {
    const statuses = [];
    for (let i = 0; i < 6; i++) {
      statuses.push((await signIn(i % 2 ? a : b, `nobody_${i}`, 'guess guess guess')).status);
    }
    expect(statuses).toEqual([401, 401, 401, 401, 401, 429]);
  });

  it('lock the browser sign-in form the same way', async () => {
    await register(a, 'victim_2');
    const form = (password: string) =>
      fetch(`${b.url}/signin/local`, {
        method: 'POST',
        headers: { 'content-type': 'application/x-www-form-urlencoded', origin: ORIGIN },
        body: new URLSearchParams({ action: 'signin', username: 'victim_2', password }),
      });
    for (let i = 0; i < 3; i++) expect((await form('not the password')).status).toBe(401);
    const page = await form('correct horse battery');
    expect(page.status).toBe(429);
    expect(await page.text()).toMatch(/Too many wrong passwords/);
  });
});

describe('browser sign-in attempts', () => {
  it('are limited per address on every replica', async () => {
    const results = [];
    for (const instance of [a, b, a]) {
      const client = await RealtimeClient.connect(instance.url);
      await client.hello();
      results.push((await client.call('auth.handoff.begin', {})).error?.code ?? 'ok');
      client.close();
    }
    expect(results).toEqual(['ok', 'ok', 'rate_limited']);
    expect(
      Number(
        (
          await harness.database.db
            .selectFrom('signin_attempts')
            .select((eb) => eb.fn.countAll().as('n'))
            .executeTakeFirstOrThrow()
        ).n,
      ),
    ).toBe(2);
  });
});

describe('uploads', () => {
  it('refuse decompression bombs by ratio, and keep real compressed maps', async () => {
    // 64 MiB of zeros packs into ~64 KiB: far beyond any game file's ratio.
    const bomb = gzipSync(Buffer.alloc(64 * 1024 * 1024));
    expect(bomb.length * MAX_GZIP_RATIO).toBeLessThan(64 * 1024 * 1024);
    const check = await checkMapFileAsync(bomb, { format: 'map', newestVersionMinor: 200 });
    expect(check).toMatchObject({ ok: false, problem: 'too_large' });
    const corrupt = await checkMapFileAsync(Buffer.from([0x1f, 0x8b, 1, 2, 3]), {
      format: 'map',
      newestVersionMinor: 200,
    });
    expect(corrupt).toMatchObject({ ok: false, problem: 'corrupt_gzip' });
  });

  it('take the quota before unpacking, on every replica', async () => {
    await serveSim(harness.database.db);
    const guest = (await (
      await postJson(`${a.url}/api/v1/auth/guest`, { platform: 'desktop' })
    ).json()) as { tokens: { accessToken: string } };
    const bomb = gzipSync(Buffer.alloc(32 * 1024 * 1024));
    const statuses: number[] = [];
    for (let i = 0; i < 31; i++) {
      const response = await fetch(
        `${(i % 2 ? a : b).url}/api/v1/uploads?format=map&simVersion=${simVersionKey(SIM)}`,
        {
          method: 'POST',
          headers: {
            authorization: `Bearer ${guest.tokens.accessToken}`,
            'content-type': 'application/octet-stream',
          },
          body: bomb,
        },
      );
      statuses.push(response.status);
    }
    // Every bomb is refused unpacked-too-large and still spends the hourly
    // quota (30), so the 31st is not even unpacked.
    expect(statuses.slice(0, 30)).toEqual(Array(30).fill(400));
    expect(statuses[30]).toBe(429);
  });
});
