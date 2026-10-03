# Identity: accounts, sign-in and tokens

The online platform has its own accounts. Players start as guests without
signing up, and can later link a sign-in provider to the same account. There is
no bundled identity-provider service: OpenID Connect providers, Sign in with
Apple and optional local passwords are built into `platform-api`
(`platform/apps/api/src/auth/`). See the [architecture](architecture.md) for the
rest of the platform.

## Accounts

An account (`accounts`) is either a **guest** or **registered**, has a display
name (1-32 UTF-8 bytes, the engine's player-name limit, no control characters
or surrounding spaces), a role (`user`, `moderator`, `admin`) and a status
(`active`, `banned`, `deleted`), plus an optional mute expiry.

- **Guests** are created on first contact: `POST /api/v1/auth/guest` without a
  credential creates one named `Guest-NNNN` and returns a 256-bit device
  credential (base64url, 43 characters) once. The client keeps it in its
  settings (native) or browser storage, per instance. The server stores only its
  SHA-256 (`device_credentials`), so a database leak does not reveal usable
  credentials. Presenting it again signs the same account in, also after the
  guest has been upgraded.
- Guests can play rooms and casual queues. Leaderboards show registered accounts
  only. Keeping guests out of rated queues is a queue rule, not an
  `AccessPolicy` decision.
- **Display names.** Guests cannot choose a name. Names of registered accounts
  are unique per instance, ignoring case (a partial unique index), and names
  like `Guest-1234` are reserved for guests. A new registered account takes the
  name the provider suggests (or the chosen local display name); a taken name
  gets a numbered variant, and without any usable name the account becomes
  `Player-NNNNNN`. Owners rename with `PATCH /api/v1/accounts/me`; the first
  rename is free, later ones wait `accounts.renameIntervalDays` (default 30,
  `0` disables the limit). A rename too early answers `rate_limited` with
  `details.renameAvailableAt`; `SelfAccount.renameAvailableAt` shows it ahead.
