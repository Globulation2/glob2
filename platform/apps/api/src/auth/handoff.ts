// Browser sign-in handoff. A game client asks for an attempt over its realtime
// socket, opens /signin?attempt=… in the system browser, and the platform
// pushes the result to the waiting socket on whichever replica holds it.
//
// Attempt rows move pending → completed | failed | cancelled | expired once;
// `delivered_at` makes delivery to the socket happen exactly once, so tokens
// are minted only by the replica that hands them over.
import { sql, type Kysely } from 'kysely';
import type { Account, Database } from '@glob2/db';
import type { ClientPlatform, IdentityConflict } from '@glob2/protocol';
import type { AccountService } from './accounts.ts';
import { confirmationCode, randomSecret, safeEqual, sha256Hex } from './secrets.ts';

export type FailureReason = 'expired' | 'denied' | 'cancelled' | 'conflict' | 'error';

export type Delivery =
  | { kind: 'completed'; account: Account; linked: boolean; platform: ClientPlatform }
  | { kind: 'failed'; reason: FailureReason; conflict?: IdentityConflict };

export interface Attempt {
  id: string;
  confirmationCode: string;
  expiresAt: Date;
  /** Lets the client re-attach to the attempt from a new socket (returned once). */
  resumeToken: string;
}

export class HandoffService {
  private readonly db: Kysely<Database>;
  private readonly accounts: AccountService;
  private readonly lifetimeSeconds: number;
  /** Called after an attempt reached a final state, to wake the replica holding its socket. */
  notify: (attemptId: string) => Promise<void> = async () => undefined;

  constructor(db: Kysely<Database>, accounts: AccountService, lifetimeSeconds: number) {
    this.db = db;
    this.accounts = accounts;
    this.lifetimeSeconds = lifetimeSeconds;
  }

  async begin(options: {
    provider?: string | undefined;
    mode: 'link' | 'signin';
    requestingAccountId?: string | undefined;
    platform: ClientPlatform;
  }): Promise<Attempt> {
    const resumeToken = randomSecret();
    const row = await this.db
      .insertInto('signin_attempts')
      .values({
        resume_hash: sha256Hex(resumeToken),
        confirmation_code: confirmationCode(),
        provider: options.provider ?? null,
        mode: options.mode,
        requesting_account_id: options.requestingAccountId ?? null,
        client_platform: options.platform,
        expires_at: new Date(Date.now() + this.lifetimeSeconds * 1000),
      })
      .returning(['id', 'confirmation_code', 'expires_at'])
      .executeTakeFirstOrThrow();
    return {
      id: row.id,
      confirmationCode: row.confirmation_code,
      expiresAt: row.expires_at,
      resumeToken,
    };
  }

  /** The attempt, if the resume token matches and it was not delivered yet. */
  async forResume(id: string, resumeToken: string) {
    const attempt = await this.get(id);
    if (!attempt || !attempt.resume_hash || attempt.delivered_at) return undefined;
    return safeEqual(attempt.resume_hash, sha256Hex(resumeToken)) ? attempt : undefined;
  }

  async get(id: string) {
    if (!/^[0-9a-f-]{36}$/.test(id)) return undefined;
    return this.db
      .selectFrom('signin_attempts')
      .selectAll()
      .where('id', '=', id)
      .executeTakeFirst();
  }

  /** A pending, unexpired attempt. */
  async pending(id: string) {
    const attempt = await this.get(id);
    if (!attempt || attempt.status !== 'pending' || attempt.expires_at <= new Date())
      return undefined;
    return attempt;
  }

  /** Binds the attempt to one browser (first visit wins). Returns whether `hash` is the bound browser. */
  async bindBrowser(id: string, hash: string): Promise<boolean> {
    await this.db
      .updateTable('signin_attempts')
      .set({ browser_binding_hash: hash })
      .where('id', '=', id)
      .where('browser_binding_hash', 'is', null)
      .execute();
    const attempt = await this.get(id);
    return attempt?.browser_binding_hash === hash;
  }

  async recordConflict(id: string, accountId: string): Promise<void> {
    await this.db
      .updateTable('signin_attempts')
      .set({ conflict_account_id: accountId })
      .where('id', '=', id)
      .where('status', '=', 'pending')
      .execute();
  }

  async complete(id: string, accountId: string, linked: boolean): Promise<boolean> {
    const result = await this.db
      .updateTable('signin_attempts')
      .set({
        status: 'completed',
        account_id: accountId,
        linked,
        completed_at: sql<Date>`now()`,
      })
      .where('id', '=', id)
      .where('status', '=', 'pending')
      .executeTakeFirst();
    const done = Number(result.numUpdatedRows) === 1;
    if (done) await this.notify(id);
    return done;
  }

  async fail(id: string, reason: FailureReason): Promise<boolean> {
    const status =
      reason === 'expired' ? 'expired' : reason === 'cancelled' ? 'cancelled' : 'failed';
    const result = await this.db
      .updateTable('signin_attempts')
      .set({ status, failure_reason: reason, completed_at: sql<Date>`now()` })
      .where('id', '=', id)
      .where('status', '=', 'pending')
      .executeTakeFirst();
    const done = Number(result.numUpdatedRows) === 1;
    if (done) await this.notify(id);
    return done;
  }

  /**
   * Claims a finished attempt for delivery to its socket. Returns undefined if
   * it is still pending or was already delivered.
   */
  async claimDelivery(id: string): Promise<Delivery | undefined> {
    const row = await this.db
      .updateTable('signin_attempts')
      .set({ delivered_at: sql<Date>`now()` })
      .where('id', '=', id)
      .where('status', '<>', 'pending')
      .where('delivered_at', 'is', null)
      .returningAll()
      .executeTakeFirst();
    if (!row) return undefined;
    if (row.status === 'completed' && row.account_id) {
      const account = await this.accounts.get(row.account_id);
      if (account && account.status === 'active') {
        return {
          kind: 'completed',
          account,
          linked: row.linked ?? false,
          platform: row.client_platform,
        };
      }
      return { kind: 'failed', reason: 'error' };
    }
    const reason: FailureReason =
      row.failure_reason ?? (row.status === 'expired' ? 'expired' : 'error');
    if (reason === 'conflict' && row.conflict_account_id) {
      const owner = await this.accounts.get(row.conflict_account_id);
      if (owner) {
        return {
          kind: 'failed',
          reason,
          conflict: {
            reason: 'identity_in_use',
            provider: row.provider ?? 'unknown',
            account: this.accounts.publicView(owner),
          },
        };
      }
    }
    return { kind: 'failed', reason };
  }

  async setProvider(id: string, provider: string): Promise<void> {
    await this.db.updateTable('signin_attempts').set({ provider }).where('id', '=', id).execute();
  }
}
