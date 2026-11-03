import { createHash, randomUUID } from 'node:crypto';
import { sql, type Kysely } from 'kysely';
import type { Account, Database } from '@glob2/db';
import type { BlobStore } from '@glob2/core';
import { canonicalAvatar, MAX_BYTES } from './image.ts';

const CACHE_MS = 86_400_000;
const RETRY_MS = 60_000;
const MAX_REFRESHES = 4;
const MAX_PENDING = 128;

/** External image work never holds a database connection or an account lock. */
export class GravatarPhotos {
  private pending = new Map<string, Promise<void>>();
  private active = 0;
  private waiting: (() => void)[] = [];
  private db: Kysely<Database>;
  private blobs: BlobStore;
  private remove: (keys: (string | null)[]) => Promise<void>;
  constructor(
    db: Kysely<Database>,
    blobs: BlobStore,
    remove: (keys: (string | null)[]) => Promise<void>,
  ) {
    this.db = db;
    this.blobs = blobs;
    this.remove = remove;
  }

  async keyFor(account: Account): Promise<string | null> {
    const emails = await this.db
      .selectFrom('identities')
      .select('email')
      .where('account_id', '=', account.id)
      .orderBy('created_at')
      .orderBy('id')
      .execute();
    const normalized = [
      ...new Set(emails.flatMap((r) => (r.email ? [r.email.trim().toLowerCase()] : []))),
    ];
    const fingerprint = createHash('sha256').update(JSON.stringify(normalized)).digest('hex');
    if (
      account.gravatar_fingerprint === fingerprint &&
      account.gravatar_checked_at &&
      Date.now() - account.gravatar_checked_at.getTime() < CACHE_MS
    )
      return account.gravatar_key;

    // Coalesce visitors to one account; bound upstream work for a page of uncached
    // photos. The bounded queue holds no database connections. Overloaded replicas
    // serve the previous image or initials and retry on a later visit.
    const token = `${account.id}:${account.avatar_revision}:${fingerprint}`;
    let pending = this.pending.get(token);
    if (!pending && this.pending.size < MAX_PENDING) {
      pending = this.refresh(account, normalized, fingerprint).finally(() => {
        this.pending.delete(token);
      });
      this.pending.set(token, pending);
    }
    if (pending) await pending;
    const current = await this.db
      .selectFrom('accounts')
      .select(['status', 'avatar_source', 'avatar_key', 'gravatar_key', 'gravatar_fingerprint'])
      .where('id', '=', account.id)
      .executeTakeFirst();
    if (!current || current.status !== 'active') return null;
    if (current.avatar_source === 'uploaded') return current.avatar_key;
    return current.avatar_source === 'automatic' && current.gravatar_fingerprint === fingerprint
      ? current.gravatar_key
      : null;
  }

  private async refresh(account: Account, emails: string[], fingerprint: string) {
    if (this.active >= MAX_REFRESHES)
      await new Promise<void>((resolve) => this.waiting.push(resolve));
    else this.active++;
    try {
      // A queued account may have opted out or changed identities while waiting.
      const current = await this.db
        .selectFrom('accounts')
        .select(['avatar_revision', 'avatar_source', 'status'])
        .where('id', '=', account.id)
        .executeTakeFirst();
      if (
        !current ||
        current.status !== 'active' ||
        current.avatar_source !== 'automatic' ||
        current.avatar_revision !== account.avatar_revision
      )
        return;
      await this.fetchAndStore(account, emails, fingerprint);
    } finally {
      const next = this.waiting.shift();
      if (next) next();
      else this.active--;
    }
  }

  private async fetchAndStore(account: Account, emails: string[], fingerprint: string) {
    let image: Buffer | undefined;
    let unavailable = false;
    const signal = AbortSignal.timeout(3000);
    try {
      for (const email of emails) {
        const hash = createHash('sha256').update(email).digest('hex');
        const response = await fetch(`https://gravatar.com/avatar/${hash}?s=512&d=404&r=g`, {
          signal,
          redirect: 'error',
        });
        if (response.status === 404) {
          await response.body?.cancel();
          continue;
        }
        if (!response.ok || !response.body) {
          await response.body?.cancel();
          throw new Error('gravatar unavailable');
        }
        const parts: Uint8Array[] = [];
        let size = 0;
        for await (const chunk of response.body) {
          size += chunk.length;
          if (size > MAX_BYTES) throw new Error('gravatar too large');
          parts.push(chunk);
        }
        image = await canonicalAvatar(Buffer.concat(parts));
        break;
      }
    } catch {
      unavailable = true;
    }
    const next = image
      ? `avatars/${account.id}/${randomUUID()}.webp`
      : unavailable && account.gravatar_fingerprint === fingerprint
        ? account.gravatar_key
        : null;
    if (next && image) await this.blobs.put(next, image);
    let accepted = false;
    try {
      const result = await this.db
        .updateTable('accounts')
        .set({
          gravatar_key: next,
          gravatar_fingerprint: fingerprint,
          gravatar_checked_at: new Date(Date.now() - (unavailable ? CACHE_MS - RETRY_MS : 0)),
        })
        .where('id', '=', account.id)
        .where('status', '=', 'active')
        .where('avatar_source', '=', 'automatic')
        .where('avatar_revision', '=', account.avatar_revision)
        .where(sql<boolean>`gravatar_key IS NOT DISTINCT FROM ${account.gravatar_key}`)
        .where(
          sql<boolean>`gravatar_checked_at IS NOT DISTINCT FROM ${account.gravatar_checked_at}`,
        )
        .executeTakeFirst();
      accepted = result.numUpdatedRows > 0;
    } finally {
      // Another replica or an identity/preference change won the race. Never
      // publish stale photos, and discard bytes that lost the conditional write.
      if (!accepted && image) await this.remove([next]);
    }
    if (accepted && account.gravatar_key !== next) await this.remove([account.gravatar_key]);
  }
}
