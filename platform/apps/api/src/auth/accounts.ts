// Accounts: guests, registered accounts, display names and linked identities.
import { randomInt } from 'node:crypto';
import { sql, type Kysely, type Transaction } from 'kysely';
import type { Account, Database } from '@glob2/db';
import {
  MAX_PLAYER_NAME_BYTES,
  utf8ByteLength,
  type ClientPlatform,
  type IdentityConflict,
  type PublicAccount,
  type SelfAccount,
} from '@glob2/protocol';
import { apiError } from '../errors.ts';
import { randomSecret, sha256Hex } from './secrets.ts';

type Db = Kysely<Database> | Transaction<Database>;

/** Names of the form Guest-1234 belong to guests; registered accounts cannot take them. */
const GUEST_NAME = /^guest[\s_-]*\d+$/i;

export const DEFAULT_RENAME_INTERVAL_DAYS = 30;

/** An external or local sign-in identity, as a provider reported it. */
export interface ProviderIdentity {
  provider: string;
  subject: string;
  email?: string | undefined;
  /** Name the provider suggests, used for a new registered account's display name. */
  name?: string | undefined;
}

export interface IdentityOptions {
  /** The account the caller is signed in as. */
  current?: Account | undefined;
  mode: 'link' | 'signin';
  /** Local registration: stored with the new identity. */
  passwordHash?: string;
  /** Fail with conflict if the identity exists (local registration). */
  createOnly?: boolean;
}

export type IdentityOutcome =
  | { kind: 'signed-in'; account: Account; linked: boolean; created: boolean }
  | { kind: 'conflict'; conflict: IdentityConflict };

export function isUniqueViolation(error: unknown, constraint?: string): boolean {
  const e = error as { code?: string; constraint?: string };
  return e?.code === '23505' && (constraint === undefined || e.constraint === constraint);
}

const NAME_INDEX = 'accounts_registered_display_name_key';

/** Throws bad_request unless `name` is a well-formed display name. */
export function checkDisplayName(name: string): void {
  if (name !== name.trim() || name.length === 0) {
    throw apiError('bad_request', 'Display names cannot start or end with spaces.');
  }
  // eslint-disable-next-line no-control-regex
  if (/[\u0000-\u001f\u007f]/.test(name)) {
    throw apiError('bad_request', 'Display names cannot contain control characters.');
  }
  if (utf8ByteLength(name) > MAX_PLAYER_NAME_BYTES) {
    throw apiError('bad_request', `Display names are at most ${MAX_PLAYER_NAME_BYTES} bytes.`);
  }
}

/** Best-effort display name from free text (a provider's name claim). */
export function sanitizeName(raw: string | undefined): string | undefined {
  if (!raw) return undefined;
  let name = raw
    // eslint-disable-next-line no-control-regex
    .replace(/[\u0000-\u001f\u007f]/g, ' ')
    .replace(/\s+/g, ' ')
    .trim();
  while (utf8ByteLength(name) > MAX_PLAYER_NAME_BYTES)
    name = [...name].slice(0, -1).join('').trim();
  if (!name || GUEST_NAME.test(name)) return undefined;
  return name;
}

function iso(date: Date | null | undefined): string | undefined {
  return date ? date.toISOString() : undefined;
}

export class AccountService {
  private readonly db: Kysely<Database>;
  private readonly renameIntervalDays: number;

  constructor(db: Kysely<Database>, options: { renameIntervalDays?: number } = {}) {
    this.db = db;
    this.renameIntervalDays = options.renameIntervalDays ?? DEFAULT_RENAME_INTERVAL_DAYS;
  }

  async get(id: string, db: Db = this.db): Promise<Account | undefined> {
    if (!/^[0-9a-f-]{36}$/.test(id)) return undefined;
    return db.selectFrom('accounts').selectAll().where('id', '=', id).executeTakeFirst();
  }

  /** The account, or an error if it does not exist or may not sign in. */
  async requireUsable(id: string, db: Db = this.db): Promise<Account> {
    const account = await this.get(id, db);
    if (!account || account.status === 'deleted') {
      throw apiError('unauthenticated', 'The account no longer exists.');
    }
    if (account.status === 'banned') throw apiError('forbidden', 'This account is banned.');
    return account;
  }

  publicView(account: Account): PublicAccount {
    return {
      id: account.id,
      displayName: account.display_name,
      kind: account.kind,
      createdAt: account.created_at.toISOString(),
    };
  }

  renameAvailableAt(account: Account): Date | undefined {
    if (account.kind !== 'registered' || !account.display_name_changed_at) return undefined;
    if (this.renameIntervalDays === 0) return undefined;
    const next = new Date(
      account.display_name_changed_at.getTime() + this.renameIntervalDays * 86_400_000,
    );
    return next.getTime() > Date.now() ? next : undefined;
  }

