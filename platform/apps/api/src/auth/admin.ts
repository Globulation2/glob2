// Moderation actions shared by the admin REST endpoints and the `platform
// admin` CLI. Every action is written to admin_audit_log; a null actor means
// the server's command line.
import { sql, type Kysely } from 'kysely';
import type { Account, Database, JsonValue } from '@glob2/db';
import type { AdminAccount } from '@glob2/protocol';
import { scrubMatchNames } from '@glob2/play';
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
  removeAvatars?(keys: string[]): Promise<void>;
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
   * Deletes an account (guest or registered), by a moderator or (`self`) by
   * its owner. The row stays, marked deleted, because match history, ratings
   * and audit entries refer to it by id; everything that identifies the
   * person or lets anyone sign in goes:
   *
   * - display name: "Deleted player", also on its past match participations
   *   and its seats in stored match setups (matches still running or awaiting
   *   their verdict are scrubbed by the worker once settled);
   * - its names in room chat (its own messages are deleted), in the names of
   *   rooms it hosted, and in audit-log entries about it;
   * - sign-in identities (passwords, providers, e-mail addresses: the username
   *   is free again), device credentials, refresh tokens and web sessions (all
   *   revoked now, its sockets closed);
   * - its catalog maps (versions, likes, reports, download counts, like other
   *   map deletions; the bytes stay for matches played on them), its likes of
   *   other maps, its uploads, and its queue tickets.
   *
   * Kept: matches and rating rows by id (deleted accounts are left out of
   * leaderboards and player pages), the binary match records and replays
   * (the verified record of games other people played too; the engine wrote
   * the in-game name into them), reports it filed, and audit entries by id.
   * There is no undo.
   */
  async deleteAccount(
    actor: Account | undefined,
    target: Account,
    reason?: string,
    options: { self?: boolean } = {},
  ): Promise<{ account: Account; removedMaps: number }> {
    if (!options.self) {
      if (actor && actor.id === target.id)
        throw apiError('forbidden', 'You cannot delete your own account here.');
      if (actor && !(RANK[actor.role] > RANK[target.role])) {
        throw apiError('forbidden', 'You can only delete accounts with a lower role.');
      }
    }
    if (target.status === 'deleted') throw apiError('not_found', 'No such account.');
    const result = await this.db.transaction().execute(async (tx) => {
      const id = target.id;
      // Studio delivery takes the wallet before locking its request. Match that
      // order before taking the account lock used by publication and deletion.
      await tx
        .selectFrom('map_wallets')
        .select('account_id')
        .where('account_id', '=', id)
        .forUpdate()
        .execute();
      // Skin publication and draft saves lock this account before committing.
      const current = await tx
        .selectFrom('accounts')
        .select(['status', 'avatar_key', 'gravatar_key'])
        .where('id', '=', id)
        .forUpdate()
        .executeTakeFirstOrThrow();
      if (current.status === 'deleted') throw apiError('not_found', 'No such account.');
      // Fence leased workers before removing their private state. A completion
      // that already holds the wallet finishes first; later completions can no
      // longer find a request or publish a map. Keep financial audit rows.
      const studioRequests = await tx
        .selectFrom('studio_requests')
        .select(['id', 'kind', 'status'])
        .where('account_id', '=', id)
        .orderBy('id')
        .forUpdate()
        .execute();
      const reservedGenerations = studioRequests.filter(
        (r) => r.kind === 'generate' && !['ready', 'failed'].includes(r.status),
      );
      if (reservedGenerations.length) {
        await tx
          .updateTable('map_wallets')
          .set({ reserved: sql`reserved - ${reservedGenerations.length}` })
          .where('account_id', '=', id)
          .execute();
        await tx
          .insertInto('map_ledger')
          .values(
            reservedGenerations.map((r) => ({
              id: `generation:${r.id}`,
              account_id: id,
              amount: 0,
              kind: 'usage' as const,
              details: { requestId: r.id, delivered: false, returned: true, accountDeleted: true },
            })),
          )
          .execute();
      }
      await sql`DELETE FROM engine_jobs WHERE kind='import-ai-map' AND id::text IN (SELECT checkpoints->>'importJob' FROM studio_requests WHERE account_id=${id})`.execute(
        tx,
      );
      await tx.deleteFrom('studio_threads').where('account_id', '=', id).execute();
      // Every name the account went by: now, in its matches, and in renames.
      const pastNames = await tx
        .selectFrom('match_participants')
        .select('display_name')
        .distinct()
        .where('account_id', '=', id)
        .execute();
      const renames = await tx
        .selectFrom('admin_audit_log')
        .select('details')
        .where('target_type', '=', 'account')
        .where('target_id', '=', id)
        .where('action', '=', 'account.rename')
        .execute();
      const names = new Set([target.display_name, ...pastNames.map((p) => p.display_name)]);
      for (const { details } of renames) {
        const d = details as { from?: unknown; to?: unknown };
        for (const n of [d.from, d.to]) if (typeof n === 'string') names.add(n);
      }
      names.delete(DELETED_NAME);
      const nameList = [...names].filter((n) => n.trim().length > 0);

      await tx.deleteFrom('colony_skin_drafts').where('account_id', '=', id).execute();
      await tx.deleteFrom('colony_skin_equipment').where('account_id', '=', id).execute();
      // Keep immutable version ids for match history, but stop serving the paint.
      await tx
        .updateTable('colony_skins')
        .set({ name: 'Deleted skin', disabled_at: sql<Date>`now()` })
        .where('owner_account_id', '=', id)
        .execute();
      await tx.deleteFrom('ai_uploads').where('owner_account_id', '=', id).execute();
      await tx.deleteFrom('ais').where('owner_account_id', '=', id).execute();
      await tx.deleteFrom('ai_likes').where('account_id', '=', id).execute();
      await tx.deleteFrom('ai_favourites').where('account_id', '=', id).execute();
      await tx.deleteFrom('ai_reports').where('reporter_account_id', '=', id).execute();
      await tx.deleteFrom('ai_downloads').where('downloader', '=', `a:${id}`).execute();
      // Music has no match-history dependency. Removing releases cascades their
      // assets/likes/reports; private source blobs expire within 24 hours.
      await tx.deleteFrom('music_releases').where('owner_id', '=', id).execute();
      await tx.deleteFrom('music_likes').where('account_id', '=', id).execute();
      await tx.deleteFrom('music_reports').where('account_id', '=', id).execute();
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
      await tx.deleteFrom('map_uploads').where('owner_account_id', '=', id).execute();
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

      // Chat: its messages go; its names in other players' messages in the
      // rooms it was in become "Deleted player", as do the names of rooms it hosted.
      const rooms = await tx
        .selectFrom('room_chat_messages')
        .select('room_id')
        .where('account_id', '=', id)
        .union(tx.selectFrom('room_members').select('room_id').where('account_id', '=', id))
        .union(tx.selectFrom('rooms').select('id as room_id').where('host_account_id', '=', id))
        .execute();
      await tx.deleteFrom('room_chat_messages').where('account_id', '=', id).execute();
      if (nameList.length > 0 && rooms.length > 0) {
        const roomIds = rooms.map((r) => r.room_id);
        await sql`
          UPDATE room_chat_messages
          SET text = left(regexp_replace(text, name_pattern(${nameList}::text[]), ${DELETED_NAME}, 'gi'), 500)
          WHERE room_id = ANY(${roomIds}::uuid[])
            AND text ~* name_pattern(${nameList}::text[])`.execute(tx);
        await sql`
          UPDATE rooms
          SET name = left(regexp_replace(name, name_pattern(${nameList}::text[]), ${DELETED_NAME}, 'gi'), 64)
          WHERE host_account_id = ${id} AND name ~* name_pattern(${nameList}::text[])`.execute(tx);
      }

      await tx
        .updateTable('match_participants')
        .set({ display_name: DELETED_NAME })
        .where('account_id', '=', id)
        .execute();
      const matches = await tx
        .selectFrom('match_participants')
        .select('match_id')
        .distinct()
        .where('account_id', '=', id)
        .execute();
      await scrubMatchNames(
        tx,
        id,
        matches.map((m) => m.match_id),
      );

      const account = await tx
        .updateTable('accounts')
        .set({
          avatar_source: 'initials',
          avatar_key: null,
          gravatar_key: null,
          gravatar_fingerprint: null,
          gravatar_checked_at: null,
          status: 'deleted',
          role: 'user',
          display_name: DELETED_NAME,
          deleted_at: sql<Date>`now()`,
          updated_at: sql<Date>`now()`,
        })
        .where('id', '=', id)
        .returningAll()
        .executeTakeFirstOrThrow();
      const removedMaps = Number(maps[0]?.numDeletedRows ?? 0);
      await tx
        .insertInto('admin_audit_log')
        .values({
          actor_account_id: actor?.id ?? null,
          action: 'account.delete',
          target_type: 'account',
          target_id: id,
          details: JSON.stringify({
            kind: target.kind,
            removedMaps,
            ...(options.self ? { self: true } : {}),
            ...(reason ? { reason } : {}),
          }),
        })
        .execute();
      // Last, so a moderator's reason is covered too; ids stay.
      if (nameList.length > 0) {
        await sql`SELECT scrub_audit_log_account(${id}::uuid, ${nameList}::text[])`.execute(tx);
      }
      return {
        account,
        removedMaps,
        avatarKeys: [current.avatar_key, current.gravatar_key].filter(
          (key): key is string => key !== null,
        ),
      };
    });
    await this.effects.removeAvatars?.(result.avatarKeys);
    await this.effects.endSessions(target.id, 'deleted');
    return { account: result.account, removedMaps: result.removedMaps };
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
