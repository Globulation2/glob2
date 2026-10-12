# Account administration and data lifecycle

Administrator endpoints, self-service export/deletion and retention guarantees. The [privacy policy](../mobile/privacy-policy.md) describes the official service to players.

## Administration

The first administrator is granted on the server:

```sh
cd platform
npm run platform -- admin grant <account id or exact display name>   # admin
npm run platform -- admin grant <account> --role moderator
npm run platform -- admin revoke <account>                           # back to user
npm run platform -- admin ban <account> [--reason <text>]
npm run platform -- admin delete <account> [--reason <text>]
```

Only registered accounts can hold a role. Minimal REST endpoints cover what the
old YOG lobby's admin chat commands did. The web moderation interface is available
at `/admin`; see [administration and reporting](../hosting/admin-reporting.md).

| Endpoint | Role |  |
| --- | --- | --- |
| `GET /api/v1/admin/accounts?q=&cursor=` | moderator | search by name fragment, id or linked email (`AdminAccountList`) |
| `GET /api/v1/admin/accounts/{id}` | moderator | `AdminAccount` |
| `POST …/{id}/rename` | moderator | `AdminRenameRequest`; bypasses the rename interval |
| `POST …/{id}/mute` | moderator | `AdminMuteRequest`; `minutes: 0` lifts it |
| `POST …/{id}/ban` | admin | `AdminBanRequest`; ends every session of the account at once |
| `POST …/{id}/role` | admin | `AdminRoleRequest` |
| `DELETE …/{id}?reason=` | admin | deletes the account (`204`), see below |

Nobody can change their own role or ban themselves, and mutes and bans apply
only to accounts of a lower role. Every action is recorded in
`admin_audit_log` (a null actor is the command line).

### Downloading your data

