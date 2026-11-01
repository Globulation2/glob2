// The platform's Ed25519 signing keys. Every platform JWT (access tokens now,
// match tickets in M4) is signed with the active key; every key that may still
// have signed a live token is published at /.well-known/jwks.json.
//
// Rotation: add the new key (as `<kid>.pem`) while the old one stays active, so
// relays fetch it with the JWKS; switch JWT_ACTIVE_KID to the new key; after
// the longest token lifetime, replace the old private key with its public half
// (`<old>.pub.pem`) or remove it.
import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import {
  createPrivateKey,
  createPublicKey,
  generateKeyPairSync,
  type KeyObject,
} from 'node:crypto';
import { ConfigError, type Logger, type SigningKeysConfig } from '@glob2/core';
import { signJwt, verifyJwt } from '@glob2/protocol/node';
import type { PlatformJwks } from '@glob2/protocol';

const KID_PATTERN = /^[A-Za-z0-9._-]{1,128}$/;

export interface SigningKey {
  kid: string;
  publicKey: KeyObject;
  /** Absent for retired keys kept only for verification. */
  privateKey?: KeyObject;
}

function ed25519(key: KeyObject, where: string): KeyObject {
  if (key.asymmetricKeyType !== 'ed25519') {
    throw new ConfigError(`${where}: signing keys must be Ed25519`);
  }
  return key;
}

export function signingKeyFromPem(kid: string, pem: string, where = kid): SigningKey {
  if (!KID_PATTERN.test(kid)) throw new ConfigError(`${where}: invalid key id ${kid}`);
  if (pem.includes('PRIVATE KEY')) {
    const privateKey = ed25519(createPrivateKey(pem), where);
    return { kid, privateKey, publicKey: createPublicKey(privateKey) };
  }
  return { kid, publicKey: ed25519(createPublicKey(pem), where) };
}

/** A new Ed25519 private key as PKCS#8 PEM. */
export function generateSigningKeyPem(): string {
  const { privateKey } = generateKeyPairSync('ed25519');
  return privateKey.export({ format: 'pem', type: 'pkcs8' }).toString();
}

export class SigningKeys {
  private readonly keys: Map<string, SigningKey>;
  readonly active: SigningKey & { privateKey: KeyObject };

  constructor(keys: SigningKey[], activeKid?: string) {
    this.keys = new Map();
    for (const key of keys) {
      if (this.keys.has(key.kid)) throw new ConfigError(`duplicate signing key id ${key.kid}`);
      this.keys.set(key.kid, key);
    }
    const signers = keys.filter((key) => key.privateKey);
    let active: SigningKey | undefined;
    if (activeKid) {
      active = this.keys.get(activeKid);
      if (!active?.privateKey) {
        throw new ConfigError(`JWT_ACTIVE_KID ${activeKid} is not a configured private key`);
      }
    } else if (signers.length === 1) {
      active = signers[0];
    } else {
      throw new ConfigError(
        signers.length === 0
          ? 'no signing key configured (JWT_KEYS_DIR or JWT_PRIVATE_KEY)'
          : 'several signing keys configured: set JWT_ACTIVE_KID',
      );
    }
    this.active = active as SigningKey & { privateKey: KeyObject };
  }

  /** Generates a throwaway key (tests and local development). */
  static ephemeral(kid = 'dev'): SigningKeys {
    return new SigningKeys([signingKeyFromPem(kid, generateSigningKeyPem())]);
  }

  /**
   * Loads keys from the configured directory and environment. Without any
   * configured key, a local-development instance (http://localhost) gets an
   * ephemeral key; any other instance refuses to start.
   */
  static load(config: SigningKeysConfig | undefined, publicOrigin: string, logger: Logger) {
    const keys: SigningKey[] = [];
    if (config?.directory) {
      for (const file of readdirSync(config.directory).sort()) {
        const kid = /^(.+?)(\.pub)?\.pem$/.exec(file)?.[1];
        if (!kid) continue;
        const path = join(config.directory, file);
        keys.push(signingKeyFromPem(kid, readFileSync(path, 'utf8'), path));
      }
    }
    if (config?.inline) {
      keys.push(signingKeyFromPem(config.inline.kid, config.inline.pem, 'JWT_PRIVATE_KEY'));
    }
    if (keys.length === 0) {
      if (/^http:\/\/(localhost|127\.0\.0\.1)(:\d+)?$/.test(publicOrigin)) {
        logger.warn('no signing keys configured; using an ephemeral development key');
        return SigningKeys.ephemeral();
      }
      throw new ConfigError('no signing keys configured: set JWT_KEYS_DIR or JWT_PRIVATE_KEY');
    }
    return new SigningKeys(keys, config?.activeKid);
  }

  sign(type: string, claims: object): string {
    return signJwt(
      { alg: 'EdDSA', typ: type, kid: this.active.kid },
      claims,
      this.active.privateKey,
    );
  }

  verify(token: string, options: { type: string; audience: string; now?: number }) {
    return verifyJwt(token, {
      ...options,
      leewaySeconds: 30,
      key: (kid) => this.keys.get(kid)?.publicKey,
    });
  }

  /** The signing key first, then every other key. */
  jwks(): PlatformJwks {
    const ordered = [this.active, ...[...this.keys.values()].filter((k) => k !== this.active)];
    return {
      keys: ordered.map((key) => {
        const x = String(key.publicKey.export({ format: 'jwk' }).x);
        return { kty: 'OKP', crv: 'Ed25519', x, kid: key.kid, alg: 'EdDSA', use: 'sig' };
      }),
    };
  }
}
