// Rate limits that hold across API replicas: sliding-window counters in
// Postgres (migration 0011, rate_limit_take). One round trip per check.
//
// The global per-address request cap (@fastify/rate-limit in app.ts) stays in
// memory: it is a coarse flood guard where a per-replica budget is fine. The
// limits here guard things that cost or risk something whatever replica
// answers: sign-ins, password guesses, new guests and browser sign-in
// attempts, uploads, map catalog writes and chat.
import type { FastifyReply, FastifyRequest } from 'fastify';
import { sql, type Kysely } from 'kysely';
import type { Database } from '@glob2/db';
import { apiError } from '../errors.ts';

export interface LimitCheck {
  allowed: boolean;
  /** Events in the sliding window, after this one if it was allowed. */
  estimate: number;
  /** Seconds until the current fixed window ends: a fair Retry-After. */
  retryAfterSeconds: number;
}

/** One named limit: at most `max` events per key in any `windowMs`. */
export class SharedLimit {
  readonly bucket: string;
  readonly max: number;
  readonly windowMs: number;
  private readonly db: Kysely<Database>;

  constructor(db: Kysely<Database>, bucket: string, max: number, windowMs: number) {
    this.db = db;
    this.bucket = bucket;
    this.max = max;
    this.windowMs = windowMs;
  }

  private async run(key: string, cost: number): Promise<LimitCheck> {
    const { rows } = await sql<{
      allowed: boolean;
      estimate: number;
      window_left: number;
    }>`SELECT * FROM rate_limit_take(${this.bucket}, ${key.slice(0, 256)}, ${this.max},
        ${this.windowMs / 1000}, ${cost})`.execute(this.db);
    const row = rows[0];
    return {
      allowed: row?.allowed ?? true,
      estimate: row?.estimate ?? 0,
      retryAfterSeconds: Math.max(1, Math.ceil(row?.window_left ?? 1)),
    };
  }

  /** Records `cost` events unless that would exceed the limit. */
  take(key: string, cost = 1): Promise<LimitCheck> {
    return this.run(key, cost);
  }

  /** True if the key is at its limit (records nothing). */
  async exhausted(key: string): Promise<boolean> {
    return (await this.run(key, 0)).estimate >= this.max;
  }

  /** Forgets the key (e.g. failed sign-ins after a successful one). */
  async reset(key: string): Promise<void> {
    await this.db
      .deleteFrom('rate_limits')
      .where('bucket', '=', this.bucket)
      .where('key', '=', key)
      .execute();
  }
}

/** Takes one event or answers 429 with Retry-After. */
export async function enforce(
  limit: SharedLimit,
  key: string,
  reply: FastifyReply | undefined,
  message = 'Too many requests; wait a while and try again.',
): Promise<void> {
  const check = await limit.take(key);
  if (check.allowed) return;
  if (reply) void reply.header('retry-after', String(check.retryAfterSeconds));
  throw apiError('rate_limited', message, { retryAfterSeconds: check.retryAfterSeconds });
}

/** A route preHandler limiting each client address. */
export function perAddress(limit: SharedLimit) {
  return async (request: FastifyRequest, reply: FastifyReply) => enforce(limit, request.ip, reply);
}
