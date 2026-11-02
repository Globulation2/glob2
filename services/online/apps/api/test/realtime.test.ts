// The realtime socket and browser sign-in handoff, across two API replicas of
// one instance sharing a database, with a mock OpenID Connect issuer.
import { generateKeyPairSync } from 'node:crypto';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import WebSocket from 'ws';
import type { InstanceConfig } from '@glob2/core';
import { checkDocument as check } from '@glob2/protocol';
import { MockIssuer, verifyEs256 } from './mockIssuer.ts';
import {
  Browser,
  RealtimeClient,
  createHarness,
  json,
  postJson,
  type Harness,
  type Instance,
} from './support.ts';

const ORIGIN = 'http://play.test';
const apple = generateKeyPairSync('ec', { namedCurve: 'P-256' });

let harness: Harness;
let issuer: MockIssuer;
let a: Instance;
let b: Instance;

function instanceConfig(): Partial<InstanceConfig> {
  return {
    auth: {
      providers: [
        {
          id: 'mock',
          kind: 'oidc',
          displayName: 'Mock ID',
          issuer: issuer.issuer,
          clientId: 'glob2-test',
          clientSecretEnv: 'MOCK_SECRET',
          allowInsecureIssuer: true,
        },
        {
          id: 'apple',
          kind: 'apple',
          displayName: 'Apple',
          issuer: issuer.issuer,
          clientId: 'org.glob2.signin',
          teamId: 'TEAM123456',
          keyId: 'APPLEKEY01',
          privateKeyEnv: 'APPLE_KEY',
          allowInsecureIssuer: true,
        },
      ],
      local: { enabled: true },
    },
    limits: { authPerMinute: 1000, guestsPerHour: 1000 },
  };
}

beforeAll(async () => {
  issuer = new MockIssuer();
  await issuer.start();
  issuer.clients.set('glob2-test', { secret: 's3cret' });
  issuer.clients.set('org.glob2.signin', {
    verifySecret: (secret) => {
      const jwt = verifyEs256(secret, apple.publicKey);
      return (
        jwt?.header['alg'] === 'ES256' &&
        jwt.header['kid'] === 'APPLEKEY01' &&
        jwt.claims['iss'] === 'TEAM123456' &&
        jwt.claims['sub'] === 'org.glob2.signin' &&
        jwt.claims['aud'] === issuer.issuer
      );
    },
  });
  harness = await createHarness();
  const secrets = {
    MOCK_SECRET: 's3cret',
    APPLE_KEY: apple.privateKey.export({ format: 'pem', type: 'pkcs8' }).toString(),
  };
  // Two replicas of one instance: same origin, same database, separate pub/sub.
  a = await harness.start({
    origin: ORIGIN,
    instance: instanceConfig(),
    secrets,
  });
  b = await harness.start({ origin: ORIGIN, instance: instanceConfig(), secrets });
});

afterAll(async () => {
  await harness?.close();
  await issuer?.stop();
});

async function guest(instance: Instance = a) {
  return (await json(
    await postJson(`${instance.url}/api/v1/auth/guest`, { platform: 'desktop' }),
  )) as {
    account: { id: string; displayName: string };
    tokens: { accessToken: string; refreshToken: string };
  };
}

/** Socket on replica A, signed in as a fresh guest. */
async function guestSocket() {
  const session = await guest();
  const client = await RealtimeClient.connect(a.url);
  const hello = await client.hello(session.tokens.accessToken);
  expect((hello['account'] as { id: string }).id).toBe(session.account.id);
  return { client, session };
}

/** A browser that reaches the instance through replica B, on the game's network. */
const browserViaB = () => new Browser({ [ORIGIN]: b.url });

/** The same, from another network (a phone on mobile data, or someone sent a link). */
const browserElsewhere = () => new Browser({ [ORIGIN]: b.url }, '203.0.113.7');

async function beginHandoff(client: RealtimeClient, params: object = {}) {
  const result = await client.ok('auth.handoff.begin', params);
  expect(check('RealtimeAuthHandoffBeginResult', result).stage).toBe('ok');
  return result as {
    attemptId: string;
    signInUrl: string;
    confirmationCode: string;
    resumeToken: string;
  };
}

/**
 * Opens the sign-in link as the player does. On the game's network it binds
 * at once; elsewhere the page shows the game's code and the player confirms
 * with one click. Returns where the bound browser is sent next.
 */
