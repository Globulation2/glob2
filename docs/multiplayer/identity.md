# Identity: accounts, sign-in and tokens

The online platform has its own accounts. Players start as guests without
signing up, and can later link a sign-in provider to the same account. There is
no bundled identity-provider service: OpenID Connect providers, Sign in with
Apple and optional local passwords are built into `platform-api`. This guide is
the design; the account tables and protocol shapes exist since M0, and the flows
are implemented in M3. See the [architecture](architecture.md) for the rest of
the platform.

## Accounts

An account (`accounts`) is either a **guest** or **registered**, has a display
name (1-32 characters, the engine's player-name limit), a role (`user`,
`moderator`, `admin`) and a status (`active`, `banned`, `deleted`), plus an
optional mute expiry.

- **Guests** are created on first contact. The server returns a 256-bit device
  credential (base64url, 43 characters) once; the client keeps it in its
  settings (native) or browser storage, per instance. The server stores only its
  SHA-256 (`device_credentials`), so a database leak does not reveal usable
  credentials. Presenting it again signs the guest in (`POST /api/v1/auth/guest`).
- Guests can play rooms and casual queues. Leaderboards show registered accounts
  only. Keeping guests out of rated queues is a queue rule, not an
  `AccessPolicy` decision.
- **Linking** a provider to a guest upgrades the same account to registered, so
  history and ratings carry over. An identity (`identities`: provider id and
  subject) belongs to exactly one account.

## Sign-in providers

Providers are configured in `instance.yaml` under `auth.providers`; their
secrets stay in the environment and are referenced by variable name.

| Kind | Notes |
| --- | --- |
| `oidc` | Generic OpenID Connect (authorization code with PKCE, via `openid-client`), with `google` and `microsoft` presets that fill in the issuer. The identity is the issuer's `sub`. |
| `apple` | Sign in with Apple: the client secret is a short-lived ES256 JWT signed with the operator's Apple key; responses arrive by `form_post`. |
| local | Username and password, hashed with argon2id, off by default (`auth.local.enabled`), for self-hosted instances without single sign-on. The identity's provider is `local` and its subject the normalized username. |

`GET /api/v1/instance` lists the configured providers (`InstanceInfo.authProviders`)
so clients can show the right buttons.

## Browser handoff sign-in

Providers are only ever used in a real browser, which works the same on desktop,
phones and the web client, and needs no loopback server in the game.

1. The client, connected to `/realtime`, sends `auth.handoff.begin` (optionally
   naming a provider). The platform records a `signin_attempts` row and answers
   with `attemptId`, a `signInUrl` (`/signin?attempt=…`), a short
   `confirmationCode` and an expiry.
2. The client opens the system browser at `signInUrl` and shows the code.
3. The web page shows the same code, so the player can tell the request is
   theirs, and runs the provider sign-in.
4. On success the platform completes the attempt and pushes
   `auth.handoff.completed` (account and tokens) to the waiting socket through
   pub/sub, whichever API replica holds it. Expiry, denial or `auth.handoff.cancel`
   produce `auth.handoff.failed`. Pending attempts expire; the worker's
   maintenance task marks them.

A guest that signs in this way links the provider to its own account, unless the
identity already belongs to another account, in which case the client switches
to that account.

## Tokens

- **Access tokens** are EdDSA (Ed25519) JWTs with header `typ: at+jwt`, valid for
  minutes. Clients send them in `session.hello`/`session.authenticate` and as
  HTTP bearer tokens.
- **Refresh tokens** are opaque random strings, stored as SHA-256 hashes in
  `refresh_tokens` and rotated on every use (`POST /api/v1/auth/refresh`). Each
  sign-in starts a family; presenting an already-rotated token revokes the whole
  family. Sign-out revokes the presented token's family. Expired tokens are
  deleted 30 days after expiry.
- **Match tickets** are EdDSA JWTs with `typ: glob2-match+jwt` and audience
  `glob2-relay`, carrying the match id, seat, account, sim version, the match's
  human seats, relay URL, entitlements and a short expiry (`MatchTicketClaims`).
  A fresh ticket for a running match comes from `match.reconnect`.
- **Keys** come from key files or the environment, each with a `kid`. The
  public keys are served at `/.well-known/jwks.json`; relays fetch and cache it,
  so keys can be rotated by publishing the new key before signing with it and
  retiring the old one after the longest token lifetime.

Relays reject tokens that are not `EdDSA`, have another `typ` (an access token is
never a ticket), name an unknown `kid`, are expired (30 s leeway) or have the
wrong audience. `platform/packages/protocol/fixtures/tickets/` contains a test
JWKS and one ticket for each of these cases.

## Administration

The first administrator is granted from the command line on the server
(`platform admin grant <account>`). A minimal web admin (M8) covers account
search, rename, ban and mute, match lookup and hiding maps; it replaces the YOG
chat commands. Administrative actions are recorded in `admin_audit_log`.

## Hardening

- Rate limits on sign-in, refresh and guest creation (`@fastify/rate-limit`).
- Web sessions use `SameSite` cookies; WebSocket upgrades check `Origin`.
- Logs redact authorization headers, cookies, tokens, device credentials,
  passwords and tickets.
- Credentials are per instance: a client connected to a self-hosted instance
  never sends the official instance's tokens there, and vice versa.