  async selfView(account: Account, db: Db = this.db): Promise<SelfAccount> {
    const identities = await db
      .selectFrom('identities')
      .select(['provider', 'created_at', 'email'])
      .where('account_id', '=', account.id)
      .orderBy('created_at')
      .execute();
    const entitlements = await db
      .selectFrom('entitlements')
      .select('entitlement')
      .distinct()
      .where('account_id', '=', account.id)
      .where('revoked_at', 'is', null)
      .where((eb) => eb.or([eb('expires_at', 'is', null), eb('expires_at', '>', sql<Date>`now()`)]))
      .orderBy('entitlement')
      .execute();
    const muted =
      account.muted_until && account.muted_until > new Date() ? account.muted_until : undefined;
    const renameAt = this.renameAvailableAt(account);
    return {
      ...this.publicView(account),
      role: account.role,
      status: account.status,
      ...(muted ? { mutedUntil: muted.toISOString() } : {}),
      identities: identities.map((identity) => ({
        provider: identity.provider,
        linkedAt: identity.created_at.toISOString(),
        ...(identity.email ? { email: identity.email } : {}),
      })),
      entitlements: entitlements.map((row) => row.entitlement),
      canRename: account.kind === 'registered',
      ...(renameAt ? { renameAvailableAt: iso(renameAt) } : {}),
    };
  }

  /**
   * Removes every identity of `provider` from the account. A registered account
   * keeps at least one sign-in method: removing the last one is a conflict.
   */
  async unlinkProvider(account: Account, provider: string): Promise<void> {
    await this.db.transaction().execute(async (tx) => {
      // Serialises concurrent unlinks of the same account's last two methods.
      await tx
        .selectFrom('accounts')
        .select('id')
        .where('id', '=', account.id)
        .forUpdate()
        .execute();
      const linked = await tx
        .selectFrom('identities')
        .select(['id', 'provider'])
        .where('account_id', '=', account.id)
        .execute();
      const removing = linked.filter((identity) => identity.provider === provider);
      if (removing.length === 0) {
        throw apiError('not_found', `No ${provider} sign-in is linked to this account.`);
      }
      if (account.kind === 'registered' && removing.length === linked.length) {
        throw apiError(
          'conflict',
          'This is the last way to sign in to this account; link another method first.',
          { reason: 'last_sign_in_method', provider },
        );
      }
      await tx
        .deleteFrom('identities')
        .where('account_id', '=', account.id)
        .where('provider', '=', provider)
        .execute();
    });
  }

  /** Creates a guest with a generated name and a new device credential (returned once). */
  async createGuest(platform: ClientPlatform): Promise<{ account: Account; credential: string }> {
    const credential = randomSecret();
    return this.db.transaction().execute(async (tx) => {
      const account = await tx
        .insertInto('accounts')
        .values({ kind: 'guest', display_name: `Guest-${randomInt(1000, 10000)}` })
        .returningAll()
        .executeTakeFirstOrThrow();
      await tx
        .insertInto('device_credentials')
        .values({ account_id: account.id, credential_hash: sha256Hex(credential), platform })
        .execute();
      return { account, credential };
    });
  }

  /** The account a device credential belongs to, if the credential is live. */
  async findByDeviceCredential(credential: string): Promise<Account | undefined> {
    const row = await this.db
      .updateTable('device_credentials')
      .set({ last_used_at: sql<Date>`now()` })
      .where('credential_hash', '=', sha256Hex(credential))
      .where('revoked_at', 'is', null)
      .returning('account_id')
      .executeTakeFirst();
    return row ? this.get(row.account_id) : undefined;
  }

  async touch(accountId: string): Promise<void> {
    await this.db
      .updateTable('accounts')
      .set({ last_seen_at: sql<Date>`now()` })
      .where('id', '=', accountId)
      .execute();
  }

  /**
   * Renames an account. Owners: registered accounts only, once per rename
   * interval (the first rename is free). Moderators (`bypassLimits`) may rename
   * anyone at any time.
   */
  async rename(account: Account, name: string, options: { bypassLimits?: boolean } = {}) {
    checkDisplayName(name);
    if (!options.bypassLimits) {
      if (account.kind !== 'registered') {
        throw apiError(
          'forbidden',
          'Guests get generated names; link a sign-in method to choose one.',
        );
      }
      const availableAt = this.renameAvailableAt(account);
      if (availableAt) {
        throw apiError('rate_limited', 'Display names can only be changed once per interval.', {
          renameAvailableAt: availableAt.toISOString(),
        });
      }
    }
    if (account.kind === 'registered' && GUEST_NAME.test(name)) {
      throw apiError('bad_request', 'Names like Guest-1234 are reserved for guests.');
    }
    if (name === account.display_name) return account;
    try {
      return await this.db
        .updateTable('accounts')
        .set({
          display_name: name,
          updated_at: sql<Date>`now()`,
          ...(options.bypassLimits ? {} : { display_name_changed_at: sql<Date>`now()` }),
        })
        .where('id', '=', account.id)
        .returningAll()
        .executeTakeFirstOrThrow();
    } catch (error) {
      if (isUniqueViolation(error, NAME_INDEX)) {
        throw apiError('conflict', 'That display name is taken.', { reason: 'name_taken' });
      }
      throw error;
    }
  }