The web account page (`/account`; the game's Settings row "Download or delete my
data" opens it) has a **Download my data** link to `GET /api/v1/accounts/me/export`.
It answers, for the signed-in account (session cookie or bearer token), a JSON file
(`AccountExport`, `format: "glob2-account-export/1"`, sent as an attachment with
`Cache-Control: no-store`) holding every stored row about the account, read in one
snapshot (`apps/api/src/auth/accountExport.ts`):

| Section | Contents |
| --- | --- |
| `account` | id, kind, display name, role, status, creation, last seen, last rename, mute |
| `signIn` | linked identities (provider, subject, e-mail; for local accounts the username), device credentials, refresh tokens and web sessions (platform and times), browser sign-in attempts |
| `entitlements`, `moderation` | entitlements; moderation actions about the account (action, details, time; not who took them) |
| `ratings`, `ratingHistory` | ratings per ladder, and every rating change |
| `matches` | every match played: the match's origin, status, result, times, map hash, and the account's seat, team, name, outcome, disconnects, rating change and connection-quality summary, with the match page URL |
| `rooms` | rooms hosted (with their settings), memberships (with server-region round trips), seats, own chat messages, kicks |
| `matchmaking` | queue tickets (with region round trips), cooldowns, quick-match proposals and responses |
| `skins` | published paints and immutable version metadata (including the swarm mesh), equipped version and building color, private draft (name, building color, swarm mesh) with its colour atlas and material map bytes as base64 (`imageBase64`, `materialBase64`), match appearances, purchases and payment-event references, reports filed |
| `maps` | catalog maps with their versions, likes, reports filed, uploads, and download days |

Rows keep the database's columns in camelCase and leave out nulls. Left out on
purpose: password, credential and token hashes, token families, sign-in
confirmation codes, other players' ids (who moderated, kicked or resolved), other
players' chat, signed appearance assertions, checkout recovery bookkeeping, and
published file bytes (replays, maps and skin images have their own downloads).
Private skin drafts include both images' stored bytes because they are not
published. New drafts store lossless WebP; older drafts may still contain PNG.
Rate-limit counters (keyed by address or account, kept a day) are not exported, and
server logs are outside the database. Each account may export 10 times an hour
(`429` beyond). `apps/api/test/accountExport.test.ts` fails if a column referring to
`accounts` is added without being exported or listed as deliberately left out.

### Deleting an account

Players delete their own account on the web account page (`/account`; the game's
Settings row "Download or delete my data" opens it), which calls `DELETE
/api/v1/accounts/me` with `{"confirmDisplayName": "<current display name>"}`
(`DeleteAccountRequest`; a mismatch is `400` with `reason: confirmation_mismatch`).
Moderators use `DELETE /api/v1/admin/accounts/{id}` or `platform admin delete`.
Both run `AdminService.deleteAccount`, which keeps the row, marked `deleted` with
`deleted_at`, because matches, ratings and the audit log refer to it by id. In one
transaction it:

- renames it "Deleted player", also on its past match participations and its
  seats in stored match setups (`matches.setup` and the copy in the match's
  verify-match job). A match still running or awaiting its verdict keeps the
  setup the relay recorded until it is settled; the worker then scrubs it
  (`account_name_scrubs`, at most a week later);
- deletes its room chat messages and replaces every name it went by (current,
  in past matches, and in renames) with "Deleted player" in other players'
  messages in rooms it was in and in the names of rooms it hosted (whole words,
  ignoring case);
- replaces those names in the free text of audit-log entries about it (the
  entries, actors, ids and times stay; the log is otherwise append-only, see
  [database roles](../hosting/stack.md#database-roles)); the deletion's own entry
  records no name;
- removes its sign-in identities (so a username can be registered again; e-mail
  addresses go with them) and device credentials, and revokes its refresh tokens
  and web sessions (open sockets get `session.revoked` and close);
- deletes its catalog maps, like a map deletion (versions, likes, reports and
  download counts go; the bytes stay for matches played on them), its likes of
  other maps, its uploads, and its queue tickets;
- removes its AI Map Studio projects, prompts, worker checkpoints, stage events
  and artifact records, and cancels their native import jobs. Pending generation
  reservations are released without charging; credit purchases and ledger entries
  remain as financial records. Anonymous daily provider-call totals retain service
  budget usage without retaining the private journal. Unreferenced stage image bytes are collected under
  the normal blob retention policy. Concurrent project creation and late worker
  completions cannot restore the deleted projects.

There is no undo. What stays, and why:

| Kept | Why |
| --- | --- |
| The account row (id, kind, creation time, `deleted` status) | Matches, ratings and audit entries refer to it. |
| Match history and rating rows, by account id, as "Deleted player" | Other players' history and ratings depend on them; deleted accounts are left out of leaderboards and player pages. |
| Binary match records and replays (blobs) | The verified record of games other people played too. The engine wrote each player's in-game name into them, and they are content-addressed, so the name there stays. |
| Uploaded file bytes (blobs) | Matches played on them refer to them by hash. |
| Map reports it filed, audit entries about it (scrubbed) | Moderation records. |

### Data retention

| Data | Kept |
| --- | --- |
| Refresh tokens | Rotated or revoked: 7 days (reuse detection); expired: 30 days after expiry. |
| Web sessions | 30 days after they expire or are revoked. |
| Provider sign-in flows | 24 hours after they expire. |
| Browser sign-in attempts | 7 days once finished. |
| Rate-limit counters | A day after their last use. |
| Queue tickets | Expired after an hour of waiting; deleted 30 days after they finish. |
| Guest accounts | Deleted when unused for 90 days if they never played a match, host no open room and own no catalog map. |
| Room chat | 30 days. |
| Registered accounts, matches, ratings, catalog maps | Until the player deletes the account (or a moderator does). |

The worker deletes these (`apps/worker/src/maintenance.ts`); the full table, with jobs
and blobs, is in [hosting: retention](../hosting/operations.md#retention). The
[privacy policy](../mobile/privacy-policy.md) states the same periods for players.

[Multiplayer index](README.md) · [Documentation index](../README.md).
