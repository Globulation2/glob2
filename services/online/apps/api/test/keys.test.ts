// Signing keys: loading from files and the environment, rotation by kid.
import { createPublicKey } from 'node:crypto';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { loadConfig } from '@glob2/core';
import { generateSigningKeyPem, SigningKeys } from '../src/auth/keys.ts';
import { logger } from './support.ts';

describe('signing keys', () => {
  it('loads a key directory, signs with the active kid and publishes retired keys', () => {
    const dir = mkdtempSync(join(tmpdir(), 'glob2-keys-'));
    const old = generateSigningKeyPem();
    writeFileSync(join(dir, 'k2026a.pem'), old);
    writeFileSync(join(dir, 'k2026b.pem'), generateSigningKeyPem());
    const retired = createPublicKey(generateSigningKeyPem()).export({
      format: 'pem',
      type: 'spki',
    });
    writeFileSync(join(dir, 'k2025.pub.pem'), retired);

    expect(() =>
      SigningKeys.load(
        { directory: dir, inline: undefined, activeKid: undefined },
        'https://x.org',
        logger,
      ),
    ).toThrow(/JWT_ACTIVE_KID/);
    expect(() =>
      SigningKeys.load(
        { directory: dir, inline: undefined, activeKid: 'k2025' },
        'https://x.org',
        logger,
      ),
    ).toThrow(/not a configured private key/);

    const keys = SigningKeys.load(
      { directory: dir, inline: undefined, activeKid: 'k2026b' },
      'https://x.org',
      logger,
    );
    expect(keys.jwks().keys.map((k) => k.kid)).toEqual(['k2026b', 'k2025', 'k2026a']);
    const token = keys.sign('at+jwt', { aud: 'a', exp: Math.floor(Date.now() / 1000) + 60 });
    expect(JSON.parse(Buffer.from(token.split('.')[0]!, 'base64url').toString()).kid).toBe(
      'k2026b',
    );
    expect(keys.verify(token, { type: 'at+jwt', audience: 'a' }).claims['aud']).toBe('a');

    // A token signed by the previous key still verifies after rotation.
    const previous = SigningKeys.load(
      { directory: dir, inline: undefined, activeKid: 'k2026a' },
      'https://x.org',
      logger,
    );
    const oldToken = previous.sign('at+jwt', { aud: 'a', exp: Math.floor(Date.now() / 1000) + 60 });
    expect(keys.verify(oldToken, { type: 'at+jwt', audience: 'a' }).header.kid).toBe('k2026a');
  });

  it('reads an inline key from the environment and refuses to start without keys', () => {
    const pem = generateSigningKeyPem();
    const config = loadConfig({
      cwd: tmpdir(),
      env: {
        DATABASE_URL: 'postgres://x/y',
        JWT_KEY_ID: 'env1',
        JWT_PRIVATE_KEY: pem.replace(/\n/g, '\\n'),
      },
    });
    const keys = SigningKeys.load(config.keys, 'https://x.org', logger);
    expect(keys.active.kid).toBe('env1');
    expect(() =>
      SigningKeys.load(
        { directory: undefined, inline: undefined, activeKid: undefined },
        'https://x.org',
        logger,
      ),
    ).toThrow(/no signing keys/);
    expect(SigningKeys.load(undefined, 'http://localhost:8080', logger).active.kid).toBe('dev');
  });
});
