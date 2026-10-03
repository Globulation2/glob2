// REST identity flows against a real Postgres: guests, tokens and JWKS, local
// passwords, renames, admin CLI and role checks, rate limits.
import { createPublicKey, verify } from 'node:crypto';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
import { checkDocument } from '@glob2/protocol';
import { runCli } from '../src/cli.ts';
import { createHarness, json, postJson, type Harness, type Instance } from './support.ts';

let harness: Harness;
let api: Instance;

beforeAll(async () => {
  harness = await createHarness();
  api = await harness.start({
    instance: {
      auth: { providers: [], local: { enabled: true } },
      accounts: { renameIntervalDays: 30 },
      limits: { authPerMinute: 1000, guestsPerHour: 1000 },
    },
  });
});

afterAll(async () => {
  await harness?.close();
});

async function newGuest(instance = api) {
  const response = await postJson(`${instance.url}/api/v1/auth/guest`, { platform: 'desktop' });
  expect(response.status).toBe(200);
  return (await response.json()) as {
    account: { id: string; displayName: string; kind: string };
    tokens: { accessToken: string; refreshToken: string };
    deviceCredential: string;
  };
}

const bearer = (token: string) => ({ authorization: `Bearer ${token}` });

function decode(token: string) {
  const [h, c] = token.split('.');
  return {
    header: JSON.parse(Buffer.from(h!, 'base64url').toString()),
    claims: JSON.parse(Buffer.from(c!, 'base64url').toString()),
  };
}

describe('guests', () => {
  it('creates a guest with a generated name and a device credential stored hashed', async () => {
    const session = await newGuest();
    expect(checkDocument('SignInResponse', session).stage).toBe('ok');
    expect(session.account.kind).toBe('guest');
    expect(session.account.displayName).toMatch(/^Guest-\d{4}$/);
    expect(session.deviceCredential).toMatch(/^[A-Za-z0-9_-]{43}$/);
    const rows = await harness.database.db
      .selectFrom('device_credentials')
      .select('credential_hash')
      .where('account_id', '=', session.account.id)
      .execute();
    expect(rows).toHaveLength(1);
    expect(rows[0]!.credential_hash).toMatch(/^[0-9a-f]{64}$/);
    expect(rows[0]!.credential_hash).not.toContain(session.deviceCredential);
  });

  it('signs a returning guest in with its device credential, without a new credential', async () => {
    const first = await newGuest();
    const again = await json(
      await postJson(`${api.url}/api/v1/auth/guest`, {
        platform: 'android',
        deviceCredential: first.deviceCredential,
      }),
    );
    expect((again['account'] as { id: string }).id).toBe(first.account.id);
    expect(again['deviceCredential']).toBeUndefined();
    const unknown = await postJson(`${api.url}/api/v1/auth/guest`, {
      platform: 'android',
      deviceCredential: 'A'.repeat(43),
    });
    expect(unknown.status).toBe(401);
  });

  it('serves the self and public account views', async () => {
    const session = await newGuest();
    const me = await fetch(`${api.url}/api/v1/accounts/me`, {
      headers: bearer(session.tokens.accessToken),
    });
    const self = await json(me);
    expect(checkDocument('SelfAccount', self).stage).toBe('ok');
    expect(self).toMatchObject({
      id: session.account.id,
      role: 'user',
      status: 'active',
      canRename: false,
    });
    const pub = await json(await fetch(`${api.url}/api/v1/accounts/${session.account.id}`));
    expect(checkDocument('PublicAccount', pub).stage).toBe('ok');
    expect(pub['role']).toBeUndefined();
    expect((await fetch(`${api.url}/api/v1/accounts/me`)).status).toBe(401);
  });

  it('does not let guests choose a name', async () => {
    const session = await newGuest();
    const response = await fetch(`${api.url}/api/v1/accounts/me`, {
      method: 'PATCH',
      headers: { ...bearer(session.tokens.accessToken), 'content-type': 'application/json' },
      body: JSON.stringify({ displayName: 'Alice' }),
    });
    expect(response.status).toBe(403);
  });
});

