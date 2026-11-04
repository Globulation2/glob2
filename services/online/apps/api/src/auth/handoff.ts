// Browser sign-in handoff. A game client asks for an attempt over its realtime
// socket, opens /signin?attempt=… in the system browser, and the platform
// pushes the result to the waiting socket on whichever replica holds it.
//
// A browser that opens the link from the game's own network goes straight on
// (bindBrowser). Any other browser first confirms with one click, comparing
// the code the game shows (confirmCode), so a link someone else started does
// not quietly sign a victim's account into the sender's game.
//
// Attempt rows move pending → completed | failed | cancelled | expired once;
// `delivered_at` makes delivery to the socket happen exactly once, so tokens
// are minted only by the replica that hands them over.
import { sql, type Kysely } from 'kysely';
import type { Account, Database } from '@glob2/db';
import type { ClientPlatform, IdentityConflict } from '@glob2/protocol';
import type { AccountService } from './accounts.ts';
import { confirmationCode, randomSecret, safeEqual, sha256Hex } from './secrets.ts';

/** Wrong codes allowed before a browser sign-in fails. */
export const CODE_ATTEMPTS = 5;

/** The code as typed: case, spaces and separators do not matter. */
export function normalizeCode(code: string): string {
  return code.toUpperCase().replace(/[^A-Z0-9]/g, '');
}

/**
 * The part of an address that one household or device shares: the whole IPv4
 * address, or the /64 prefix of an IPv6 one (privacy extensions rotate the
 * rest). Hashed, so the attempt row never stores the address itself.
 */
export function networkHash(ip: string): string {
  const address = ip.startsWith('::ffff:') ? ip.slice(7) : ip;
  if (!address.includes(':')) return sha256Hex(`ipv4:${address}`);
  const [head = '', tail = ''] = address.split('::');
  const left = head ? head.split(':') : [];
  const right = tail ? tail.split(':') : [];
  const groups = address.includes('::')
    ? [...left, ...Array<string>(Math.max(0, 8 - left.length - right.length)).fill('0'), ...right]
    : left;
  const prefix = groups
    .slice(0, 4)
    .map((group) => (parseInt(group, 16) || 0).toString(16))
    .join(':');
  return sha256Hex(`ipv6:${prefix}`);
}

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
    /** The game socket's address; a browser on the same network needs no confirmation. */
    ip?: string | undefined;
  }): Promise<Attempt> {
    const resumeToken = randomSecret();
    const row = await this.db
      .insertInto('signin_attempts')
      .values({
        resume_hash: sha256Hex(resumeToken),
        confirmation_code: confirmationCode(6),
        provider: options.provider ?? null,
        mode: options.mode,
        requesting_account_id: options.requestingAccountId ?? null,
        client_platform: options.platform,
        requesting_network_hash: options.ip ? networkHash(options.ip) : null,
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

  /** Whether a browser at this address shares the game's network. */
  sameNetwork(attempt: { requesting_network_hash: string | null }, ip: string): boolean {
    return (
      attempt.requesting_network_hash !== null &&
      safeEqual(attempt.requesting_network_hash, networkHash(ip))
    );
  }

  /**
   * Binds the attempt to this browser without a code, for a browser on the
   * game's own network (the first browser to open the link wins).
   */
  async bindBrowser(id: string, bindingHash: string): Promise<boolean> {
    const bound = await this.db
      .updateTable('signin_attempts')
      .set({
        browser_binding_hash: bindingHash,
        code_confirmed_at: sql<Date>`coalesce(code_confirmed_at, now())`,
      })
      .where('id', '=', id)
      .where('status', '=', 'pending')
      .where('expires_at', '>', sql<Date>`now()`)
      .where((eb) =>
        eb.or([
          eb('browser_binding_hash', 'is', null),
          eb('browser_binding_hash', '=', bindingHash),
        ]),
      )
      .executeTakeFirst();
    return Number(bound.numUpdatedRows) === 1;
  }

  /**
   * Checks the code the player confirmed in the browser against the one their
   * game shows. The right code binds the attempt to this browser (the first
   * browser to enter it wins); wrong codes count, and the attempt fails after
   * CODE_ATTEMPTS of them so the code cannot be guessed.
   */
  async confirmCode(
    id: string,
    typed: string,
    bindingHash: string,
  ): Promise<'confirmed' | 'wrong' | 'locked' | 'other_browser' | 'gone'> {
    const attempt = await this.pending(id);
    if (!attempt) return 'gone';
    if (attempt.browser_binding_hash && attempt.browser_binding_hash !== bindingHash) {
      return 'other_browser';
    }
    if (attempt.code_failures >= CODE_ATTEMPTS) return 'locked';
    if (safeEqual(normalizeCode(typed), normalizeCode(attempt.confirmation_code))) {
      const bound = await this.db
        .updateTable('signin_attempts')
        .set({
          browser_binding_hash: bindingHash,
          code_confirmed_at: sql<Date>`coalesce(code_confirmed_at, now())`,
        })
        .where('id', '=', id)
        .where('status', '=', 'pending')
        .where('code_failures', '<', CODE_ATTEMPTS)
        .where((eb) =>
          eb.or([
            eb('browser_binding_hash', 'is', null),
            eb('browser_binding_hash', '=', bindingHash),
          ]),
        )
        .executeTakeFirst();
      return Number(bound.numUpdatedRows) === 1 ? 'confirmed' : 'other_browser';
    }
    const counted = await this.db
      .updateTable('signin_attempts')
      .set({ code_failures: sql<number>`code_failures + 1` })
      .where('id', '=', id)
      .where('status', '=', 'pending')
      .returning('code_failures')
      .executeTakeFirst();
    if ((counted?.code_failures ?? CODE_ATTEMPTS) >= CODE_ATTEMPTS) {
      await this.fail(id, 'denied');
      return 'locked';
    }
    return 'wrong';
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
        requesting_network_hash: null,
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
      .set({
        status,
        failure_reason: reason,
        completed_at: sql<Date>`now()`,
        requesting_network_hash: null,
      })
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