  /**
   * Resolves a sign-in identity to an account. With `current` and mode `link`,
   * a new identity is linked to `current` (a guest becomes registered in place),
   * and an identity owned by a different account is a conflict: accounts are
   * never merged. Otherwise the identity's account signs in, created if new.
   */
  async resolveIdentity(
    identity: ProviderIdentity,
    options: IdentityOptions,
  ): Promise<IdentityOutcome> {
    for (let attempt = 0; ; attempt++) {
      try {
        return await this.db.transaction().execute((tx) => this.resolveIn(tx, identity, options));
      } catch (error) {
        // Concurrent sign-ins of the same new identity, or a name taken
        // between check and insert: retry; the next round sees the winner.
        if (isUniqueViolation(error) && attempt < 3) continue;
        throw error;
      }
    }
  }

  private async resolveIn(
    tx: Transaction<Database>,
    identity: ProviderIdentity,
    options: IdentityOptions,
  ): Promise<IdentityOutcome> {
    const existing = await tx
      .selectFrom('identities')
      .select(['id', 'account_id'])
      .where('provider', '=', identity.provider)
      .where('subject', '=', identity.subject)
      .executeTakeFirst();
    const current = options.mode === 'link' ? options.current : undefined;
    if (existing && options.createOnly) {
      throw apiError('conflict', 'That username is taken.', { reason: 'username_taken' });
    }
    if (existing) {
      const owner = await this.requireUsable(existing.account_id, tx);
      if (current && owner.id !== current.id) {
        return {
          kind: 'conflict',
          conflict: {
            reason: 'identity_in_use',
            provider: identity.provider,
            account: this.publicView(owner),
          },
        };
      }
      await tx
        .updateTable('identities')
        .set({
          last_used_at: sql<Date>`now()`,
          ...(identity.email ? { email: identity.email } : {}),
        })
        .where('id', '=', existing.id)
        .execute();
      return {
        kind: 'signed-in',
        account: owner,
        linked: owner.id === current?.id,
        created: false,
      };
    }

    let account: Account;
    let created = false;
    if (current) {
      const fresh = await this.requireUsable(current.id, tx);
      if (fresh.kind === 'guest') {
        // Upgrade in place: same id, so history and ratings carry over.
        const name = await this.uniqueRegisteredName(tx, identity.name, fresh.display_name);
        account = await tx
          .updateTable('accounts')
          .set({ kind: 'registered', display_name: name, updated_at: sql<Date>`now()` })
          .where('id', '=', fresh.id)
          .returningAll()
          .executeTakeFirstOrThrow();
      } else {
        account = fresh;
      }
    } else {
      const name = await this.uniqueRegisteredName(tx, identity.name);
      account = await tx
        .insertInto('accounts')
        .values({ kind: 'registered', display_name: name })
        .returningAll()
        .executeTakeFirstOrThrow();
      created = true;
    }
    await tx
      .insertInto('identities')
      .values({
        account_id: account.id,
        provider: identity.provider,
        subject: identity.subject,
        email: identity.email ?? null,
        password_hash: options.passwordHash ?? null,
        last_used_at: sql<Date>`now()`,
      })
      .execute();
    return { kind: 'signed-in', account, linked: current !== undefined, created };
  }

  /** A free registered display name: the preferred one, a numbered variant, or Player-NNNNNN. */
  private async uniqueRegisteredName(
    db: Db,
    ...preferences: (string | undefined)[]
  ): Promise<string> {
    const taken = async (name: string) =>
      (await db
        .selectFrom('accounts')
        .select('id')
        .where(sql<string>`lower(display_name)`, '=', name.toLowerCase())
        .where('kind', '=', 'registered')
        .where('status', '<>', 'deleted')
        .executeTakeFirst()) !== undefined;
    for (const preference of preferences) {
      const base = sanitizeName(preference);
      if (!base) continue;
      if (!(await taken(base))) return base;
      for (let i = 0; i < 5; i++) {
        const suffix = ` ${randomInt(10, 10000)}`;
        let stem = base;
        while (utf8ByteLength(stem + suffix) > MAX_PLAYER_NAME_BYTES)
          stem = [...stem].slice(0, -1).join('');
        const candidate = `${stem.trim()}${suffix}`;
        if (!(await taken(candidate))) return candidate;
      }
    }
    for (;;) {
      const candidate = `Player-${randomInt(100000, 1000000)}`;
      if (!(await taken(candidate))) return candidate;
    }
  }

  async localIdentity(subject: string) {
    return this.db
      .selectFrom('identities')
      .select(['account_id', 'password_hash'])
      .where('provider', '=', 'local')
      .where('subject', '=', subject)
      .executeTakeFirst();
  }

  /** Finds an account by id or (case-insensitive) exact display name. */
  async findByIdOrName(reference: string): Promise<Account[]> {
    const byId = await this.get(reference);
    if (byId) return [byId];
    return this.db
      .selectFrom('accounts')
      .selectAll()
      .where(sql<string>`lower(display_name)`, '=', reference.toLowerCase())
      .where('status', '<>', 'deleted')
      .orderBy('created_at')
      .execute();
  }
}