describe('tokens', () => {
  it('issues EdDSA at+jwt access tokens verifiable with the published JWKS', async () => {
    const session = await newGuest();
    const { header, claims } = decode(session.tokens.accessToken);
    expect(header).toEqual({ alg: 'EdDSA', typ: 'at+jwt', kid: 'test-1' });
    expect(checkDocument('AccessTokenClaims', claims).stage).toBe('ok');
    expect(claims).toMatchObject({
      iss: api.url,
      aud: 'glob2-platform',
      sub: session.account.id,
      kind: 'guest',
    });
    expect(claims.exp - claims.iat).toBe(600);

    const jwksResponse = await fetch(`${api.url}/.well-known/jwks.json`);
    expect(jwksResponse.headers.get('cache-control')).toContain('max-age');
    const jwks = await json(jwksResponse);
    expect(checkDocument('PlatformJwks', jwks)).toEqual({ stage: 'ok', issues: [] });
    const jwk = (jwks['keys'] as { kid: string }[]).find((k) => k.kid === header.kid)!;
    const key = createPublicKey({ key: jwk as never, format: 'jwk' });
    const [h, c, s] = session.tokens.accessToken.split('.');
    expect(verify(null, Buffer.from(`${h}.${c}`), key, Buffer.from(s!, 'base64url'))).toBe(true);
  });

  it('rejects tampered, foreign and wrong-type tokens', async () => {
    const session = await newGuest();
    const [h, c, s] = session.tokens.accessToken.split('.');
    const claims = JSON.parse(Buffer.from(c!, 'base64url').toString());
    const tampered = `${h}.${Buffer.from(JSON.stringify({ ...claims, role: 'admin' })).toString('base64url')}.${s}`;
    const ticketTyped = harness.keys.sign('glob2-match+jwt', claims);
    for (const token of [tampered, ticketTyped, 'not-a-jwt']) {
      const response = await fetch(`${api.url}/api/v1/accounts/me`, { headers: bearer(token) });
      expect(response.status).toBe(401);
    }
  });

  it('rotates refresh tokens and revokes the family when a rotated token is reused', async () => {
    const session = await newGuest();
    const first = session.tokens.refreshToken;
    const rotated = await json(
      await postJson(`${api.url}/api/v1/auth/refresh`, { refreshToken: first }),
    );
    expect(checkDocument('AuthTokens', rotated).stage).toBe('ok');
    const second = rotated['refreshToken'] as string;
    expect(second).not.toBe(first);
    expect(decode(rotated['accessToken'] as string).claims.sid).toBe(
      decode(session.tokens.accessToken).claims.sid,
    );

    // The old token is presented again: theft is assumed, the family dies.
    const reuse = await postJson(`${api.url}/api/v1/auth/refresh`, { refreshToken: first });
    expect(reuse.status).toBe(401);
    expect((await json(reuse))['details']).toEqual({ reason: 'reused' });
    const afterReuse = await postJson(`${api.url}/api/v1/auth/refresh`, { refreshToken: second });
    expect(afterReuse.status).toBe(401);
    // Access tokens of the family stop working immediately, not at expiry.
    const me = await fetch(`${api.url}/api/v1/accounts/me`, {
      headers: bearer(rotated['accessToken'] as string),
    });
    expect(me.status).toBe(401);
    // The device credential still signs the guest in (a new family).
    const again = await postJson(`${api.url}/api/v1/auth/guest`, {
      platform: 'desktop',
      deviceCredential: session.deviceCredential,
    });
    expect(again.status).toBe(200);
  });

  it('stores refresh tokens hashed and revokes on sign-out', async () => {
    const session = await newGuest();
    const stored = await harness.database.db
      .selectFrom('refresh_tokens')
      .select('token_hash')
      .where('account_id', '=', session.account.id)
      .execute();
    expect(stored.every((row) => row.token_hash !== session.tokens.refreshToken)).toBe(true);
    const out = await postJson(`${api.url}/api/v1/auth/sign-out`, {
      refreshToken: session.tokens.refreshToken,
    });
    expect(out.status).toBe(204);
    const refresh = await postJson(`${api.url}/api/v1/auth/refresh`, {
      refreshToken: session.tokens.refreshToken,
    });
    expect(refresh.status).toBe(401);
    const me = await fetch(`${api.url}/api/v1/accounts/me`, {
      headers: bearer(session.tokens.accessToken),
    });
    expect(me.status).toBe(401);
  });
});