- **Linking** a provider to a guest upgrades the same account to registered in
  place, so history and ratings carry over. An identity (`identities`: provider
  id and subject) belongs to exactly one account. If the identity already
  belongs to another account the result is a **conflict** (`IdentityConflict`:
  `reason: identity_in_use`, the provider and the owner's `PublicAccount`).
  Accounts are never merged; the player may switch to the other account instead.

| REST | |
| --- | --- |
| `POST /api/v1/auth/guest` | `GuestSignInRequest` → `SignInResponse` |
| `POST /api/v1/auth/refresh` | `RefreshRequest` → `AuthTokens` |
| `POST /api/v1/auth/sign-out` | `SignOutRequest` → 204; revokes the sign-in |
| `POST /api/v1/auth/local/register` | `LocalRegisterRequest` → `SignInResponse` (with a guest bearer token: upgrade in place) |
| `POST /api/v1/auth/local/sign-in` | `LocalSignInRequest` → `SignInResponse` |
| `POST /api/v1/auth/web/sign-out` | ends the browser's web session |
| `GET /api/v1/accounts/me`, `PATCH …/me` | `SelfAccount`; `UpdateAccountRequest` |
| `DELETE /api/v1/accounts/me/identities/{provider}` | 204; unlinks that sign-in method. A registered account keeps at least one: removing the last is `409 conflict` (`details.reason: last_sign_in_method`); an unlinked provider is `404` |
| `GET /api/v1/accounts/{id}` | `PublicAccount` |
| `GET /.well-known/jwks.json` | `PlatformJwks` |

Authenticated REST calls send `Authorization: Bearer <access token>`. Errors are
`ErrorBody` documents (`unauthenticated` 401, `forbidden` 403, `conflict` 409,
`rate_limited` 429, …).

## Sign-in providers

Providers are configured in `instance.yaml` under `auth.providers`; their
secrets stay in the environment and are referenced by variable name (see
`platform/instance.example.yaml`). Every provider's redirect URI is
`<PUBLIC_ORIGIN>/auth/<id>/callback`; register exactly that with the provider.

| Kind | Notes |
| --- | --- |
| `oidc` | Generic OpenID Connect, authorization code with PKCE (S256), state and nonce, via `openid-client`. `preset: google` fills in `https://accounts.google.com`; `preset: microsoft` fills in `https://login.microsoftonline.com/<tenant>/v2.0` (`tenant` defaults to `common`; the ID token's issuer is checked against its own `tid`). Any other issuer is set with `issuer`. The client secret is optional (public clients). The identity is the issuer's `sub`. |
| `apple` | Sign in with Apple: the client secret is a fresh ES256 JWT (5 minutes, `iss` team id, `sub` Services ID, `aud` Apple) signed with the operator's `.p8` key from the environment. The response arrives by `form_post`, a cross-site POST to the callback, protected by `state`; the name comes from the first sign-in's `user` field. Apple does not document PKCE for this flow, so it is not used. |
| local | Username and password, off by default (`auth.local.enabled`, with `allowRegistration`), for self-hosted instances without single sign-on. Usernames are 3-32 of `A-Z a-z 0-9 . _ -`, compared case-insensitively (the identity's subject is the lowercase form). Passwords have 10-256 characters and are hashed with argon2id (19 MiB, 2 passes, 1 lane; PHC strings, so the cost can rise later). Unknown usernames take as long as wrong passwords. |

Provider flows keep their state server-side (`auth_flows`: hash of `state`,
PKCE verifier, nonce, purpose, the attempt and browser binding), consumed once
and expiring after 15 minutes. A provider that fails discovery or lacks its
secret is left out (logged) instead of stopping the API.

`GET /api/v1/instance` lists the configured providers (`InstanceInfo.authProviders`),
including `{id: "local", kind: "local"}` when local accounts are on, so clients
can show the right buttons.

## Browser handoff sign-in

Providers are only ever used in a real browser, which works the same on desktop,
phones and the web client, and needs no loopback server in the game.

1. The client, connected to `/realtime`, sends `auth.handoff.begin` (optionally
   naming a provider, and a `mode`). The platform records a `signin_attempts`
   row and answers with `attemptId`, a `signInUrl` (`<origin>/signin?attempt=…`),
   a 6-character `confirmationCode`, an expiry (`auth.handoffMinutes`, default
   10) and a `resumeToken`.
2. The client opens the system browser at `signInUrl` and shows the code.
3. `/signin` (server-rendered by the API, no scripts, strict CSP, no cross-site Referer)
   first asks the player to **type the code the game shows** (`POST
   /signin/confirm`; case, spaces and dashes do not matter) and offers nothing
   else until they do. The page never shows the code itself. The right code binds
   the attempt to that browser (a random cookie whose hash is stored on the
   attempt; other browsers are then refused with `409`); five wrong codes fail the
   attempt (`denied`). Then the page offers the provider buttons and, if enabled, the local password forms
   (separate Sign in and Create account forms, `current-password` and
   `new-password`). A problem with a local form (short password, invalid or taken
   username, unknown username, wrong password) re-renders this page with the
   message on the field and the username kept, never the password; the status is
   still `400`, `401`, `403` or `409`. Other sign-in problems get a page that links
   back to `/signin`.
4. The player signs in at the provider; the callback resolves the identity:
   - on an authenticated socket (mode `link`, the default there) a new identity
     is linked to that account, upgrading a guest in place;
   - on an unauthenticated socket, or with mode `signin`, the identity's
     account signs in, created if new;
   - an identity owned by another account is a conflict: the page names the
     owner and offers "Play as <owner> instead" or "Keep my current account".
5. The platform completes the attempt and notifies every replica (NOTIFY on
   the `realtime` channel). The replica holding the socket claims delivery
   once (`delivered_at`), mints the tokens and pushes `auth.handoff.completed`
   (`session`: account and tokens; `linked`: whether the identity joined the
   socket's previous account), and the socket is authenticated as that
   account. Refusal at the provider, cancellation (`auth.handoff.cancel` or the
   page's Cancel), a declined conflict or expiry produce `auth.handoff.failed`
   with `reason` `denied`, `cancelled`, `conflict` (with `conflict`) or
   `expired`. The browser also gets a web session.

Tokens are only minted at delivery, so nothing secret is stored or sent through
NOTIFY. Phones often lose the socket while the browser is in front: pending
attempts survive a closed socket, and `auth.handoff.resume {attemptId,
resumeToken}` on a new socket (any replica) re-attaches it, delivering at once
if the attempt already finished. At most three attempts may be pending per
socket. The worker's maintenance marks attempts past their expiry and deletes
them a week later.

Typing the code is the defence against a link sent by someone else (RFC 8628
§5.4): whoever completes the sign-in signs the game that started it in, so an
attacker could start an attempt and send its link to a victim. With only the
link the victim's browser offers no way to sign in; the attacker would also have
to talk them into typing a code, which the page warns against. Starting attempts
needs no account, so they are limited per address
(`limits.signinAttemptsPerHour`, 30) and in total
(`limits.signinAttemptsPerMinuteTotal`, 300), on every replica.

## Tokens

- **Access tokens** are EdDSA (Ed25519) JWTs with header
  `{alg: EdDSA, typ: at+jwt, kid}` and claims `AccessTokenClaims`: `iss` (the
  instance origin), `aud: glob2-platform`, `sub` (account id), `jti`, `iat`,
  `exp` (`auth.accessTokenMinutes`, default 10), `client_id` (client platform),
  `sid` (the sign-in, i.e. refresh-token family), `kind` and `role`. Clients send
  them in `session.hello`/`session.authenticate` and as HTTP bearer tokens. The
  platform re-reads the account on every use, so a ban, a role change or a
  revoked sign-in takes effect immediately; the `kind`/`role` claims are
  informational. Relays never accept access tokens: they verify only tickets
  (`typ: glob2-match+jwt`, audience `glob2-relay`).
- **Refresh tokens** are opaque 256-bit secrets stored as SHA-256 hashes in
  `refresh_tokens` and rotated on every use (`POST /api/v1/auth/refresh`,
  lifetime `auth.refreshTokenDays`, default 60). Each sign-in starts a family;
  presenting an already-rotated token is treated as theft and revokes the whole
  family, including its access tokens, and sockets authenticated with it get
  `session.revoked`. Sign-out revokes the presented token's family. Clients must
  therefore serialise refreshes per sign-in. Expired tokens are deleted 30 days
  after expiry.
- **Web sessions** (the browser after `/signin`) are a random secret in an
  HttpOnly `SameSite=Lax` cookie (`__Host-glob2_session` over HTTPS), stored
  hashed in `web_sessions` (`auth.webSessionDays`, default 30). Cookie-
  authenticated requests that change state must come from an allowed origin.
- **Match tickets** (M4) are EdDSA JWTs with `typ: glob2-match+jwt` and audience
  `glob2-relay`, carrying the match id, seat, account, sim version, the match's
  human seats, relay URL, entitlements and a short expiry (`MatchTicketClaims`),
  signed with the same keys.

### Keys and JWKS

The API loads Ed25519 private keys from `JWT_KEYS_DIR` (files `<kid>.pem`,
PKCS#8; `<kid>.pub.pem` holds the public half of a retired key) and/or one key
from `JWT_PRIVATE_KEY` with `JWT_KEY_ID`. With several private keys,
`JWT_ACTIVE_KID` names the signing key. `npm run platform -- keys generate
--kid <id> --dir keys` writes a new key. Only an instance whose `PUBLIC_ORIGIN`
is `http://localhost` starts without keys (with a throwaway one).

`GET /.well-known/jwks.json` (cacheable for 5 minutes) lists every key as an
OKP JWK (`kty: OKP, crv: Ed25519, x, kid, alg: EdDSA, use: sig`), the signing
key first. Relays and other verifiers fetch and cache it, refetching when they
meet an unknown `kid`. To rotate: add the new key while the old one signs (wait
for caches to pick it up), set `JWT_ACTIVE_KID` to it, and after the longest
token lifetime replace the old private key by its `.pub.pem` or remove it.

Verifiers reject tokens that are not `EdDSA`, have another `typ`, name an
unknown `kid`, are expired (30 s leeway) or have the wrong audience.
`platform/packages/protocol/fixtures/tickets/` contains a test JWKS and one
ticket for each of these cases.

## Realtime sessions

`/realtime` carries the envelope described in the [architecture](architecture.md#realtime-messages).
Identity-related methods: `session.hello` (optionally with an access token;
an invalid token answers `unauthenticated` and the client may retry without
it), `session.authenticate` (attach or replace the token, e.g. after a
refresh), `session.ping`, `auth.handoff.begin`, `auth.handoff.resume` and
`auth.handoff.cancel`. Events: `session.revoked` (sign-out elsewhere, token
reuse, ban; a ban also closes the socket), `auth.handoff.completed`,
`auth.handoff.failed`.

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
YOG chat commands did; the web admin pages come in M8.

| Endpoint | Role | |
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

### Deleting an account

Players delete their own account on the web account page (`/account`; the game's
Settings link "Delete my account" opens it), which calls `DELETE
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
  [database roles](../hosting/README.md#database-roles)); the deletion's own entry
  records no name;
- removes its sign-in identities (so a username can be registered again; e-mail
  addresses go with them) and device credentials, and revokes its refresh tokens
  and web sessions (open sockets get `session.revoked` and close);
- deletes its catalog maps, like a map deletion (versions, likes, reports and
  download counts go; the bytes stay for matches played on them), its likes of
  other maps, its uploads, and its queue tickets.

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
| Refresh tokens | Until 30 days after they expire (reuse detection), then deleted. |
| Web sessions | Until 30 days after they expire or are revoked. |
| Provider sign-in flows | 24 hours after they expire. |
| Browser sign-in attempts | A week after they expire. |
| Rate-limit counters | A day after their last use. |
| Queue tickets | Expired after an hour of waiting. |
| Guest accounts, chat, matches, maps | Until the player deletes the account (or a moderator does); no automatic expiry yet. |

## Hardening

- Rate limits per client address that hold across every API replica (sliding
  windows in Postgres, `rate_limits`): each sign-in route (`limits.authPerMinute`,
  default 30 per route and address), new guests (`limits.guestsPerHour`, 20),
  browser sign-in attempts (above), uploads (30 per account per hour), map
  catalog writes, invite-code misses and chat. Wrong local passwords count per
  username (`limits.passwordFailuresPerAccount`, 10 per 15 minutes; past that the
  username is locked for the rest of the window, even for the right password) and
  per address (`limits.passwordFailuresPerIp`, 50 per hour, over all usernames);
  a correct password clears its username's count. The general cap on other
  routes (`limits.apiPerMinute`, default 600) is per replica, in memory.

## Hardening

- Realtime sockets: a token bucket per
  socket (`realtimePerSecond` 10, `realtimeBurst` 40; excess requests answer
  `rate_limited`, persistent excess closes the socket), at most
  `realtimeConnectionsPerIp` (20) sockets per address, frames up to 64 KiB, text
  only, and a ping every 30 s (a socket that misses a pong is dropped).
- `/realtime` upgrades must carry an allowed `Origin` (the public origin plus
  `web.allowedOrigins`) or none (native clients). Cookie-authenticated
  state-changing requests and the `/signin` forms check `Origin` (or
  `Sec-Fetch-Site`) the same way. The handoff binding cookie is
  `SameSite=None` over HTTPS only so that Apple's `form_post` callback still
  carries it; it authenticates nothing by itself.
- The API sits behind a reverse proxy (`trustProxy`), which must route
  `/api`, `/realtime`, `/signin`, `/auth/` and `/.well-known/` to it.
- Uploads are checked against the account's quota before they are unpacked;
  gzip is unpacked off the event loop, up to 64 MiB and 256 times the packed size.
- Logs redact authorization headers, cookies, tokens, device credentials,
  passwords and tickets.
- Credentials are per instance: a client connected to a self-hosted instance
  never sends the official instance's tokens there, and vice versa.

## What is verified

Vitest suites under `platform/apps/api/test/` run against Postgres with two API
replicas and an in-test OpenID Connect issuer (`mockIssuer.ts`): guest creation
and return, token rotation and reuse detection, JWKS verification, key
rotation, local registration and sign-in, renames, admin CLI and role checks,
rate limits, the realtime envelope, origin checks, heartbeat, cross-replica
fan-out, shared rate limits and password lockout across replicas (`abuse.test.ts`),
self-service deletion and name scrubbing (`deletion.test.ts`), and the handoff
(code entry and wrong-code lockout, link, conflict and switch, browser binding, refusal,
cancellation, resume after a dropped socket, expiry, Apple `form_post` with a
verified ES256 client secret, local passwords on the page). Against the real
Google and Microsoft endpoints only discovery and the authorization redirect
have been exercised; complete sign-ins with real Google, Microsoft and Apple
accounts need registered client ids and remain to be tested.
