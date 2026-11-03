// Moderation REST shapes (/api/v1/admin/*). Moderators may search, rename and
// mute; administrators may also ban and change roles. Every action is recorded
// in the admin audit log.
import { Type, type Static } from 'typebox';
import { DisplayName, Open, Strict, Timestamp } from './common.ts';
import { AccountRole, Page, SelfAccount } from './resources.ts';

export const AdminAccount = Type.Intersect([
  SelfAccount,
  Open(
    {
      lastSeenAt: Type.Optional(Timestamp),
      updatedAt: Timestamp,
    },
    { description: 'An account as moderators see it.' },
  ),
]);
export type AdminAccount = Static<typeof AdminAccount>;

/** GET /api/v1/admin/accounts?q=&cursor= */
export const AdminAccountList = Page(AdminAccount, 'Accounts matching a search, newest first.');
export type AdminAccountList = Static<typeof AdminAccountList>;

const Reason = Type.String({ minLength: 1, maxLength: 500 });

/** POST /api/v1/admin/accounts/{id}/rename (bypasses the rename interval and name rules for guests). */
export const AdminRenameRequest = Strict({
  displayName: DisplayName,
  reason: Type.Optional(Reason),
});
/** POST /api/v1/admin/accounts/{id}/mute; minutes 0 lifts a mute. */
export const AdminMuteRequest = Strict({
  minutes: Type.Integer({ minimum: 0, maximum: 525600 }),
  reason: Type.Optional(Reason),
});
/** POST /api/v1/admin/accounts/{id}/ban (administrators only); banned false lifts a ban. */
export const AdminBanRequest = Strict({ banned: Type.Boolean(), reason: Type.Optional(Reason) });
/** POST /api/v1/admin/accounts/{id}/role (administrators only). */
export const AdminRoleRequest = Strict({ role: AccountRole, reason: Type.Optional(Reason) });

export type AdminRenameRequest = Static<typeof AdminRenameRequest>;
export type AdminMuteRequest = Static<typeof AdminMuteRequest>;
export type AdminBanRequest = Static<typeof AdminBanRequest>;
export type AdminRoleRequest = Static<typeof AdminRoleRequest>;