describe('local accounts', () => {
  it('upgrades a guest in place and signs in with the password', async () => {
    const guest = await newGuest();
    const register = await postJson(
      `${api.url}/api/v1/auth/local/register`,
      {
        username: 'Alice_1',
        password: 'correct horse battery',
        displayName: 'Alice',
        platform: 'desktop',
      },
      bearer(guest.tokens.accessToken),
    );
    expect(register.status).toBe(200);
    const registered = await json(register);
    const account = registered['account'] as Record<string, unknown>;
    expect(account).toMatchObject({
      id: guest.account.id,
      kind: 'registered',
      displayName: 'Alice',
    });
    expect(account['identities']).toEqual([expect.objectContaining({ provider: 'local' })]);

    const hash = await harness.database.db
      .selectFrom('identities')
      .select(['password_hash', 'subject'])
      .where('account_id', '=', guest.account.id)
      .executeTakeFirstOrThrow();
    expect(hash.subject).toBe('alice_1');
    expect(hash.password_hash).toMatch(/^\$argon2id\$v=19\$m=19456,t=2,p=1\$/);

    const signIn = await postJson(`${api.url}/api/v1/auth/local/sign-in`, {
      username: 'ALICE_1',
      password: 'correct horse battery',
      platform: 'browser',
    });
    expect(signIn.status).toBe(200);
    expect(((await json(signIn))['account'] as { id: string }).id).toBe(guest.account.id);

    const wrong = await postJson(`${api.url}/api/v1/auth/local/sign-in`, {
      username: 'alice_1',
      password: 'wrong password!',
      platform: 'browser',
    });
    expect(wrong.status).toBe(401);
    const nobody = await postJson(`${api.url}/api/v1/auth/local/sign-in`, {
      username: 'nobody_here',
      password: 'whatever12345',
      platform: 'browser',
    });
    expect(nobody.status).toBe(401);
    // The same answer whether or not the username exists.
    expect((await json(nobody))['message']).toBe((await json(wrong))['message']);

    // The username is taken now, whoever asks.
    const taken = await postJson(`${api.url}/api/v1/auth/local/register`, {
      username: 'alice_1',
      password: 'another password',
      platform: 'desktop',
    });
    expect(taken.status).toBe(409);
  });

  it('unlinks a sign-in method but never the last one', async () => {
    const registered = await json(
      await postJson(`${api.url}/api/v1/auth/local/register`, {
        username: 'carol_u',
        password: 'a long password',
        displayName: 'Carol',
        platform: 'desktop',
      }),
    );
    const accountId = (registered['account'] as { id: string }).id;
    const token = (registered['tokens'] as { accessToken: string }).accessToken;
    const unlink = (provider: string, auth: Record<string, string> = bearer(token)) =>
      fetch(`${api.url}/api/v1/accounts/me/identities/${provider}`, {
        method: 'DELETE',
        headers: auth,
      });

    // The only method: refused, and still linked.
    const last = await unlink('local');
    expect(last.status).toBe(409);
    expect(((await json(last))['details'] as Record<string, unknown>)['reason']).toBe(
      'last_sign_in_method',
    );
    expect(await unlink('google').then((r) => r.status)).toBe(404);
    expect(await unlink('Bad Provider!').then((r) => r.status)).toBe(400);
    expect(await unlink('local', {}).then((r) => r.status)).toBe(401);

    // With a second method, either one may go, but not both.
    await harness.database.db
      .insertInto('identities')
      .values({ account_id: accountId, provider: 'google', subject: 'carol-google-1' })
      .execute();
    expect(await unlink('local').then((r) => r.status)).toBe(204);
    const self = await json(
      await fetch(`${api.url}/api/v1/accounts/me`, { headers: bearer(token) }),
    );
    expect(self['identities']).toEqual([expect.objectContaining({ provider: 'google' })]);
    expect(await unlink('google').then((r) => r.status)).toBe(409);

    // A guest has nothing to unlink.
    const guest = await newGuest();
    expect(await unlink('local', bearer(guest.tokens.accessToken)).then((r) => r.status)).toBe(404);
  });

  it('keeps registered display names unique and limits renames', async () => {
    const make = async (username: string, displayName: string) =>
      json(
        await postJson(`${api.url}/api/v1/auth/local/register`, {
          username,
          password: 'a long password',
          displayName,
          platform: 'desktop',
        }),
      );
    const bob = await make('bob_r', 'Bob');
    const bobby = await make('bobby_r', 'bob');
    // A taken name gets a numbered variant at registration.
    expect((bobby['account'] as { displayName: string }).displayName).toMatch(/^bob \d+$/);

    const token = (bobby['tokens'] as { accessToken: string }).accessToken;
    const rename = (displayName: string) =>
      fetch(`${api.url}/api/v1/accounts/me`, {
        method: 'PATCH',
        headers: { ...bearer(token), 'content-type': 'application/json' },
        body: JSON.stringify({ displayName }),
      });
    expect((await rename('BOB')).status).toBe(409);
    expect((await rename('Guest-1234')).status).toBe(400);
    const first = await rename('Robert');
    expect(first.status).toBe(200);
    const self = await json(first);
    expect(self['displayName']).toBe('Robert');
    expect(self['renameAvailableAt']).toBeDefined();
    const second = await rename('Rob');
    expect(second.status).toBe(429);
    expect(
      ((await json(second))['details'] as Record<string, unknown>)['renameAvailableAt'],
    ).toBeDefined();
    expect((bob['account'] as { displayName: string }).displayName).toBe('Bob');
  });
});

