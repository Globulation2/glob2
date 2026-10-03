// Random secrets and their at-rest hashes. Every bearer secret the platform
// hands out (device credentials, refresh tokens, web session cookies, browser
// bindings, OAuth state) is 256 random bits, base64url, and stored only as its
// SHA-256: the secrets have full entropy, so a fast hash is enough.
import { createHash, randomBytes, randomInt, timingSafeEqual } from 'node:crypto';

/** 256-bit random secret, base64url without padding (43 characters). */
export function randomSecret(): string {
  return randomBytes(32).toString('base64url');
}

export function sha256Hex(value: string): string {
  return createHash('sha256').update(value, 'utf8').digest('hex');
}

export function safeEqual(a: string, b: string): boolean {
  const left = Buffer.from(a);
  const right = Buffer.from(b);
  return left.length === right.length && timingSafeEqual(left, right);
}

/** Letters and digits that cannot be confused when read aloud or copied (no 0/O, 1/I/L). */
const CODE_ALPHABET = 'ABCDEFGHJKMNPQRSTUVWXYZ23456789';

/** Short human-comparable code, e.g. for the browser sign-in confirmation. */
export function confirmationCode(length = 8): string {
  let code = '';
  for (let i = 0; i < length; i++) code += CODE_ALPHABET[randomInt(CODE_ALPHABET.length)];
  return code;
}