async function openLink(browser: Browser, signInUrl: string, code?: string) {
  const attempt = new URL(signInUrl).searchParams.get('attempt')!;
  const opened = await browser.get(signInUrl);
  if (opened.status === 303) return opened.headers.get('location')!;
  expect(opened.status).toBe(200);
  const page = await opened.text();
  expect(page).toContain('Yes, continue');
  const shown = /<p class="code"><strong>([^<]+)<\/strong>/.exec(page)![1]!;
  const confirm = await browser.post(`${ORIGIN}/signin/confirm`, {
    attempt,
    code: code ?? shown,
  });
  expect(confirm.status).toBe(303);
  return confirm.headers.get('location')!;
}

/** Runs the provider sign-in in the browser; returns the final page. */
async function providerSignIn(browser: Browser, signInUrl: string, provider = 'mock') {
  const attempt = new URL(signInUrl).searchParams.get('attempt')!;
  const next = await openLink(browser, signInUrl);
  // Straight to the provider picked in the game, or the page that offers them all.
  let html: string | undefined;
  if (next.startsWith('/signin?')) {
    const page = await browser.get(`${ORIGIN}${next}`);
    expect(page.status).toBe(200);
    html = await page.text();
  } else {
    expect(next).toBe(`/auth/${provider}/start?attempt=${attempt}`);
  }
  const start = await browser.get(`${ORIGIN}/auth/${provider}/start?attempt=${attempt}`);
  expect(start.status).toBe(302);
  const authorize = await fetch(start.headers.get('location')!, { redirect: 'manual' });
  if (authorize.status === 200) {
    // form_post: the provider's page posts the fields to the callback, cross-site.
    const { action, fields } = (await authorize.json()) as {
      action: string;
      fields: Record<string, string>;
    };
    const final = await browser.post(action, fields, issuer.issuer);
    return { html, final, text: await final.text() };
  }
  expect(authorize.status).toBe(302);
  const callback = await browser.get(authorize.headers.get('location')!);
  return { html, final: callback, text: await callback.text() };
}

describe('realtime envelope', () => {
  it('requires session.hello first and validates requests', async () => {
    const client = await RealtimeClient.connect(a.url);
    const early = await client.call('session.ping');
    expect(early.ok).toBe(false);
    expect(early.error?.code).toBe('bad_request');

    const hello = await client.hello();
    expect(check('RealtimeSessionHelloResult', hello).stage).toBe('ok');
    expect(hello['account']).toBeUndefined();
    expect(hello['simSupported']).toBe(false);
    // No engine agent has reported in, so the instance serves no sim version.
    expect(hello['supportedSimVersions']).toEqual([]);

    const ping = await client.ok('session.ping');
    expect(typeof ping['serverTime']).toBe('string');
    const unknown = await client.call('nothing.here');
    expect(unknown.error?.code).toBe('bad_request');
    const badParams = await client.call('auth.handoff.cancel', { attemptId: 'nope' });
    expect(badParams.error?.code).toBe('bad_request');
    // Rooms need a signed-in socket (tests/rooms.test.ts covers the rest).
    const later = await client.call('room.create', { name: 'x', visibility: 'public' });
    expect(later.error?.code).toBe('unauthenticated');
    for (const frame of [early, unknown, later])
      expect(check('RealtimeServerMessage', frame).stage).toBe('ok');

    client.sendRaw('{not json');
    expect((await client.waitClosed())?.code).toBe(1007);
  });

  it('authenticates with access tokens and rejects bad ones', async () => {
    const session = await guest();
    const client = await RealtimeClient.connect(a.url);
    const bad = await client.call('session.hello', {
      protocol: 1,
      client: {
        platform: 'desktop',
        version: 't',
        simVersion: { versionMinor: 1, netProtocol: 1, dataHash: '0'.repeat(64) },
      },
      accessToken: 'eyJ.bad.token',
    });
    expect(bad.error?.code).toBe('unauthenticated');
    await client.hello();
    const auth = await client.ok('session.authenticate', {
      accessToken: session.tokens.accessToken,
    });
    expect((auth['account'] as { id: string }).id).toBe(session.account.id);
    client.close();
  });

  it('checks Origin on upgrade', async () => {
    await expect(RealtimeClient.connect(a.url, { origin: 'https://evil.example' })).rejects.toThrow(
      /403/,
    );
    const same = await RealtimeClient.connect(a.url, { origin: ORIGIN });
    await same.hello();
    same.close();
    // Native clients send no Origin.
    const native = await RealtimeClient.connect(a.url);
    await native.hello();
    native.close();
  });

  it('rate-limits messages per socket', async () => {
    const limited = await harness.start({
      instance: { limits: { realtimePerSecond: 1, realtimeBurst: 3 } },
    });
    const client = await RealtimeClient.connect(limited.url);
    await client.hello();
    const results = await Promise.all(Array.from({ length: 6 }, () => client.call('session.ping')));
    const codes = results.map((r) => (r.ok ? 'ok' : r.error?.code));
    expect(codes.filter((c) => c === 'ok').length).toBe(2);
    expect(codes.filter((c) => c === 'rate_limited').length).toBe(4);
    client.close();
  });

  it('drops sockets that stop answering pings', async () => {
    // Only this case needs an accelerated heartbeat. Keep ordinary requests
    // on production timing so a busy test host cannot reap healthy sockets.
    const fast = await harness.start({ build: { realtime: { heartbeatMs: 200 } } });
    const socket = new WebSocket(`${fast.url.replace('http', 'ws')}/realtime`, { autoPong: false });
    const closed = new Promise<number>((resolve) => socket.on('close', (code) => resolve(code)));
    await new Promise((resolve) => socket.once('open', resolve));
    expect(await closed).toBe(1006);
  });
});

