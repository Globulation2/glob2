# Self-hosting a Globulation 2 online instance

One Docker Compose stack, `deploy/compose.yaml`, runs every online feature: accounts
and sign-in, rooms and invite links, match relays, quick match, verification,
ratings and history. This guide covers setting it up from nothing, operating it, and
upgrading it. The design behind the services is in the
[platform architecture](../multiplayer/architecture.md).

Until the YOG cutover (milestone M9), the old lobby and router keep running from
their own stack, `deploy/compose.legacy.yaml`, described in
[deploy/README.md](../../deploy/README.md). See [Legacy YOG](#legacy-yog-until-the-cutover).

## The stack

```
             internet ── 80/443 ──► caddy ─┬─ /api /realtime /signin /auth /.well-known /j ─► platform-api ×N ─┐
                                           ├─ /relay/<relay id> (WebSocket) ──────────────► relay ×N ──────────┤ /internal (backend only)
                                           ├─ /play/  WebAssembly client (static)                              │
                                           └─ /       web app (static)                                         ▼
  platform-worker ×1..N ── postgres 16 ◄── engine-agent ×N (one image per sim version) ◄── blob volume ──► platform-api
```

| Service | Image (Dockerfile target) | Replicas | Role |
| --- | --- | --- | --- |
| `caddy` | `caddy` | 1 | TLS (ACME or local CA), static web app and web client, routing. The only service with published ports. |
| `postgres` | `postgres:16-alpine` | 1 | All state, the job queue, pub/sub and the matchmaker's leader lock. |
| `init` | `platform` | one-shot | Creates the first signing key and the relay key, then applies database migrations. Runs before the platform starts on every `up`. |
| `platform-api` | `platform` | `GLOB2_API_REPLICAS` (2) | REST, realtime WebSocket, sign-in pages, JWKS, and `/internal` for relays. Stateless. |
| `platform-worker` | `platform` | `GLOB2_WORKER_REPLICAS` (1) | Engine-job results, ratings, matchmaker and schedules (the scheduler runs on one replica at a time). |
| `engine-agent` | `engine-agent` | `GLOB2_ENGINE_AGENT_REPLICAS` (1) | Map generation, validation, previews and match verification with the headless `glob2` binary of one sim version. |
| `relay` | `relay` | `GLOB2_RELAY_REPLICAS` (1) | Match WebSockets ([relay](../multiplayer/relay.md)). |

Networks:

- `backend` is internal (no route to the internet). Every service is on it; Caddy
  has a fixed address there (`GLOB2_PROXY_ADDRESS`) that relays trust for
  `X-Forwarded-For`.
- `egress` gives `platform-api` outbound access for sign-in providers (OIDC
  discovery, token exchange, Apple keys).
- `public` carries Caddy's published ports and its ACME traffic.

Volumes (Compose project `glob2-platform`, so named `glob2-platform_<volume>`):

| Volume | Contents | Back up? |
| --- | --- | --- |
| `postgres-data` | The database | Yes, with `pg_dump` (below) |
| `blobs` | Maps, saves, previews, match records, replays (content-addressed) | Yes |
| `signing-keys` | Ed25519 private keys (`<kid>.pem`) for access tokens and match tickets | Yes, encrypted |
| `relay-secret` | `relay.key`, the bearer key relays use on `/internal` | Yes, encrypted (or regenerate) |
| `relay-spool` | Match records a relay has not uploaded yet, one directory per relay id | Optional |
| `caddy-data`, `caddy-config` | Certificates, ACME account, local CA | Optional (Caddy re-issues) |

All services except Postgres run as UID/GID 10001 with a read-only root
filesystem, all capabilities dropped and `no-new-privileges`; Postgres runs as its
own UID 70 under the same restrictions. Health checks: `platform-api` `/readyz`
(database reachable), `relay` `/readyz` (ticket keys loaded, not draining), Caddy
`/livez`, Postgres `pg_isready`.

## Requirements

- Docker Engine 25 or newer with Compose v2.24 or newer.
- 2 CPU cores and 4 GB of memory for a small instance; engine jobs (map generation,
  verification) are the heaviest load and scale with `engine-agent` replicas.
- Building the images yourself compiles the game: allow 30-60 minutes and about
  10 GB of disk for the build cache. Prebuilt images avoid this (see
  [Images](#images)).
- For a public instance: a DNS name, and inbound TCP 80 and 443 (UDP 443 for HTTP/3).

## Setup from zero

```sh
git clone https://github.com/<owner>/<repository>.git glob2 && cd glob2/deploy
cp .env.example .env
cp ../platform/instance.example.yaml instance.yaml
```

1. Edit `.env`. At least:
   - `POSTGRES_PASSWORD`: letters and digits only, e.g. `openssl rand -hex 24`;
   - for a local trial keep the defaults (`https://localhost:8443`); for a public
     instance see [DNS and TLS](#dns-and-tls).
2. Edit `instance.yaml`: the instance name, sign-in providers (the example enables
   Google, which needs `GOOGLE_CLIENT_SECRET` in `.env`; remove it or see
   [Sign-in providers](#sign-in-providers)), whether guests may play, and the
   quick-match queues.
3. Optionally build the WebAssembly client to serve "Play in browser" at `/play/`:
   `scons target=web release=1` from the repository root (see
   [browser/README.md](../../browser/README.md)), then set
   `GLOB2_WEB_CLIENT_DIR=../build/emscripten/client/release` in `.env`. Without it
   `/play/` answers 404 and everything else works.
4. Build and start:

   ```sh
   echo "GLOB2_SIM_VERSION=$(python3 sim_version.py)" >> .env   # labels the engine-agent image
   docker compose build
   docker compose up -d --wait
   ```

   On the first start `init` writes a signing key and the relay key into their
   volumes and creates the database schema. `docker compose logs init` shows what
   it did.
5. Open the origin (`https://localhost:8443` locally). For a local instance, trust
   Caddy's local CA in the browser or device you test with:

   ```sh
   docker compose exec caddy cat /data/caddy/pki/authorities/local/root.crt > glob2-local-ca.crt
   ```

6. Make yourself an administrator: sign in once with a provider or a local account
   (guests cannot be administrators), then

   ```sh
   docker compose run --rm --no-deps platform-api platform admin grant "<display name or account id>"
   ```

7. Check the instance: `https://<origin>/api/v1/instance` lists the sim version
   your engine agent serves; a desktop client pointed at the origin can sign in.

`tests/deployment/platform_stack_smoke.py` performs these steps in an isolated
project and checks the result; it is a good first test of a new host as well.

## DNS and TLS

For a public instance at, say, `play.example.org`:

1. Create `A`/`AAAA` records for `play.example.org` pointing at the host.
2. In `.env`:

   ```dotenv
   GLOB2_DOMAIN=play.example.org
   GLOB2_PUBLIC_ORIGIN=https://play.example.org
   GLOB2_BIND=0.0.0.0
   GLOB2_HTTP_PORT=80
   GLOB2_HTTPS_PORT=443
   ```

3. `docker compose up -d`. Caddy obtains and renews a certificate through ACME
   (Let's Encrypt, falling back to ZeroSSL) and redirects HTTP to HTTPS. Port 80
   must be reachable for the HTTP challenge, or 443 for TLS-ALPN.

`GLOB2_TLS_MODE` chooses the issuer: `auto` (default) uses ACME for public names and
Caddy's local CA for `localhost`; `internal` always uses the local CA, for LAN host
names or a staging instance. Clients must trust that CA.

`GLOB2_PUBLIC_ORIGIN` is what every link, redirect URI, cookie and relay URL is
built from. It must be exactly the origin browsers show: scheme, host, and the port
only when it is not 443. Changing it later invalidates provider redirect URIs and
invite links.

Behind a cloud load balancer, pass TLS through to Caddy, or have the balancer verify
Caddy's certificate, and list its source ranges in `GLOB2_EDGE_TRUSTED_PROXIES` so
client addresses survive (rate limits and relay per-address limits use them). The
balancer must allow WebSocket upgrades and idle connections of at least 60 seconds.

### Separate public website and app

The public Astro website, **Globulation 2 Online**, lives in the separate
[Globulation2/glob2-online-website](https://github.com/Globulation2/glob2-online-website)
repository and deploys to Firebase Hosting in project `pharaoh-418820`.
`glob2online.com` serves that static website; `app.glob2online.com` serves this
stack. Website builds and releases do not restart platform services or matches.
The website's Play and Sign in links navigate to `/play/` and `/signin` on the app
origin. Public player-rating snapshots are published independently to GCS by the
website repository's publisher using the existing public leaderboard API.

For a domain cutover, first point `app.glob2online.com` at the backend and add
`GLOB2_ADDITIONAL_DOMAIN=app.glob2online.com` while retaining the existing
`GLOB2_DOMAIN` and `GLOB2_PUBLIC_ORIGIN`. This certifies and serves the extra name;
it does not enable that origin for relay WebSockets. Test TLS and routes before
changing the canonical origin. Once existing games have drained, set
`GLOB2_DOMAIN=app.glob2online.com` and
`GLOB2_PUBLIC_ORIGIN=https://app.glob2online.com`, update provider callback URIs,
and recreate the affected API and relay services in a planned maintenance window.
New `/api/v1/instance` responses must advertise `wss://app.glob2online.com/realtime`,
and relay URLs and browser requests must use the same app origin. Do not combine
old-origin sessions with new-origin relay endpoints. The Compose relay origin
allowlist follows `GLOB2_PUBLIC_ORIGIN` automatically.

Reload Caddy rather than replacing it while connections are active; its existing
`stream_close_delay` permits a short reconnection grace period but does not promise
indefinite connection retention. A Compose container replacement closes sockets.
Only after app login and a real multiplayer match pass should the apex DNS move to
Firebase and the temporary backend hostname be removed. Website rollout or
rollback alone must not recreate any backend container. `/play/*` supplies COOP
`same-origin` and COEP `require-corp` for WebAssembly browser isolation; keep its
workers and assets on the app origin. These isolation headers are deliberately
limited to the game route so the app's sign-in flows retain normal opener behavior.

### Routes

| Path | Goes to |
| --- | --- |
| `/api/*`, `/realtime`, `/signin`, `/signin/*`, `/auth/*`, `/.well-known/*`, `/j/*` (invite pages) | `platform-api`, round robin over healthy replicas |
| `/relay/<relay id>` | that relay's WebSocket (path rewritten to `/relay`) |
| `/play/*` | the WebAssembly client, `GLOB2_WEB_CLIENT_DIR` |
| everything else | the web app (single-page app with `index.html` fallback) |
| `/internal/*`, `/healthz`, `/readyz`, `/metrics` | `404` at the edge |

`/internal` is the relays' API (registration, heartbeats, match setup, record
upload, match end; see [rooms and matches](../multiplayer/rooms-and-matches.md#internal-api-for-relays)).
It is served by `platform-api` on the backend network only; relays reach it at
`http://platform-api:8080` with their bearer key, and fetch the JWKS from there
too. Caddy never forwards it.

Invite links `https://<origin>/j/<code>` are small pages rendered by the API; their
"Play in browser" button opens `web.browserClientUrl` from `instance.yaml`, which
defaults to `<origin>/play/`.

### Mobile app links

Android App Links and iOS universal links let an invite link open the installed
app directly. Phones check two files on the domain, which `platform-api` serves
from `appLinks` in `instance.yaml`:

- `/.well-known/assetlinks.json` from `appLinks.android`: `packageName` (default
  `org.globulation2.glob2`) and `sha256CertFingerprints`, the SHA-256 of the
  release signing certificate as `AA:BB:…` (32 bytes). With Play App Signing,
  copy it from Play Console > Test and release > App integrity > App signing key
  certificate; otherwise `keytool -list -v -keystore <release keystore>`.
- `/.well-known/apple-app-site-association` from `appLinks.ios.appIds`:
  `<Team ID>.org.globulation2.glob2`, with the Team ID from the Apple Developer
  account's Membership page.

Both files cover `/j/*` only. The apps declare only the official domain, so a
self-hosted instance leaves `appLinks` out (both files then answer 404) and its
invites open the game through `glob2://`. Apple caches the file through its CDN;
after a change, allow a day or reinstall the app.

## Configuration

Everything is configured in `deploy/.env` (Compose variables, and the environment of
`platform-api`) and `deploy/instance.yaml`. `.env.example` lists every variable with
its default.

| Variable | Default | Meaning |
| --- | --- | --- |
| `GLOB2_DOMAIN` | `localhost` | Site name Caddy serves and certifies |
| `GLOB2_ADDITIONAL_DOMAIN` | unset | Optional second served hostname during cutover; does not change the canonical origin |
| `GLOB2_PUBLIC_ORIGIN` | `https://localhost:8443` | Origin clients use; also the relays' URL base |
| `GLOB2_BIND`, `GLOB2_HTTP_PORT`, `GLOB2_HTTPS_PORT` | `127.0.0.1`, `8080`, `8443` | Published address and ports |
| `GLOB2_TLS_MODE` | `auto` | `auto` or `internal` |
| `GLOB2_EDGE_TRUSTED_PROXIES` | `127.0.0.1/32` | Load balancer ranges Caddy trusts for client addresses |
| `POSTGRES_PASSWORD` | required | Database password (letters and digits) |
| `GLOB2_API_REPLICAS`, `GLOB2_WORKER_REPLICAS`, `GLOB2_ENGINE_AGENT_REPLICAS`, `GLOB2_RELAY_REPLICAS` | `2`, `1`, `1`, `1` | Replica counts |
| `ENGINE_CONCURRENCY`, `GLOB2_ENGINE_SCRATCH_SIZE` | `1`, `1g` | Jobs per agent, and its scratch tmpfs |
| `WARM_MAPS_PER_ENTRY` | `1` | Pre-generated quick-match maps per map pool entry (0: off) |
| `GLOB2_RELAY_REGION` | `default` | Region these relays report |
| `GLOB2_RELAY_MAX_MATCHES` | `200` | Matches per relay |
| `GLOB2_RELAY_DRAIN_SECONDS`, `GLOB2_RELAY_STOP_GRACE` | `1800`, `31m` | Longest relay drain, and Compose's stop timeout (keep it longer) |
| `UPLOAD_MAX_BYTES`, `RECORD_MAX_BYTES` | 16 MiB, 64 MiB | Largest map/save upload, and largest match record |
| `RELAY_KEYS` | unset | Extra relay keys, `<relayId>:<key>` comma-separated (relays on other hosts, rotation) |
| `JWT_ACTIVE_KID` | unset | Signing key id, needed while several keys exist ([rotation](#signing-keys-and-rotation)) |
| `LOG_LEVEL` | `info` | Platform log level |
| `GLOB2_WEB_CLIENT_DIR` | `./web-client` | Built WebAssembly client for `/play/` |
| `GLOB2_INSTANCE_CONFIG` | `./instance.yaml` | Instance settings file |
| `GLOB2_*_IMAGE` | local `:development` tags | Images to run ([Images](#images)) |
| `GLOB2_SIM_VERSION` | unset | Label for locally built engine-agent images; the build fails if it does not match the source |
| `GLOB2_BACKEND_SUBNET`, `GLOB2_PROXY_ADDRESS` | `172.30.89.0/24`, `.10` | Backend network; change together if the range is taken |
| provider secrets | | Named in `instance.yaml`, e.g. `GOOGLE_CLIENT_SECRET` |

`instance.yaml` holds the settings players see: name, guests, sign-in providers,
local accounts, rate limits, the access policy and quick-match queues. Its format
is documented in [`platform/instance.example.yaml`](../../platform/instance.example.yaml)
and [ratings and matchmaking](../multiplayer/ratings-and-matchmaking.md). Restart
`platform-api` and `platform-worker` after editing it:
`docker compose up -d --force-recreate platform-api platform-worker`.

## Sign-in providers

Every provider's redirect URI is `<GLOB2_PUBLIC_ORIGIN>/auth/<id>/callback`, where
`<id>` is the provider's `id` in `instance.yaml`. Register exactly that URI. Client
secrets go in `.env` under the name the provider's `clientSecretEnv` gives.
Details of each flow are in [identity and sign-in](../multiplayer/identity.md).

**Google.** In the Google Cloud console, under *APIs & Services → Credentials*,
create an *OAuth client ID* of type *Web application*. Add the redirect URI
`https://play.example.org/auth/google/callback`. Configure the consent screen with
the `openid`, `email` and `profile` scopes. Then:

```yaml
auth:
  providers:
    - {id: google, kind: oidc, preset: google, displayName: Google,
       clientId: 1234-abc.apps.googleusercontent.com, clientSecretEnv: GOOGLE_CLIENT_SECRET}
```

**Microsoft.** In the Microsoft Entra admin center, *App registrations → New
registration*. Choose the account types (personal and work accounts: tenant
`common`; personal only: `consumers`; one organization: its tenant id). Add a *Web*
redirect URI `https://play.example.org/auth/microsoft/callback`, then create a
client secret under *Certificates & secrets*:

```yaml
    - {id: microsoft, kind: oidc, preset: microsoft, tenant: common, displayName: Microsoft,
       clientId: 00000000-0000-0000-0000-000000000000, clientSecretEnv: MICROSOFT_CLIENT_SECRET}
```

**Apple.** In the Apple Developer portal: an App ID with *Sign in with Apple*, a
*Services ID* (its identifier is the `clientId`) configured with the domain
`play.example.org` and return URL `https://play.example.org/auth/apple/callback`,
and a *Sign in with Apple* key (download the `.p8` file, note its key id). Put the
key into `.env` with literal `\n` between lines:

```yaml
    - {id: apple, kind: apple, displayName: Apple, clientId: org.example.glob2.signin,
       teamId: ABCDE12345, keyId: KEY1234567, privateKeyEnv: APPLE_SIGNIN_KEY}
```

```dotenv
APPLE_SIGNIN_KEY=-----BEGIN PRIVATE KEY-----\nMIGT...\n-----END PRIVATE KEY-----
```

Apple returns to the callback with a cross-site `form_post`, so the instance must be
served over HTTPS.

**Any OpenID Connect provider** (Keycloak, Authentik, Forgejo, GitLab, Okta…):
create a confidential client with the authorization code flow, the redirect URI
above and the `openid profile email` scopes, then

```yaml
    - {id: forgejo, kind: oidc, issuer: https://code.example.org, displayName: Example Code,
       clientId: glob2, clientSecretEnv: FORGEJO_CLIENT_SECRET}
```

The issuer must serve `/.well-known/openid-configuration`; `platform-api` reaches it
through the `egress` network.

**Without single sign-on**, enable local accounts (argon2id passwords):
`auth.local.enabled: true` and, to let players register themselves,
`allowRegistration: true`.

## Signing keys and rotation

Access tokens and match tickets are EdDSA (Ed25519) JWTs. `platform-api` signs with
one key and publishes every key in `/.well-known/jwks.json`; relays verify tickets
with that JWKS and refetch it when they meet an unknown key id. The keys are
`<kid>.pem` files in the `signing-keys` volume; `init` creates the first one
(`k<yyyymmdd>`) when the volume holds none, and never replaces a key.

To rotate (for example yearly, or at once if a key may have leaked):

```sh
# 1. Keep the current key signing while the new one is published.
docker compose exec platform-api ls /var/lib/glob2/keys          # e.g. k20261001.pem
echo JWT_ACTIVE_KID=k20261001 >> .env
docker compose run --rm --no-deps init platform keys generate --dir /var/lib/glob2/keys --kid k20270101
docker compose up -d platform-api
# 2. After the JWKS cache time (5 minutes), sign with the new key.
sed -i 's/^JWT_ACTIVE_KID=.*/JWT_ACTIVE_KID=k20270101/' .env
docker compose up -d platform-api
# 3. Once tokens signed by the old key have expired (access tokens: 10 minutes;
#    tickets: their match's expiry), remove the old key.
docker compose run --rm --no-deps init rm /var/lib/glob2/keys/k20261001.pem
docker compose up -d platform-api
```

After a leak, skip the waiting periods: tokens signed by the removed key stop
verifying at once, which signs every player out and refuses tickets for matches not
yet joined. Refresh tokens are opaque database rows, not JWTs, and are unaffected
by key rotation; revoke them with the admin tools if accounts may be compromised.

The relay key (`relay-secret` volume, `relay.key`, read by the API as
`RELAY_KEYS_FILE` and by relays as `GLOB2_RELAY_KEY_FILE`) authenticates relays on
`/internal`. The API accepts every key in that file and in `RELAY_KEYS`, so rotate
without interruption:

```sh
# 1. Keep accepting the current key while relays switch.
echo "RELAY_KEYS=$(docker compose run --rm --no-deps -T init cat /var/lib/glob2/relay/relay.key)" >> .env
docker compose run --rm --no-deps init rm /var/lib/glob2/relay/relay.key
docker compose up -d --force-recreate init platform-api   # init writes a new relay.key
# 2. Replace the relays (drain them as in "Draining relays"), then drop the old key.
sed -i '/^RELAY_KEYS=/d' .env && docker compose up -d platform-api
```

## Scaling

Change the replica counts in `.env` and run `docker compose up -d`.

- **platform-api** is stateless; Caddy re-resolves the replicas every few seconds.
  Realtime sockets on a removed replica reconnect to another.
- **platform-worker** can run several replicas: every replica applies job results,
  one at a time holds the scheduler (matchmaker, ratings, warm maps).
- **engine-agent** replicas share the job queue; each runs `ENGINE_CONCURRENCY` jobs.
  Each engine process may use up to 4-8 GB of address space
  (`ENGINE_MEMORY_MB`), so size concurrency by memory.
- **relay**: each replica registers its own URL, `wss://<host>/relay/<container id>`,
  derived from `GLOB2_PUBLIC_ORIGIN` and its container's host name. Adding replicas
  is immediate. Removing one ends its matches unless it is drained first; use the
  [relay drain procedure](#draining-relays) rather than lowering the count.

Relays on other hosts (to be closer to players) are not part of this file. Such a
relay needs its own TLS or proxy and public URL (`GLOB2_RELAY_PUBLIC_URL`), the relay
key, and a private route to `platform-api`'s `/internal` (a VPN or private network:
`/internal` is never served publicly). Settings are in [relay](../multiplayer/relay.md).

## Backups and restore

Back up the database and the blob volume together, plus the keys:

```sh
cd deploy
docker compose exec -T postgres pg_dump -U glob2 -Fc glob2 > glob2-$(date +%F).dump
docker compose run --rm --no-deps -T platform-worker tar czf - -C /var/lib/glob2 blobs > blobs-$(date +%F).tar.gz
docker compose run --rm --no-deps -T init tar czf - -C /var/lib/glob2 keys relay > keys-$(date +%F).tar.gz
```

`pg_dump` is consistent while the stack runs. Blobs are content-addressed and never
rewritten, so a blob archive taken right after the dump holds everything the dump
refers to. Store `keys-*.tar.gz` encrypted: it can sign tokens for any account.

To restore into a fresh stack (same `.env` and `instance.yaml`):

```sh
docker compose up -d --wait postgres
docker compose run --rm --no-deps -T init sh -c 'rm -f /var/lib/glob2/keys/*.pem /var/lib/glob2/relay/relay.key; tar xzf - -C /var/lib/glob2' < keys-2026-10-01.tar.gz
docker compose run --rm --no-deps -T platform-worker tar xzf - -C /var/lib/glob2 < blobs-2026-10-01.tar.gz
docker compose exec -T postgres pg_restore -U glob2 -d glob2 --clean --if-exists --no-owner < glob2-2026-10-01.dump
docker compose up -d --wait
```

`init` then applies any migrations newer than the dump. Check the restore on a
separate host or project (`docker compose -p glob2-restore …` with other ports)
before relying on it.

## Upgrades

Before every upgrade, take a [backup](#backups-and-restore) and read the release
notes for migrations and sim-version changes.

```sh
git pull                       # or set new GLOB2_*_IMAGE digests in .env
docker compose build           # skip with prebuilt images: docker compose pull
docker compose up -d --wait
```

On a single host, `deploy/update-host.sh <env-file> [git-ref]` does all of this in
one command: it checks out the revision (when given), builds the WebAssembly client
into `GLOB2_WEB_CLIENT_DIR` with `deploy/build-web-client.sh` (Emscripten runs in a
container, so the host needs only Docker), builds the images with the checkout's
sim version, and starts the stack, waiting until every service is healthy.

`up` runs `init` first, which applies new migrations (forward only, each in a
transaction) before the new API and worker start. `platform-api` and
`platform-worker` replicas are replaced together: realtime clients reconnect after
a few seconds. Caddy keeps WebSockets open across its own configuration reloads.

### Draining relays

A plain `up -d` with a new relay image replaces the relays at once and drains each
for up to `GLOB2_RELAY_DRAIN_SECONDS` while new matches have no relay. Replace them
without downtime instead:

```sh
old=$(docker compose ps -q relay)
docker compose up -d --no-recreate --scale relay=$((2 * ${GLOB2_RELAY_REPLICAS:-1})) relay   # new relays start, register
docker stop --timeout 1860 $old   # each old relay drains: no new matches, running matches finish
docker rm $old
docker compose up -d relay     # back to GLOB2_RELAY_REPLICAS, all on the new image
```

While draining, a relay reports `draining` in its heartbeat so the platform stops
allocating matches to it, and still accepts reconnects to its running matches.
Records not yet uploaded stay in `relay-spool` under the relay's id; to re-send
them later, start a relay with `GLOB2_RELAY_ID=<that id>`.

### Sim versions and engine agents

A sim version is `VERSION_MINOR`, `NET_PROTOCOL_VERSION` and a hash of the data files
that affect the simulation (`python3 deploy/sim_version.py` prints it for a source
tree). Players are only matched with the same version, and only an engine agent of
that version can generate maps for or verify their games.

An upgrade that changes the sim version therefore needs care:

- New clients get matches only once an agent of their version runs. Deploy the new
  `engine-agent` image before or with the client release.
- Clients still on the old version get `update_required` unless an agent of the old
  version keeps running, and finished matches of the old version are verified only
  by an old-version agent. Keep one running until those matches are verified and
  old clients have updated.

To serve an additional (older) version, add a second agent service in
`deploy/compose.override.yaml`, which Compose merges automatically:

```yaml
services:
  engine-agent-125-48:
    extends: {file: compose.yaml, service: engine-agent}
    image: ghcr.io/<owner>/<repository>-engine-agent:simver-125-48-<data hash>
    build: !reset null
    deploy: {replicas: 1}
```

Released engine-agent images are tagged `simver-<sim version>`. To build one for an
older release yourself, build the `engine-agent` target of the current Dockerfile
(the current agent wrapper) with that release's source tree as the engine source:

```sh
git worktree add ../glob2-0.9.25 v0.9.25
docker buildx build -f deploy/Dockerfile --target engine-agent \
  --build-context engine-source=../glob2-0.9.25 \
  --build-arg GLOB2_SIM_VERSION=$(python3 deploy/sim_version.py ../glob2-0.9.25) \
  -t glob2-engine-agent:simver-$(python3 deploy/sim_version.py ../glob2-0.9.25) --load .
```

The verifier must compute byte-identical games to the release's clients, so build
it with the compiler and flags of that release where they differ, and check it
against a recorded game ([replay verification](../development/headless-replays.md))
before trusting its verdicts.

Each image knows its own version (from its binary and data), registers it, and
takes only that version's jobs; `GET /api/v1/instance` lists every version with a
live agent. Remove the service when the version is retired.

### Images

The Dockerfile targets are `platform` (API, worker and CLI), `engine-agent`,
`relay` and `caddy` (Caddy with the built web app). A `server-v*` tag runs
`.github/workflows/server-image.yml`, which publishes all of them (with the legacy
`server` and `proxy`) for linux/amd64 and linux/arm64 to
`ghcr.io/<owner>/<repository>-<target>`, tagged with the Git tag and commit; engine
agents are also tagged `simver-<sim version>` and labelled
`org.glob2.sim-version`. Pin digests in production:

```dotenv
GLOB2_PLATFORM_IMAGE=ghcr.io/<owner>/<repository>-platform@sha256:…
GLOB2_ENGINE_AGENT_IMAGE=ghcr.io/<owner>/<repository>-engine-agent@sha256:…
GLOB2_RELAY_IMAGE=ghcr.io/<owner>/<repository>-relay@sha256:…
GLOB2_CADDY_IMAGE=ghcr.io/<owner>/<repository>-caddy@sha256:…
```

## Operations

- Logs: `docker compose logs -f platform-api relay`; Compose keeps up to 100 MB per
  container (local driver, five 20 MB files).
- Relay metrics are Prometheus text at `http://<relay>:7495/metrics` on the backend
  network (not public).
- Admin CLI: `docker compose run --rm --no-deps platform-api platform admin grant|revoke …`.
- Migrations: `docker compose run --rm --no-deps init node packages/db/src/cli.ts status`.
- Stopping: `docker compose stop` drains relays (up to `GLOB2_RELAY_STOP_GRACE`);
  `docker compose down` keeps volumes; `down --volumes` deletes all data.

## Legacy YOG until the cutover

The YOG lobby and router keep their own stack and documentation in
[deploy/README.md](../../deploy/README.md):

```sh
docker compose -f deploy/compose.legacy.yaml up -d --wait
```

Its Compose project is still `glob2`, so existing volumes and secrets carry over
unchanged. Both stacks publish ports through their own Caddy, so on one host give
them different ports or (better) different hosts or IP addresses. Released clients
keep using YOG until the clients that speak to the new platform ship; at the M9
cutover the legacy stack, `compose.legacy.yaml`, `Caddyfile.legacy`,
`provision_tls.py` and `healthcheck.py` are deleted.

## Testing a deployment

```sh
python3 tests/deployment/platform_stack_smoke.py --log-dir artifacts/platform-stack
```

The smoke test builds the images, starts an isolated project with its own ports,
volumes and `.env`, and checks: every service healthy and `init` successful; TLS
from the local CA, HTTP redirect, the web app, invite pages at `/j/` and `/play/`, and that
`/internal`, `/healthz`, `/readyz` and `/metrics` are not public; the instance
listing the agent's sim version; guest sign-in and `session.hello` over
`/realtime` (and a foreign `Origin` refused); the JWKS against the key files and the
token's `kid`; each relay registered with the platform under its public URL,
reachable at `/relay/<id>`, and unknown ids refused; and a `generate-map` job run by the engine agent
with the real binary, applied by the worker and stored as a blob. It then removes
the project and its volumes. `--no-build --tag <tag>` reuses built images; `--keep`
leaves the stack running. CI runs it when `deploy/`, `tests/deployment/`,
`src/relay/`, migrations or platform dependencies change, and on full runs.

On a running deployment, the same checks run against the public origin with a
publicly trusted certificate, the deployed web client and the replica counts in
the env file (run on the host; nothing is started or removed):

```sh
python3 tests/deployment/platform_stack_smoke.py --attach glob2-platform \
    --env-file /path/to/deployment.env --log-dir artifacts/live-smoke
```

`tests/deployment/live_match_e2e.py` then plays a real match on the instance: two
guests create and join a room by invite code, start it with AI seats on a generated
map, and two headless native clients (`glob2 --turn-client`, built from the same
sim version) play it through the relay until a sudden-death rule ends it. It checks
that both clients' per-tick checksums agree, and, with `--psql`, that the relay
reported the match, uploaded its record and the verify-match job judged it
`verified`:

```sh
python3 tests/deployment/live_match_e2e.py --origin https://play.example.org \
    --glob2 build/linux/client/release/src/glob2 --out artifacts/live-e2e \
    --psql "docker compose -p glob2-platform exec -T postgres psql -U glob2 -d glob2 -At"
```

## Example: one virtual machine on Google Cloud

A small instance fits on one Compute Engine VM. Building the images and the
WebAssembly client on the VM itself avoids a registry. The commands below use
placeholders (`glob2-host`, `REGION`, `ZONE`, `play.example.org`); keep secrets on
the VM only.

1. **Machine.** `e2-standard-4` (4 vCPUs, 16 GB) with a 100 GB balanced disk and
   Debian 12 is enough for the stack, one engine agent and image builds:

   ```sh
   gcloud compute addresses create glob2-host-ip --region REGION
   gcloud compute firewall-rules create glob2-host-web --network default \
       --allow tcp:80,tcp:443,udp:443 --target-tags glob2-host
   gcloud compute instances create glob2-host --zone ZONE --machine-type e2-standard-4 \
       --image-family debian-12 --image-project debian-cloud \
       --boot-disk-size 100GB --boot-disk-type pd-balanced \
       --address <reserved IP> --tags glob2-host --labels app=glob2
   ```

2. **DNS.** An `A` record for the domain (and `www` if wanted) pointing at the
   reserved address, with a short TTL while setting up. Caddy obtains the
   certificate from Let's Encrypt on first start, so the record must resolve before
   that.
3. **Docker.** Install Docker Engine and the Compose plugin from Docker's Debian
   repository, add your user to the `docker` group, and clone the repository.
4. **Configuration.** Keep the env file and `instance.yaml` outside the checkout,
   e.g. in a `0700` directory. Beyond [Setup from zero](#setup-from-zero), set
   `GLOB2_BIND=0.0.0.0`, `GLOB2_HTTP_PORT=80`, `GLOB2_HTTPS_PORT=443`,
   `GLOB2_DOMAIN`, `GLOB2_PUBLIC_ORIGIN` (and `GLOB2_REDIRECT_DOMAINS=www.<domain>`
   to redirect the `www` name), and point `GLOB2_INSTANCE_CONFIG`,
   `GLOB2_ENV_FILE` and `GLOB2_WEB_CLIENT_DIR` at absolute paths. With no sign-in
   providers yet, enable guests and `auth.local` in `instance.yaml`; a provider is
   added later by registering it ([Sign-in providers](#sign-in-providers)), adding
   it to `instance.yaml` and its secret to the env file, and redeploying.
5. **Deploy and redeploy.** `deploy/update-host.sh /path/to/deployment.env
   origin/<branch>` builds and starts everything; run it again for each new
   revision. The first build takes about half an hour on four vCPUs; later builds
   reuse the BuildKit caches.
6. **Check.** Run the attached smoke test and the live match above.

Approximate cost (2026 on-demand list prices, a Canadian region): the VM about
US$110 a month, the disk about US$10, the static address in use about US$3, a
Cloud DNS zone about US$0.20, plus egress. Stopping the VM stops the machine
charge; the disk and a reserved but unattached address still cost. To remove
everything: delete the VM, the address, the firewall rule and the DNS records.

## Limits

- One host. Postgres, the blob volume and Caddy are single instances; S3-compatible
  blob storage is not implemented yet (`BLOB_STORE=s3`).
- Relays on other hosts need manual setup (above).
- Until the engine reports its own data hash (`glob2 --sim-version`), the
  engine-agent image passes the hash computed by `deploy/sim_version.py` at build
  time; the agent refuses to start if the two ever disagree.
- The relay key is one shared secret for all relays.
