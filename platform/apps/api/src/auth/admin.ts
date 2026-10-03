// Moderation actions shared by the admin REST endpoints and the `platform
// admin` CLI. Every action is written to admin_audit_log; a null actor means
// the server's command line.
import { sql, type Kysely } from 'kysely';
import type { Account, Database, JsonValue } from '@glob2/db';
import type { AdminAccount } from '@glob2/protocol';
import { apiError } from '../errors.ts';
import type { AccountService } from './accounts.ts';

export type Role = Account['role'];

const RANK: Record<Role, number> = { user: 0, moderator: 1, admin: 2 };

/** What a deleted account is called everywhere it still appears. */
export const DELETED_NAME = 'Deleted player';

export function hasRole(account: Account, role: Role): boolean {
  return RANK[account.role] >= RANK[role];
}

export interface AdminEffects {
  /** Ends every session of the account (tokens, web sessions, sockets). */
  endSessions(accountId: string, reason: string): Promise<void>;
}

export class AdminService {
  private readonly db: Kysely<Database>;
  private readonly accounts: AccountService;
  private readonly effects: AdminEffects;

  constructor(db: Kysely<Database>, accounts: AccountService, effects: AdminEffects) {
    this.db = db;
    this.accounts = accounts;
    this.effects = effects;
  }

  async audit(
    actor: Account | undefined,
    action: string,
    target: Account,
    details: Record<string, JsonValue> = {},
  ): Promise<void> {
    await this.db
      .insertInto('admin_audit_log')
      .values({
        actor_account_id: actor?.id ?? null,
        action,
        target_type: 'account',
        target_id: target.id,
        details: JSON.stringify(details),
      })
      .execute();
  }

  async view(account: Account): Promise<AdminAccount> {
    return {
      ...(await this.accounts.selfView(account)),
      updatedAt: account.updated_at.toISOString(),
      ...(account.last_seen_at ? { lastSeenAt: account.last_seen_at.toISOString() } : {}),
    };
  }

  /** Accounts whose name contains `query` (or whose id or linked email equals it), newest first. */
  async search(query: string, limit: number, before?: Date): Promise<Account[]> {
    let select = this.db.selectFrom('accounts').selectAll('accounts');
    const q = query.trim();
    if (q) {
      const pattern = `%${q.toLowerCase().replace(/[\\%_]/g, (c) => `\\${c}`)}%`;
      select = select.where((eb) =>
        eb.or([
          eb(sql<string>`lower(accounts.display_name)`, 'like', pattern),
          eb(sql<string>`accounts.id::text`, '=', q.toLowerCase()),
          eb.exists(
            eb
              .selectFrom('identities')
              .select('identities.id')
              .whereRef('identities.account_id', '=', 'accounts.id')
              .where(sql<string>`lower(identities.email)`, '=', q.toLowerCase()),
          ),
        ]),
      );
    }
    if (before) select = select.where('accounts.created_at', '<', before);
    return select.orderBy('accounts.created_at', 'desc').limit(limit).execute();
  }

  async setRole(actor: Account | undefined, target: Account, role: Role): Promise<Account> {
    if (actor && actor.id === target.id)
      throw apiError('forbidden', 'You cannot change your own role.');
    if (role !== 'user' && target.kind !== 'registered') {
      throw apiError(
        'bad_request',
        'Only registered accounts can be moderators or administrators.',
      );
    }
    const updated = await this.db
      .updateTable('accounts')
      .set({ role, updated_at: sql<Date>`now()` })
      .where('id', '=', target.id)
      .returningAll()
      .executeTakeFirstOrThrow();
    await this.audit(actor, 'account.role', target, { from: target.role, to: role });
    return updated;
  }

  async setBanned(actor: Account | undefined, target: Account, banned: boolean, reason?: string) {
    if (actor && actor.id === target.id) throw apiError('forbidden', 'You cannot ban yourself.');
    if (actor && banned && !(RANK[actor.role] > RANK[target.role])) {
      throw apiError('forbidden', 'You can only ban accounts with a lower role.');
    }
    if (target.status === 'deleted') throw apiError('not_found', 'No such account.');
    const updated = await this.db
      .updateTable('accounts')
      .set({ status: banned ? 'banned' : 'active', updated_at: sql<Date>`now()` })
      .where('id', '=', target.id)
      .returningAll()
      .executeTakeFirstOrThrow();
    await this.audit(
      actor,
      banned ? 'account.ban' : 'account.unban',
      target,
      reason ? { reason } : {},
    );
    if (banned) await this.effects.endSessions(target.id, 'banned');
    return updated;
  }

