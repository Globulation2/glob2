import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { text } from 'node:stream/consumers';
import { describe, expect, it } from 'vitest';
import {
  AccessDeniedError,
  ConfigError,
  FsBlobStore,
  Shutdown,
  allowAllPolicy,
  assertAllowed,
  contentKey,
  createAccessPolicy,
  createLogger,
  loadConfig,
  loadInstanceConfig,
  parseRelayKeys,
  putContent,
} from '../src/index.ts';

const SIM = { versionMinor: 125, netProtocol: 49, dataHash: 'ab'.repeat(32) };
const silent = createLogger('test', 'silent');

function tempDir(): string {
  return mkdtempSync(join(tmpdir(), 'glob2-core-'));
}

describe('loadConfig', () => {
  it('reads the environment and falls back to the default instance', () => {
    const cwd = tempDir();
    const config = loadConfig({ cwd, env: { DATABASE_URL: 'postgres://x/y', HTTP_PORT: '9000' } });
    expect(config.http).toEqual({ host: '0.0.0.0', port: 9000 });
    expect(config.publicOrigin).toBe('http://localhost:8080');
    expect(config.instance.access.policy).toBe('allow-all');
    expect(config.instanceConfigPath).toBeUndefined();
    expect(config.blobs.directory).toBe(join(cwd, 'data/blobs'));
  });

  it('loads and validates instance.yaml', () => {
    const cwd = tempDir();
    writeFileSync(
      join(cwd, 'instance.yaml'),
      [
        'name: Test Instance',
        'guests: { enabled: false }',
        'auth:',
        '  providers:',
        '    - { id: google, kind: oidc, preset: google, displayName: Google, clientId: abc, clientSecretEnv: GOOGLE_SECRET }',
        '  local: { enabled: true }',
        'access: { policy: allow-all }',
        'queues:',
        '  - id: ranked-1v1',
        '    name: Ranked 1v1',
        '    mode: 1v1',
        '    rated: true',
        '    aiBackfillSeconds: 90',
        '    mapPool:',
        '      - { generatorId: even-ground, revision: 3, params: { width: 7, height: 7, teams: 2 }, candidates: 5, startingUnitLevel: 0 }',
      ].join('\n'),
    );
    const config = loadConfig({
      cwd,
      env: { DATABASE_URL: 'postgres://x/y', PUBLIC_ORIGIN: 'https://play.example.org/' },
    });
    expect(config.publicOrigin).toBe('https://play.example.org');
    expect(config.instance.name).toBe('Test Instance');
    expect(config.instance.queues[0]?.mapPool?.[0]?.generatorId).toBe('even-ground');
  });

  it('reads relay keys from RELAY_KEYS and RELAY_KEYS_FILE', () => {
    const cwd = tempDir();
    const shared = 's'.repeat(40);
    const pinned = 'p'.repeat(40);
    writeFileSync(join(cwd, 'relay-keys'), `# relays\nrelay-eu1:${pinned}\n\n`);
    const config = loadConfig({
      cwd,
      env: { DATABASE_URL: 'postgres://x/y', RELAY_KEYS: shared, RELAY_KEYS_FILE: 'relay-keys' },
    });
    expect(config.relayKeys).toEqual([{ key: shared }, { key: pinned, relayId: 'relay-eu1' }]);
    expect(config.uploadMaxBytes).toBe(64 * 1024 * 1024);
    expect(() => parseRelayKeys('short')).toThrow(ConfigError);
    expect(() => parseRelayKeys(`bad id!:${shared}`)).toThrow(ConfigError);
    expect(loadConfig({ cwd, env: { DATABASE_URL: 'postgres://x/y' } }).relayKeys).toEqual([]);
  });

  it('accepts the shipped instance.example.yaml', () => {
    const example = join(import.meta.dirname, '..', '..', '..', 'instance.example.yaml');
    expect(loadInstanceConfig(example).queues[0]?.id).toBe('ranked-1v1');
  });

  it('rejects bad settings with a clear message', () => {
    const cwd = tempDir();
    expect(() => loadConfig({ cwd, env: {} })).toThrow(/DATABASE_URL is required/);
    expect(() => loadConfig({ cwd, env: { DATABASE_URL: 'x', HTTP_PORT: 'eighty' } })).toThrow(
      ConfigError,
    );
    expect(() =>
      loadConfig({ cwd, env: { DATABASE_URL: 'x', INSTANCE_CONFIG: 'missing.yaml' } }),
    ).toThrow(/does not exist/);
    writeFileSync(join(cwd, 'instance.yaml'), 'name: X\nguests: { enabled: true }\n');
    expect(() => loadConfig({ cwd, env: { DATABASE_URL: 'x' } })).toThrow(
      /instance.yaml is invalid/,
    );
  });
});

describe('AccessPolicy', () => {
  it('allows everything by default', async () => {
    const policy = createAccessPolicy('allow-all');
    expect(policy).toBe(allowAllPolicy);
    const subject = {
      accountId: 'a',
      kind: 'guest' as const,
      role: 'user' as const,
      entitlements: [],
    };
    expect(await policy.canHost(subject, { simVersion: SIM, visibility: 'public' })).toEqual({
      allowed: true,
    });
    expect(
      await policy.canJoin(subject, { roomId: 'r', hostAccountId: 'h', simVersion: SIM }),
    ).toEqual({ allowed: true });
    expect(await policy.canQueue(subject, { queueId: 'q', rated: true, simVersion: SIM })).toEqual({
      allowed: true,
    });
    expect(() => createAccessPolicy('pay-to-win')).toThrow();
  });

  it('turns a denial into AccessDeniedError', () => {
    expect(() => assertAllowed({ allowed: true })).not.toThrow();
    expect(() =>
      assertAllowed({
        allowed: false,
        reason: 'Supporters only',
        requiredEntitlement: 'supporter',
      }),
    ).toThrow(AccessDeniedError);
  });
});

describe('FsBlobStore', () => {
  it('stores content by hash, atomically and idempotently', async () => {
    const store = new FsBlobStore(tempDir());
    const bytes = new TextEncoder().encode('glob2 map bytes');
    const stored = await putContent(store, bytes);
    expect(stored.key).toBe(contentKey(stored.sha256));
    expect(stored.size).toBe(bytes.byteLength);
    expect(await putContent(store, bytes)).toEqual(stored);
    expect(await store.size(stored.key)).toBe(bytes.byteLength);
    expect(await text((await store.get(stored.key))!)).toBe('glob2 map bytes');
    await store.delete(stored.key);
    expect(await store.get(stored.key)).toBeUndefined();
  });

  it('refuses keys that escape the store', async () => {
    const store = new FsBlobStore(tempDir());
    for (const key of ['../etc/passwd', '/abs', 'a/../../b', 'UPPER', 'trailing/']) {
      await expect(store.put(key, new Uint8Array())).rejects.toThrow(/invalid blob key|escapes/);
    }
  });
});

describe('Shutdown', () => {
  it('runs steps once, last registered first, and reports failures', async () => {
    const order: string[] = [];
    const shutdown = new Shutdown(silent, 5)
      .add('database', () => void order.push('database'))
      .add('broken', () => {
        order.push('broken');
        throw new Error('boom');
      })
      .add('http', async () => void order.push('http'));
    const first = shutdown.run('test');
    expect(shutdown.run('again')).toBe(first);
    await expect(first).rejects.toThrow(/errors/);
    expect(order).toEqual(['http', 'broken', 'database']);
  });
});