describe('administration', () => {
  it('grants roles from the CLI and guards admin endpoints by role', async () => {
    const register = async (username: string) =>
      json(
        await postJson(`${api.url}/api/v1/auth/local/register`, {
          username,
          password: 'a long password',
          displayName: username,
          platform: 'desktop',
        }),
      );
    const boss = await register('boss');
    const mod = await register('moddy');
    const player = await register('player_x');
    const id = (session: Record<string, unknown>) => (session['account'] as { id: string }).id;
    const token = (session: Record<string, unknown>) =>
      (session['tokens'] as { accessToken: string }).accessToken;

    const io = {
      lines: [] as string[],
      out(l: string) {
        this.lines.push(l);
      },
      err(l: string) {
        this.lines.push(l);
      },
    };
    const env = { DATABASE_URL: harness.database.url };
    // Before any grant, the endpoints refuse.
    const search = (who: Record<string, unknown>, q = 'player') =>
      fetch(`${api.url}/api/v1/admin/accounts?q=${q}`, { headers: bearer(token(who)) });
    expect((await search(boss)).status).toBe(403);

    expect(await runCli(['admin', 'grant', 'boss'], io, env)).toBe(0);
    expect(await runCli(['admin', 'grant', id(mod), '--role', 'moderator'], io, env)).toBe(0);
    expect(await runCli(['admin', 'grant', 'nobody-at-all'], io, env)).toBe(1);
    expect(io.lines.join('\n')).toContain('is now admin');

    // Roles are read from the database, so existing tokens pick them up.
    const found = await search(mod);
    expect(found.status).toBe(200);
    const list = await json(found);
    expect(checkDocument('AdminAccountList', list).stage).toBe('ok');
    expect((list['items'] as { id: string }[]).map((a) => a.id)).toContain(id(player));

    const post = (who: Record<string, unknown>, path: string, body: unknown) =>
      postJson(`${api.url}/api/v1/admin/accounts/${id(player)}/${path}`, body, bearer(token(who)));
    // Moderators may rename and mute, not ban.
    expect(
      (await post(mod, 'rename', { displayName: 'Renamed Player', reason: 'offensive' })).status,
    ).toBe(200);
    const muted = await json(await post(mod, 'mute', { minutes: 60 }));
    expect(muted['mutedUntil']).toBeDefined();
    expect((await post(mod, 'ban', { banned: true })).status).toBe(403);
    expect((await post(player, 'mute', { minutes: 5 })).status).toBe(403);

    // Administrators may ban: the player's sessions end at once.
    const banned = await post(boss, 'ban', { banned: true, reason: 'cheating' });
    expect(banned.status).toBe(200);
    // Its sign-ins are revoked, so even unexpired access tokens stop working.
    expect(
      (await fetch(`${api.url}/api/v1/accounts/me`, { headers: bearer(token(player)) })).status,
    ).toBe(401);
    const refresh = await postJson(`${api.url}/api/v1/auth/refresh`, {
      refreshToken: (player['tokens'] as { refreshToken: string }).refreshToken,
    });
    expect(refresh.status).toBe(401);
    const signIn = await postJson(`${api.url}/api/v1/auth/local/sign-in`, {
      username: 'player_x',
      password: 'a long password',
      platform: 'desktop',
    });
    expect(signIn.status).toBe(403);

    // Revoking from the CLI removes the role.
    expect(await runCli(['admin', 'revoke', id(mod)], io, env)).toBe(0);
    expect((await search(mod)).status).toBe(403);

    const audit = await harness.database.db
      .selectFrom('admin_audit_log')
      .select(['action', 'actor_account_id'])
      .where('target_id', 'in', [id(player), id(mod)])
      .orderBy('id')
      .execute();
    expect(audit.map((row) => row.action)).toEqual([
      'account.role',
      'account.rename',
      'account.mute',
      'account.ban',
      'account.role',
    ]);
    expect(audit[0]!.actor_account_id).toBeNull();
    expect(audit[3]!.actor_account_id).toBe(id(boss));
  });

  it('deletes accounts from the CLI and the admin API, freeing the username', async () => {
    const register = async (username: string) =>
      json(
        await postJson(`${api.url}/api/v1/auth/local/register`, {
          username,
          password: 'a long password',
          displayName: username,
          platform: 'desktop',
        }),
      );
    const id = (session: Record<string, unknown>) => (session['account'] as { id: string }).id;
    const token = (session: Record<string, unknown>) =>
      (session['tokens'] as { accessToken: string }).accessToken;
    const io = {
      lines: [] as string[],
      out: (l: string) => io.lines.push(l),
      err: (l: string) => io.lines.push(l),
    };
    const env = { DATABASE_URL: harness.database.url };
    const chief = await register('chief');
    expect(await runCli(['admin', 'grant', id(chief)], io, env)).toBe(0);

    // A test account with a catalog map, deleted from the server's command line.
    const tester = await register('UxReviewTester');
    const map = await json(
      await postJson(
        `${api.url}/api/v1/maps`,
        { title: 'Test map (please ignore)', visibility: 'unlisted' },
        bearer(token(tester)),
      ),
    );
    expect(
      await runCli(['admin', 'delete', 'UxReviewTester', '--reason', 'test data'], io, env),
    ).toBe(0);
    expect(io.lines.at(-1)).toMatch(
      /^deleted UxReviewTester \(.*registered\); removed 1 catalog map/,
    );
    expect(await runCli(['admin', 'delete', id(tester)], io, env)).toBe(1);
    const row = await harness.database.db
      .selectFrom('accounts')
      .selectAll()
      .where('id', '=', id(tester))
      .executeTakeFirstOrThrow();
    expect(row).toMatchObject({ status: 'deleted', display_name: 'Deleted player' });
    expect((await fetch(`${api.url}/api/v1/maps/${map['id'] as string}`)).status).toBe(404);
    expect(
      (await fetch(`${api.url}/api/v1/accounts/me`, { headers: bearer(token(tester)) })).status,
    ).toBe(401);
    expect(
      (
        await postJson(`${api.url}/api/v1/auth/local/sign-in`, {
          username: 'UxReviewTester',
          password: 'a long password',
          platform: 'desktop',
        })
      ).status,
    ).toBe(401);
    // The username can be registered again.
    expect(id(await register('UxReviewTester'))).not.toBe(id(tester));

    // A guest, deleted by an administrator over the API.
    const guest = await json(
      await postJson(`${api.url}/api/v1/auth/guest`, { platform: 'desktop' }),
    );
    const remove = (who: Record<string, unknown>, target: string) =>
      fetch(`${api.url}/api/v1/admin/accounts/${target}?reason=test`, {
        method: 'DELETE',
        headers: bearer(token(who)),
      });
    expect((await remove(guest, id(chief))).status).toBe(403);
    expect((await remove(chief, id(chief))).status).toBe(403);
    expect((await remove(chief, id(guest))).status).toBe(204);
    expect((await remove(chief, id(guest))).status).toBe(404);
    const audit = await harness.database.db
      .selectFrom('admin_audit_log')
      .select(['action', 'details'])
      .where('target_id', 'in', [id(tester), id(guest)])
      .orderBy('id')
      .execute();
    expect(audit.map((a) => a.action)).toEqual(['account.delete', 'account.delete']);
    expect(audit[0]!.details).toMatchObject({ displayName: 'UxReviewTester', removedMaps: 1 });
  });

  it('generates signing keys from the CLI', async () => {
    const io = {
      lines: [] as string[],
      out(l: string) {
        this.lines.push(l);
      },
      err(l: string) {
        this.lines.push(l);
      },
    };
    expect(await runCli(['keys', 'generate'], io)).toBe(0);
    expect(io.lines[0]).toContain('BEGIN PRIVATE KEY');
  });
});

describe('rate limits', () => {
  it('limits sign-in requests and new guests per address', async () => {
    // Counters are shared by every replica: start from none.
    await harness.database.db.deleteFrom('rate_limits').execute();
    const limited = await harness.start({
      instance: { limits: { authPerMinute: 3, guestsPerHour: 2 } },
    });
    const statuses: number[] = [];
    for (let i = 0; i < 3; i++) {
      statuses.push(
        (await postJson(`${limited.url}/api/v1/auth/guest`, { platform: 'desktop' })).status,
      );
    }
    expect(statuses).toEqual([200, 200, 429]);
    // Each sign-in route has its own per-address budget.
    const refreshes: number[] = [];
    for (let i = 0; i < 4; i++) {
      refreshes.push(
        (await postJson(`${limited.url}/api/v1/auth/refresh`, { refreshToken: 'x' })).status,
      );
    }
    expect(refreshes).toEqual([401, 401, 401, 429]);
    const response = await postJson(`${limited.url}/api/v1/auth/refresh`, { refreshToken: 'x' });
    expect(response.headers.get('retry-after')).toBeTruthy();
    const body = await json(response);
    expect(body['code']).toBe('rate_limited');
    expect(checkDocument('ErrorBody', body).stage).toBe('ok');
  });
});