describe('cross-replica fan-out', () => {
  it('delivers session.revoked to a socket on another replica', async () => {
    const { client, session } = await guestSocket();
    const out = await postJson(`${b.url}/api/v1/auth/sign-out`, {
      refreshToken: session.tokens.refreshToken,
    });
    expect(out.status).toBe(204);
    const event = await client.event('session.revoked');
    expect(check('RealtimeEventSessionRevoked', event).stage).toBe('ok');
    // The socket is signed out but stays open.
    const after = await client.call('auth.handoff.cancel', {
      attemptId: '00000000-0000-4000-8000-000000000000',
    });
    expect(after.error?.code).toBe('not_found');
    client.close();
  });
});

describe('browser sign-in handoff', () => {
  it('links a provider to the guest in place, completing on the other replica', async () => {
    issuer.nextUser = { sub: 'mock-user-1', email: 'alice@example.org', name: 'Alice Example' };
    const { client, session } = await guestSocket();
    const attempt = await beginHandoff(client, { provider: 'mock' });
    expect(attempt.signInUrl).toBe(`${ORIGIN}/signin?attempt=${attempt.attemptId}`);
    expect(attempt.confirmationCode).toMatch(/^[A-HJKMNP-Z2-9]{6}$/);

    const browser = browserViaB();
    const { html, final, text } = await providerSignIn(browser, attempt.signInUrl);
    // The game picked the provider, and the browser is on its network: no page in between.
    expect(html).toBeUndefined();
    expect(final.status).toBe(200);
    expect(text).toContain('Alice Example');
    expect(final.headers.get('content-security-policy')).toContain("default-src 'none'");

    const completed = await client.event('auth.handoff.completed');
    expect(check('RealtimeEventAuthHandoffCompleted', completed).stage).toBe('ok');
    expect(completed['linked']).toBe(true);
    const signed = completed['session'] as {
      account: {
        id: string;
        kind: string;
        displayName: string;
        identities: { provider: string; email?: string }[];
      };
      tokens: { accessToken: string; refreshToken: string };
    };
    expect(signed.account).toMatchObject({
      id: session.account.id,
      kind: 'registered',
      displayName: 'Alice Example',
    });
    expect(signed.account.identities).toEqual([
      expect.objectContaining({ provider: 'mock', email: 'alice@example.org' }),
    ]);
    // PKCE and client authentication reached the issuer.
    const tokenRequest = issuer.tokenRequests.at(-1)!;
    expect(tokenRequest.get('code_verifier')).toBeTruthy();
    expect(tokenRequest.get('client_secret')).toBe('s3cret');

    // The pushed tokens work, and the browser got a web session cookie.
    const me = await fetch(`${a.url}/api/v1/accounts/me`, {
      headers: { authorization: `Bearer ${signed.tokens.accessToken}` },
    });
    expect((await json(me))['kind']).toBe('registered');
    expect(browser.cookie('play.test', 'glob2_session')).toBeTruthy();
    const refreshed = await postJson(`${b.url}/api/v1/auth/refresh`, {
      refreshToken: signed.tokens.refreshToken,
    });
    expect(refreshed.status).toBe(200);

    // The link is single-use.
    const reuse = await browser.get(attempt.signInUrl);
    expect(reuse.status).toBe(410);
    client.close();
  });

  it('reports an identity owned by another account as a conflict, then switches on request', async () => {
    issuer.nextUser = { sub: 'mock-owner', name: 'Owner' };
    const owner = await RealtimeClient.connect(a.url);
    await owner.hello();
    const first = await beginHandoff(owner, { provider: 'mock' });
    await providerSignIn(browserViaB(), first.signInUrl);
    const ownerAccount = (
      (await owner.event('auth.handoff.completed'))['session'] as { account: { id: string } }
    ).account.id;

    // A different guest tries to link the same identity: never merged.
    const { client, session } = await guestSocket();
    const attempt = await beginHandoff(client);
    const browser = browserViaB();
    const { final, text } = await providerSignIn(browser, attempt.signInUrl);
    expect(final.status).toBe(409);
    expect(text).toContain('Owner');
    const cancel = await browser.post(`${ORIGIN}/signin/choose`, {
      attempt: attempt.attemptId,
      choice: 'cancel',
    });
    expect(cancel.status).toBe(200);
    const failed = await client.event('auth.handoff.failed');
    expect(check('RealtimeEventAuthHandoffFailed', failed).stage).toBe('ok');
    expect(failed).toMatchObject({
      reason: 'conflict',
      conflict: { reason: 'identity_in_use', provider: 'mock', account: { id: ownerAccount } },
    });

    // Asking again and choosing to switch hands the socket the owner's account.
    const again = await beginHandoff(client);
    const browser2 = browserViaB();
    await providerSignIn(browser2, again.signInUrl);
    // A cross-site form post is refused.
    const forged = await browser2.post(
      `${ORIGIN}/signin/choose`,
      { attempt: again.attemptId, choice: 'switch' },
      'https://evil.example',
    );
    expect(forged.status).toBe(403);
    await browser2.post(`${ORIGIN}/signin/choose`, { attempt: again.attemptId, choice: 'switch' });
    const switched = await client.event('auth.handoff.completed');
    expect(switched['linked']).toBe(false);
    expect((switched['session'] as { account: { id: string } }).account.id).toBe(ownerAccount);

    // The guest account was left untouched.
    const guestRow = await harness.database.db
      .selectFrom('accounts')
      .select('kind')
      .where('id', '=', session.account.id)
      .executeTakeFirstOrThrow();
    expect(guestRow.kind).toBe('guest');
    owner.close();
    client.close();
  });

  it('asks a browser on another network to confirm the code, and binds the first browser', async () => {
    const { client } = await guestSocket();
    const attempt = await beginHandoff(client, { provider: 'mock' });
    const victim = browserElsewhere();
    // A link alone (say, one a phisher sent) shows the code and offers nothing else.
    const opened = await victim.get(attempt.signInUrl);
    expect(opened.status).toBe(200);
    const page = await opened.text();
    expect(page).toContain(
      `${attempt.confirmationCode.slice(0, 3)} · ${attempt.confirmationCode.slice(3)}`,
    );
    expect(page).toContain('If someone sent you this link');
    expect(page).not.toContain('/auth/mock/start');
    expect(page).not.toContain('action="/signin/local"');
    expect(
      (await victim.get(`${ORIGIN}/auth/mock/start?attempt=${attempt.attemptId}`)).status,
    ).toBe(410);
    expect(
      (
        await victim.post(`${ORIGIN}/signin/local`, {
          attempt: attempt.attemptId,
          username: 'phished',
          password: 'a long enough password',
          action: 'register',
        })
      ).status,
    ).toBe(410);
    // A cross-site form cannot confirm, even with the code.
    expect(
      (
        await victim.post(
          `${ORIGIN}/signin/confirm`,
          { attempt: attempt.attemptId, code: attempt.confirmationCode },
          'https://evil.example',
        )
      ).status,
    ).toBe(403);

    // One click, and on to the provider the game picked.
    const player = browserElsewhere();
    expect(await openLink(player, attempt.signInUrl)).toBe(
      `/auth/mock/start?attempt=${attempt.attemptId}`,
    );
    // Any other browser, on any network, is now refused.
    for (const other of [browserViaB(), browserElsewhere()]) {
      expect((await other.get(attempt.signInUrl)).status).toBe(409);
      expect(
        (
          await other.post(`${ORIGIN}/signin/confirm`, {
            attempt: attempt.attemptId,
            code: attempt.confirmationCode,
          })
        ).status,
      ).toBe(409);
      expect(
        (await other.get(`${ORIGIN}/auth/mock/start?attempt=${attempt.attemptId}`)).status,
      ).toBe(410);
    }
    // Coming back to the link (say, after backing out of the provider) offers the choices.
    const back = await player.get(attempt.signInUrl);
    expect(back.status).toBe(200);
    expect(await back.text()).toContain('/auth/mock/start');
    client.close();
  });

  it('binds a browser on the game network at once, refusing a later one', async () => {
    const { client } = await guestSocket();
    const attempt = await beginHandoff(client);
    const player = browserViaB();
    expect(await openLink(player, attempt.signInUrl)).toBe(`/signin?attempt=${attempt.attemptId}`);
    const page = await (await player.get(attempt.signInUrl)).text();
    expect(page).toContain('Choose how to sign in');
    expect(page).toContain('action="/signin/local"');
    expect((await browserViaB().get(attempt.signInUrl)).status).toBe(409);
    client.close();
  });

  it('hands the game the account this browser is already signed in to', async () => {
    issuer.nextUser = { sub: 'mock-returning', name: 'Returning Player' };
    const first = await RealtimeClient.connect(a.url);
    await first.hello();
    const browser = browserViaB();
    await providerSignIn(browser, (await beginHandoff(first, { provider: 'mock' })).signInUrl);
    const account = (
      (await first.event('auth.handoff.completed'))['session'] as { account: { id: string } }
    ).account.id;
    first.close();

    // A new game signs in with no provider picked: one click on the browser's session.
    const game = await RealtimeClient.connect(a.url);
    await game.hello();
    const attempt = await beginHandoff(game);
    expect(await openLink(browser, attempt.signInUrl)).toBe(`/signin?attempt=${attempt.attemptId}`);
    const page = await (await browser.get(attempt.signInUrl)).text();
    expect(page).toContain('Continue as Returning Player');
    // Not from another site, nor from a browser the attempt is not bound to.
    expect(
      (
        await browser.post(
          `${ORIGIN}/signin/continue`,
          { attempt: attempt.attemptId },
          'https://evil.example',
        )
      ).status,
    ).toBe(403);
    expect(
      (await browserViaB().post(`${ORIGIN}/signin/continue`, { attempt: attempt.attemptId }))
        .status,
    ).toBe(410);
    const done = await browser.post(`${ORIGIN}/signin/continue`, { attempt: attempt.attemptId });
    expect(done.status).toBe(200);
    expect(await done.text()).toContain('Returning Player');
    const completed = await game.event('auth.handoff.completed');
    expect((completed['session'] as { account: { id: string } }).account.id).toBe(account);
    game.close();
  });

  it('stops a sign-in after too many wrong codes', async () => {
    const { client } = await guestSocket();
    const attempt = await beginHandoff(client);
    const guesser = browserElsewhere();
    const statuses = [];
    for (let i = 0; i < 5; i++) {
      statuses.push(
        (
          await guesser.post(`${ORIGIN}/signin/confirm`, {
            attempt: attempt.attemptId,
            code: 'AAAAAA',
          })
        ).status,
      );
    }
    expect(statuses).toEqual([400, 400, 400, 400, 410]);
    // Even the right code is refused now, and the game hears that it failed.
    expect(
      (
        await guesser.post(`${ORIGIN}/signin/confirm`, {
          attempt: attempt.attemptId,
          code: attempt.confirmationCode,
        })
      ).status,
    ).toBe(410);
    expect((await client.event('auth.handoff.failed'))['reason']).toBe('denied');
    client.close();
  });

  it('reports a refusal at the provider and a cancellation from the client', async () => {
    issuer.nextUser = null;
    const { client } = await guestSocket();
    const attempt = await beginHandoff(client);
    await providerSignIn(browserViaB(), attempt.signInUrl);
    expect((await client.event('auth.handoff.failed'))['reason']).toBe('denied');

    const second = await beginHandoff(client);
    await client.ok('auth.handoff.cancel', { attemptId: second.attemptId });
    expect(await client.event('auth.handoff.failed')).toMatchObject({
      attemptId: second.attemptId,
      reason: 'cancelled',
    });
    client.close();
  });

  it('resumes on a new socket after the original one dropped', async () => {
    issuer.nextUser = { sub: 'mock-phone', name: 'Phone Player' };
    const first = await RealtimeClient.connect(a.url);
    await first.hello();
    const attempt = await beginHandoff(first);
    first.close();
    await first.waitClosed();

    const browser = browserViaB();
    await providerSignIn(browser, attempt.signInUrl);

    const second = await RealtimeClient.connect(b.url);
    await second.hello();
    const wrong = await second.call('auth.handoff.resume', {
      attemptId: attempt.attemptId,
      resumeToken: 'A'.repeat(43),
    });
    expect(wrong.error?.code).toBe('not_found');
    const resumed = await second.ok('auth.handoff.resume', {
      attemptId: attempt.attemptId,
      resumeToken: attempt.resumeToken,
    });
    expect(resumed['status']).toBe('finished');
    const completed = await second.event('auth.handoff.completed');
    expect((completed['session'] as { account: { displayName: string } }).account.displayName).toBe(
      'Phone Player',
    );
    // Delivered exactly once.
    const replay = await second.call('auth.handoff.resume', {
      attemptId: attempt.attemptId,
      resumeToken: attempt.resumeToken,
    });
    expect(replay.error?.code).toBe('not_found');
    second.close();
  });

  it('tells a resuming socket that its attempt expired', async () => {
    const first = await RealtimeClient.connect(a.url);
    await first.hello();
    const attempt = await beginHandoff(first);
    first.close();
    await first.waitClosed();
    // What the worker's maintenance does to attempts past their expiry.
    await harness.database.db
      .updateTable('signin_attempts')
      .set({
        status: 'expired',
        failure_reason: 'expired',
        expires_at: new Date(Date.now() - 1000),
      })
      .where('id', '=', attempt.attemptId)
      .execute();
    expect((await browserViaB().get(attempt.signInUrl)).status).toBe(410);
    const second = await RealtimeClient.connect(a.url);
    await second.hello();
    await second.ok('auth.handoff.resume', {
      attemptId: attempt.attemptId,
      resumeToken: attempt.resumeToken,
    });
    expect((await second.event('auth.handoff.failed'))['reason']).toBe('expired');
    second.close();
  });

  it('signs in with Sign in with Apple over form_post with a JWT client secret', async () => {
    issuer.nextUser = { sub: '001234.apple.subject', email: 'relay@privaterelay.appleid.com' };
    issuer.formPostExtra = {
      user: JSON.stringify({ name: { firstName: 'Ada', lastName: 'Lovelace' } }),
    };
    const client = await RealtimeClient.connect(a.url);
    await client.hello();
    const attempt = await beginHandoff(client, { provider: 'apple' });
    const { final } = await providerSignIn(browserViaB(), attempt.signInUrl, 'apple');
    expect(final.status).toBe(200);
    const completed = await client.event('auth.handoff.completed');
    const account = (
      completed['session'] as {
        account: { displayName: string; identities: { provider: string }[] };
      }
    ).account;
    expect(account.displayName).toBe('Ada Lovelace');
    expect(account.identities.map((i) => i.provider)).toEqual(['apple']);
    const tokenRequest = issuer.tokenRequests.at(-1)!;
    expect(tokenRequest.get('client_secret')!.split('.')).toHaveLength(3);
    issuer.formPostExtra = {};
    client.close();
  });

  it('signs in with a local password on the sign-in page', async () => {
    const { client, session } = await guestSocket();
    const attempt = await beginHandoff(client);
    const browser = browserViaB();
    const next = await openLink(browser, attempt.signInUrl);
    const page = await (await browser.get(`${ORIGIN}${next}`)).text();
    expect(page).toContain('Create account');
    const short = await browser.post(`${ORIGIN}/signin/local`, {
      attempt: attempt.attemptId,
      username: 'webuser',
      password: 'short',
      action: 'register',
    });
    expect(short.status).toBe(400);
    // Not a dead end: the same page again, still for this sign-in, the problem
    // on the password field and the username kept (never the password).
    const shortPage = await short.text();
    expect(shortPage).toContain('Choose how to sign in');
    expect(shortPage).toContain(
      'This password is too short: it has 5 characters, and passwords need at least 10.',
    );
    expect(shortPage).toMatch(/id="register-username"[^>]*value="webuser"/);
    expect(shortPage).toMatch(/id="register-password"[^>]*aria-invalid="true"/);
    expect(shortPage).not.toContain('short"');
    const done = await browser.post(`${ORIGIN}/signin/local`, {
      attempt: attempt.attemptId,
      username: 'webuser',
      password: 'a long enough password',
      action: 'register',
    });
    expect(done.status).toBe(200);
    const completed = await client.event('auth.handoff.completed');
    expect(completed['linked']).toBe(true);
    expect(
      (completed['session'] as { account: { id: string; kind: string } }).account,
    ).toMatchObject({
      id: session.account.id,
      kind: 'registered',
    });
    client.close();
  });

  it('explains web sign-in problems on the form instead of a bare error page', async () => {
    const browser = browserViaB();
    const page = await (await browser.get(`${ORIGIN}/signin`)).text();
    // Separate forms, so password managers offer a new password for sign-up.
    expect(page).toMatch(/id="signin-password"[^>]*autocomplete="current-password"/);
    expect(page).toMatch(
      /id="register-password"[^>]*autocomplete="new-password"[^>]*minlength="10"/,
    );
    expect(page).toContain('<title>Sign in · Globulation 2</title>');
    const post = (form: Record<string, string>) => browser.post(`${ORIGIN}/signin/local`, form);
    const problem = async (form: Record<string, string>) => {
      const response = await post(form);
      const text = await response.text();
      expect(text).toContain('<form method="post" action="/signin/local"');
      expect(text).not.toContain('Sign-in problem');
      const error = /<p class="field-error[^"]*"[^>]*id="([^"]+)-error"[^>]*>([^<]*)</.exec(text);
      return { status: response.status, field: error?.[1], message: error?.[2], text };
    };
    const bad = await problem({
      action: 'register',
      username: 'ux test',
      password: 'long enough pw',
    });
    expect(bad).toMatchObject({
      status: 400,
      field: 'register-username',
      message: 'Usernames can only use letters, digits, dots, dashes and underscores (no spaces).',
    });
    expect(bad.text).toMatch(/id="register-username"[^>]*value="ux test"/);
    expect(bad.text).not.toContain('long enough pw');
    expect(
      (await problem({ action: 'register', username: 'ab', password: 'long enough pw' })).message,
    ).toBe('Usernames are 3 to 32 characters long; this one has 2.');
    const created = await post({
      action: 'register',
      username: 'formuser',
      password: 'a long enough password',
    });
    expect(created.status).toBe(200);
    const taken = await problem({
      action: 'register',
      username: 'FormUser',
      password: 'another long password',
    });
    expect(taken).toMatchObject({ status: 409, field: 'register-username' });
    expect(taken.message).toContain('The username FormUser is taken.');
    const wrong = await problem({ action: 'signin', username: 'formuser', password: 'nope' });
    expect(wrong).toMatchObject({
      status: 401,
      field: 'signin-password',
      message: 'That password is not right for this username. Try again.',
    });
    expect(wrong.text).toMatch(/id="signin-username"[^>]*value="formuser"/);
    const unknown = await problem({ action: 'signin', username: 'nobody-here', password: 'x' });
    expect(unknown).toMatchObject({ status: 401, field: 'signin-username' });
    expect(unknown.message).toContain('There is no account called nobody-here.');
    const empty = await problem({ action: 'signin', username: 'formuser', password: '' });
    expect(empty).toMatchObject({ field: 'signin-password', message: 'Enter your password.' });
  });

  it('lists the configured providers in the instance description', async () => {
    const info = await json(await fetch(`${b.url}/api/v1/instance`));
    expect(info['authProviders']).toEqual([
      { id: 'mock', kind: 'oidc', displayName: 'Mock ID' },
      { id: 'apple', kind: 'apple', displayName: 'Apple' },
      { id: 'local', kind: 'local', displayName: 'Username and password' },
    ]);
  });
});