  async mute(actor: Account | undefined, target: Account, minutes: number, reason?: string) {
    if (actor && minutes > 0 && !(RANK[actor.role] > RANK[target.role])) {
      throw apiError('forbidden', 'You can only mute accounts with a lower role.');
    }
    const until = minutes > 0 ? new Date(Date.now() + minutes * 60_000) : null;
    const updated = await this.db
      .updateTable('accounts')
      .set({ muted_until: until, updated_at: sql<Date>`now()` })
      .where('id', '=', target.id)
      .returningAll()
      .executeTakeFirstOrThrow();
    await this.audit(actor, minutes > 0 ? 'account.mute' : 'account.unmute', target, {
      minutes,
      ...(reason ? { reason } : {}),
    });
    return updated;
  }

  /**
   * Deletes an account (guest or registered). The row stays, marked deleted,
   * because match history, ratings and audit entries refer to it; everything
   * that identifies the person or lets anyone sign in goes:
   *
   * - display name: "Deleted player", also on its past match participations;
   * - sign-in identities (passwords, providers: the username is free again),
   *   device credentials, refresh tokens and web sessions (all revoked now);
   * - its catalog maps (versions, likes, reports, download counts, like other
   *   map deletions; the bytes stay for matches played on them) and its likes
   *   of other maps; queue tickets.
   *
   * Kept: matches (as "Deleted player"), rating rows (hidden: deleted
   * accounts are left out of leaderboards and player pages), uploaded blobs,
   * rooms it is in (left by the presence sweep once its sockets are closed)
   * and the audit log. There is no undo.
   */
  async deleteAccount(
    actor: Account | undefined,
    target: Account,
    reason?: string,
  ): Promise<{ account: Account; removedMaps: number }> {
    if (actor && actor.id === target.id)
      throw apiError('forbidden', 'You cannot delete your own account here.');
    if (actor && !(RANK[actor.role] > RANK[target.role])) {
      throw apiError('forbidden', 'You can only delete accounts with a lower role.');
    }
    if (target.status === 'deleted') throw apiError('not_found', 'No such account.');
    const result = await this.db.transaction().execute(async (tx) => {
      const id = target.id;
      const maps = await tx.deleteFrom('maps').where('owner_account_id', '=', id).execute();
      const liked = await tx
        .deleteFrom('map_likes')
        .where('account_id', '=', id)
        .returning('map_id')
        .execute();
      if (liked.length > 0) {
        await tx
          .updateTable('maps')
          .set({ like_count: sql<number>`greatest(like_count - 1, 0)` })
          .where(
            'id',
            'in',
            liked.map((l) => l.map_id),
          )
          .execute();
      }
      await tx.deleteFrom('identities').where('account_id', '=', id).execute();
      await tx.deleteFrom('device_credentials').where('account_id', '=', id).execute();
      await tx.deleteFrom('queue_tickets').where('account_id', '=', id).execute();
      await tx
        .updateTable('refresh_tokens')
        .set({ revoked_at: sql<Date>`now()` })
        .where('account_id', '=', id)
        .where('revoked_at', 'is', null)
        .execute();
      await tx
        .updateTable('web_sessions')
        .set({ revoked_at: sql<Date>`now()` })
        .where('account_id', '=', id)
        .where('revoked_at', 'is', null)
        .execute();
      await tx
        .updateTable('match_participants')
        .set({ display_name: DELETED_NAME })
        .where('account_id', '=', id)
        .execute();
      const account = await tx
        .updateTable('accounts')
        .set({
          status: 'deleted',
          role: 'user',
          display_name: DELETED_NAME,
          updated_at: sql<Date>`now()`,
        })
        .where('id', '=', id)
        .returningAll()
        .executeTakeFirstOrThrow();
      return { account, removedMaps: Number(maps[0]?.numDeletedRows ?? 0) };
    });
    await this.audit(actor, 'account.delete', target, {
      displayName: target.display_name,
      kind: target.kind,
      removedMaps: result.removedMaps,
      ...(reason ? { reason } : {}),
    });
    await this.effects.endSessions(target.id, 'deleted');
    return result;
  }

  async rename(actor: Account | undefined, target: Account, name: string, reason?: string) {
    const updated = await this.accounts.rename(target, name, { bypassLimits: true });
    await this.audit(actor, 'account.rename', target, {
      from: target.display_name,
      to: name,
      ...(reason ? { reason } : {}),
    });
    return updated;
  }
}
