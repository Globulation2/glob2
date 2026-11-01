// Local passwords (auth.local), hashed with argon2id at the OWASP-recommended
// minimum cost (19 MiB, 2 passes, 1 lane). Hashes are PHC strings, so the
// parameters can be raised later without invalidating existing hashes.
import { hash, verify } from '@node-rs/argon2';

const OPTIONS = {
  algorithm: 2, // Algorithm.Argon2id (a const enum, not importable here)
  memoryCost: 19456,
  timeCost: 2,
  parallelism: 1,
} as const;

export const LOCAL_PROVIDER = 'local';

/** Usernames compare case-insensitively; the identity subject is the lowercase form. */
export function normalizeUsername(username: string): string {
  return username.toLowerCase();
}

export function hashPassword(password: string): Promise<string> {
  return hash(password, OPTIONS);
}

// Verified against when the username does not exist, so the response time does
// not reveal which usernames are registered.
let dummyHash: Promise<string> | undefined;

export async function verifyPassword(passwordHash: string | null | undefined, password: string) {
  if (!passwordHash) {
    dummyHash ??= hashPassword('glob2-dummy-password');
    await verify(await dummyHash, password).catch(() => false);
    return false;
  }
  return verify(passwordHash, password).catch(() => false);
}
