import type { Static, TSchema } from 'typebox';
import { schemaIssues } from '@glob2/protocol';
import { apiError } from '../errors.ts';

/** The request body typed as the schema, or a bad_request error listing the problems. */
export function body<T extends TSchema>(schema: T, value: unknown): Static<T> {
  const issues = schemaIssues(schema, value ?? {});
  if (issues.length > 0) throw apiError('bad_request', 'Invalid request body.', issues);
  return (value ?? {}) as Static<T>;
}

/**
 * Counts events per key in fixed windows (in memory, per replica). Used where
 * a limit applies to only some outcomes of a route, such as creating guests.
 */
export class WindowCounter {
  private readonly windowMs: number;
  private readonly max: number;
  private readonly counts = new Map<string, { start: number; count: number }>();

  constructor(max: number, windowMs: number) {
    this.max = max;
    this.windowMs = windowMs;
  }

  /** Records one event; false if the key is over its limit. */
  take(key: string, now = Date.now()): boolean {
    let entry = this.counts.get(key);
    if (!entry || now - entry.start >= this.windowMs) {
      if (this.counts.size > 10_000) this.prune(now);
      entry = { start: now, count: 0 };
      this.counts.set(key, entry);
    }
    if (entry.count >= this.max) return false;
    entry.count++;
    return true;
  }

  /** True if the key has used up its window (without recording an event). */
  exhausted(key: string, now = Date.now()): boolean {
    const entry = this.counts.get(key);
    return !!entry && now - entry.start < this.windowMs && entry.count >= this.max;
  }

  /** Releases one event (e.g. a connection that closed). */
  release(key: string): void {
    const entry = this.counts.get(key);
    if (entry && entry.count > 0) entry.count--;
  }

  private prune(now: number): void {
    for (const [key, entry] of this.counts) {
      if (now - entry.start >= this.windowMs) this.counts.delete(key);
    }
  }
}
