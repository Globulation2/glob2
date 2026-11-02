import { createHash } from 'node:crypto';
import sharp from 'sharp';
import { afterAll, afterEach, beforeAll, describe, expect, it, vi } from 'vitest';
import { AccountService } from '../src/auth/accounts.ts';
import { canonicalAvatar } from '../src/avatars/image.ts';
import { createHarness, type Harness, type Instance } from './support.ts';
import { seedHistory, SEEDED_QUEUES, type SeededHistory } from './historySeed.ts';
let harness: Harness;
let api: Instance;
let seed: SeededHistory;
let photo: Buffer;
beforeAll(async () => {
  harness = await createHarness();
  api = await harness.start({ origin: 'http://play.test', instance: { queues: SEEDED_QUEUES } });
  seed = await seedHistory(harness.database.db, harness.blobs);
  photo = await sharp({ create: { width: 900, height: 600, channels: 3, background: '#2d8e68' } })
    .jpeg()
    .withMetadata({ orientation: 6 })
    .toBuffer();
});
afterEach(() => vi.unstubAllGlobals());
afterAll(async () => harness?.close());
function headers() {
  return { cookie: `glob2_session=${seed.userSession}`, origin: 'http://play.test' };
}
function image() {
  return api.app.inject({ method: 'GET', url: `/api/v1/accounts/${seed.accounts.kestrel}/avatar` });
}
function source(value: string) {
  return api.app.inject({
    method: 'PATCH',
    url: '/api/v1/accounts/me/avatar',
    headers: headers(),
    payload: { source: value },
  });
}
describe('account avatars', () => {
  it('validates and normalizes photos, including EXIF orientation and metadata removal', async () => {
    const out = await canonicalAvatar(photo);
    const meta = await sharp(out).metadata();
    expect(meta).toMatchObject({ format: 'webp', width: 512, height: 512 });
    expect(meta.exif).toBeUndefined();
    expect(meta.orientation).toBeUndefined();
    await expect(canonicalAvatar(Buffer.from('<svg/>'))).rejects.toThrow();
    await expect(canonicalAvatar(Buffer.alloc(10 * 1024 * 1024 + 1))).rejects.toThrow();
    const huge = await sharp({
      create: { width: 5001, height: 5000, channels: 3, background: 'white' },
    })
      .png()
      .toBuffer();
    await expect(canonicalAvatar(huge)).rejects.toThrow();
  });
  it('requires authentication and CSRF protection', async () => {
    expect(
      (
        await api.app.inject({
          method: 'PUT',
          url: '/api/v1/accounts/me/avatar',
          headers: { 'content-type': 'application/octet-stream' },
          payload: photo,
        })
      ).statusCode,
    ).toBe(401);
    expect(
      (
        await api.app.inject({
          method: 'PATCH',
          url: '/api/v1/accounts/me/avatar',
          headers: { ...headers(), origin: 'https://evil.invalid' },
          payload: { source: 'initials' },
        })
      ).statusCode,
    ).toBe(403);
  });
  it('uploads, replaces and removes photos without retaining old blobs', async () => {
    const upload = () =>
      api.app.inject({
        method: 'PUT',
        url: '/api/v1/accounts/me/avatar',
        headers: { ...headers(), 'content-type': 'application/octet-stream' },
        payload: photo,
      });
    const first = await upload();
    expect(first.statusCode, first.body).toBe(200);
    expect(first.json().avatarSource).toBe('uploaded');
    const old = await harness.database.db
      .selectFrom('accounts')
      .select('avatar_key')
      .where('id', '=', seed.accounts.kestrel)
      .executeTakeFirstOrThrow();
    expect((await image()).headers['content-type']).toBe('image/webp');
    expect((await upload()).statusCode).toBe(200);
    expect(await harness.blobs.size(old.avatar_key!)).toBeUndefined();
    expect((await source('initials')).statusCode).toBe(200);
    expect((await image()).statusCode).toBe(404);
    expect((await source('bad')).statusCode).toBe(400);
    const invalid = await api.app.inject({
      method: 'PUT',
      url: '/api/v1/accounts/me/avatar',
      headers: { ...headers(), 'content-type': 'application/octet-stream' },
      payload: Buffer.from('bad'),
    });
    expect(invalid.statusCode).toBe(400);
  });
  it('finds the first linked Gravatar, caches it, and invalidates when identities change', async () => {
    await harness.database.db
      .insertInto('identities')
      .values([
        {
          account_id: seed.accounts.kestrel,
          provider: 'test-first',
          subject: 'first',
          email: ' Missing@Example.com ',
          created_at: new Date('2020-01-01'),
        },
        {
          account_id: seed.accounts.kestrel,
          provider: 'test-next',
          subject: 'next',
          email: 'Photo@Example.com',
          created_at: new Date('2020-01-02'),
        },
      ])
      .execute();
    const missing = createHash('sha256').update('missing@example.com').digest('hex');
    const fetcher = vi.fn(async (url: string) =>
      url.includes(missing)
        ? new Response(null, { status: 404 })
        : new Response(new Uint8Array(photo), { headers: { 'content-type': 'image/jpeg' } }),
    );
    vi.stubGlobal('fetch', fetcher);
    await source('automatic');
    const response = await image();
    expect(response.statusCode, response.body).toBe(200);
    expect(fetcher).toHaveBeenCalledTimes(2);
    expect(fetcher.mock.calls[0]![0]).toContain('s=512&d=404&r=g');
    await image();
    expect(fetcher).toHaveBeenCalledTimes(2);
    await harness.database.db
      .updateTable('identities')
      .set({ email: 'new@example.com' })
      .where('provider', '=', 'test-first')
      .execute();
    await image();
    expect(fetcher).toHaveBeenCalledTimes(3);
    const publicProfile = await api.app.inject({
      method: 'GET',
      url: `/api/v1/players/${seed.accounts.kestrel}`,
    });
    expect(publicProfile.body).not.toContain('example.com');
    expect(publicProfile.body).not.toContain(missing);
    await source('initials');
    await image();
    expect(fetcher).toHaveBeenCalledTimes(3);
  });
  it('caches missing Gravatars and falls back on network failure', async () => {
    await source('automatic');
    const fetcher = vi.fn(async () => new Response(null, { status: 404 }));
    vi.stubGlobal('fetch', fetcher);
    expect((await image()).statusCode).toBe(404);
    const calls = fetcher.mock.calls.length;
    expect(calls).toBeGreaterThan(0);
    await image();
    expect(fetcher).toHaveBeenCalledTimes(calls);
    await source('automatic');
    vi.stubGlobal(
      'fetch',
      vi.fn(async () => {
        throw new Error('timeout');
      }),
    );
    expect((await image()).statusCode).toBe(404);
  });
  it('changes the photo URL when linked emails change, but not on unchanged sign-in', async () => {
    const accounts = new AccountService(harness.database.db);
    const created = await accounts.resolveIdentity(
      { provider: 'avatar-first', subject: 'identity-cache', email: 'first@example.com' },
      { mode: 'signin' },
    );
    if (created.kind !== 'signed-in') throw new Error('Expected account');
    const original = created.account;
    const linked = await accounts.resolveIdentity(
      { provider: 'avatar-second', subject: 'identity-cache', email: 'second@example.com' },
      { mode: 'link', current: original },
    );
    if (linked.kind !== 'signed-in') throw new Error('Expected linked account');
    expect(linked.account.avatar_revision).toBe(original.avatar_revision + 1);
    const unchanged = await accounts.resolveIdentity(
      { provider: 'avatar-second', subject: 'identity-cache', email: 'second@example.com' },
      { mode: 'signin' },
    );
    if (unchanged.kind !== 'signed-in') throw new Error('Expected account');
    expect(unchanged.account.avatar_revision).toBe(linked.account.avatar_revision);
    const changed = await accounts.resolveIdentity(
      { provider: 'avatar-second', subject: 'identity-cache', email: 'changed@example.com' },
      { mode: 'signin' },
    );
    if (changed.kind !== 'signed-in') throw new Error('Expected account');
    expect(changed.account.avatar_revision).toBe(linked.account.avatar_revision + 1);
    await accounts.unlinkProvider(changed.account, 'avatar-first');
    const final = await accounts.requireUsable(original.id);
    expect(final.avatar_revision).toBe(changed.account.avatar_revision + 1);
    expect(accounts.publicView(final).avatarUrl).not.toBe(accounts.publicView(original).avatarUrl);
    expect(final.gravatar_checked_at).toBeNull();
  });
  it('does not hold database connections or restore a photo after concurrent opt-out', async () => {
    await source('automatic');
    let release!: (response: Response) => void;
    const fetcher = vi.fn(
      () =>
        new Promise<Response>((resolve) => {
          release = resolve;
        }),
    );
    vi.stubGlobal('fetch', fetcher);
    const request = image();
    await vi.waitFor(() => expect(fetcher).toHaveBeenCalledTimes(1));
    expect(harness.database.pool.idleCount).toBe(harness.database.pool.totalCount);
    const writes = vi.spyOn(harness.blobs, 'put');
    try {
      expect((await source('initials')).statusCode).toBe(200);
    } finally {
      release(new Response(new Uint8Array(photo)));
    }
    expect((await request).statusCode).toBe(404);
    for (const [key] of writes.mock.calls) expect(await harness.blobs.size(key)).toBeUndefined();
    writes.mockRestore();
    const account = await harness.database.db
      .selectFrom('accounts')
      .selectAll()
      .where('id', '=', seed.accounts.kestrel)
      .executeTakeFirstOrThrow();
    expect(account.avatar_source).toBe('initials');
    expect(account.gravatar_key).toBeNull();
  });
  it('discards an in-flight refresh when a linked email changes', async () => {
    await source('automatic');
    let release!: (response: Response) => void;
    const fetcher = vi.fn(
      () =>
        new Promise<Response>((resolve) => {
          release = resolve;
        }),
    );
    vi.stubGlobal('fetch', fetcher);
    const request = image();
    await vi.waitFor(() => expect(fetcher).toHaveBeenCalledTimes(1));
    const accounts = new AccountService(harness.database.db);
    try {
      await accounts.resolveIdentity(
        { provider: 'test-first', subject: 'first', email: 'replacement@example.com' },
        { mode: 'signin' },
      );
    } finally {
      release(new Response(new Uint8Array(photo)));
    }
    expect((await request).statusCode).toBe(404);
    const account = await accounts.requireUsable(seed.accounts.kestrel);
    expect(account.gravatar_key).toBeNull();
    expect(account.gravatar_checked_at).toBeNull();
    vi.stubGlobal(
      'fetch',
      vi.fn(async () => new Response(new Uint8Array(photo))),
    );
    expect((await image()).statusCode).toBe(200);
  });
  it('bounds upstream concurrency while allowing a page of uncached photos to finish', async () => {
    const accounts = new AccountService(harness.database.db);
    const ids: string[] = [];
    for (let i = 0; i < 6; i++) {
      const created = await accounts.resolveIdentity(
        { provider: 'avatar-concurrency', subject: `photo-${i}`, email: `photo-${i}@example.com` },
        { mode: 'signin' },
      );
      if (created.kind !== 'signed-in') throw new Error('Expected account');
      ids.push(created.account.id);
    }
    let release!: () => void;
    const gate = new Promise<void>((resolve) => {
      release = resolve;
    });
    let active = 0;
    let maximum = 0;
    const fetcher = vi.fn(async () => {
      active++;
      maximum = Math.max(maximum, active);
      await gate;
      active--;
      return new Response(new Uint8Array(photo));
    });
    vi.stubGlobal('fetch', fetcher);
    const requests = Promise.all(
      ids.map((id) =>
        api.app.inject({
          method: 'GET',
          url: `/api/v1/accounts/${id}/avatar`,
        }),
      ),
    );
    try {
      await vi.waitFor(() => expect(fetcher).toHaveBeenCalledTimes(4));
      expect(harness.database.pool.idleCount).toBe(harness.database.pool.totalCount);
    } finally {
      release();
    }
    expect((await requests).map((r) => r.statusCode)).toEqual([200, 200, 200, 200, 200, 200]);
    expect(maximum).toBe(4);
  });
  it('coalesces concurrent refreshes and enforces the upstream timeout', async () => {
    await source('automatic');
    const fetcher = vi.fn(
      (_url: string, options: RequestInit) =>
        new Promise<Response>((_, reject) => {
          options.signal?.addEventListener('abort', () => reject(new Error('timeout')), {
            once: true,
          });
        }),
    );
    vi.stubGlobal('fetch', fetcher);
    const results = await Promise.all([image(), image(), image()]);
    expect(results.map((r) => r.statusCode)).toEqual([404, 404, 404]);
    expect(fetcher).toHaveBeenCalledTimes(1);
    await image();
    expect(fetcher).toHaveBeenCalledTimes(1);
  });
  it('deletion stops serving the image and cleans stored bytes', async () => {
    // The history fixture has abbreviated verifier jobs, without a scrub-able setup.
    await harness.database.db.deleteFrom('engine_jobs').execute();
    await api.app.inject({
      method: 'PUT',
      url: '/api/v1/accounts/me/avatar',
      headers: { ...headers(), 'content-type': 'application/octet-stream' },
      payload: photo,
    });
    const row = await harness.database.db
      .selectFrom('accounts')
      .selectAll()
      .where('id', '=', seed.accounts.kestrel)
      .executeTakeFirstOrThrow();
    const response = await api.app.inject({
      method: 'DELETE',
      url: '/api/v1/accounts/me',
      headers: headers(),
      payload: { confirmDisplayName: row.display_name },
    });
    expect(response.statusCode, response.body).toBe(204);
    expect((await image()).statusCode).toBe(404);
    expect(await harness.blobs.size(row.avatar_key!)).toBeUndefined();
  });
});
