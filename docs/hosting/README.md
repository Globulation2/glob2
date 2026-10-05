# Self-hosting a Globulation 2 online instance

One Docker Compose stack, `deploy/compose.yaml`, runs every online feature: accounts
and sign-in, rooms and invite links, match relays, quick match, verification,
ratings and history. This guide covers setting it up from nothing, operating it, and
upgrading it. The design behind the services is in the
[platform architecture](../multiplayer/architecture.md).

This stack replaced the YOG lobby and router at the M9 cutover, with no data
import (see [the former YOG lobby](#the-former-yog-lobby)).

## The stack

```
             internet ── 80/443 ──► caddy ─┬─ /api /realtime /signin /auth /.well-known /j ─► platform-api ×N ─┐
                                           ├─ /relay/<relay id> (WebSocket) ──────────────► relay ×N ──────────┤ /internal (backend only)
                                           ├─ /play/  WebAssembly client (static)                              │
                                           └─ /       web app (static)                                         ▼
  platform-worker ×1..N ──► postgres 16 ◄── platform-api ◄── /internal/v1/engine ── engine-agent ×N (one image per sim version)
         └──────────────── blob volume ────────────┘          (no database, no blob volume)
```

| Service | Image (Dockerfile target) | Replicas | Role |
| --- | --- | --- | --- |
| `caddy` | `caddy` | 1 | TLS (ACME or local CA), static web app and web client, routing. The only service with published ports. |
| `postgres` | `postgres:16-alpine` | 1 | All state, the job queue, pub/sub and the matchmaker's leader lock. |
| `init` | `platform` | one-shot | Creates the first signing key, the relay and engine-agent keys and the database role passwords, creates or updates the [database roles](#database-roles) as the Postgres superuser, then applies migrations as `glob2_migrator`. Runs before the platform starts on every `up`; the only process that uses the superuser. |
| `platform-api` | `platform` | `GLOB2_API_REPLICAS` (2) | REST, realtime WebSocket, sign-in pages, JWKS, and `/internal` for relays. Stateless. |
| `music-worker` | `music-worker` | 1 | Community music inspection, conversion and final media storage. |
| `platform-worker` | `platform` | `GLOB2_WORKER_REPLICAS` (1) | Engine-job results, ratings, matchmaker and schedules (the scheduler runs on one replica at a time). |
| `engine-agent` | `engine-agent` | `GLOB2_ENGINE_AGENT_REPLICAS` (1) | Map generation, validation, previews and match verification with the headless `glob2` binary of one sim version. It runs the engine on uploaded files, so it has no database access and no blob volume: it leases jobs and moves blobs through `platform-api`'s `/internal/v1/engine` with a bearer agent key. |
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
| `blobs` | Maps, saves, previews, match records, replays and community music (content-addressed) | Yes |
| `signing-keys` | Ed25519 private keys (`<kid>.pem`) for access tokens and match tickets | Yes, encrypted |
| `relay-secret` | `relay.key`, the bearer key relays use on `/internal` | Yes, encrypted (or regenerate) |
| `engine-agent-secret` | `agent.key`, the bearer key engine agents use on `/internal/v1/engine` | Optional (regenerate: delete it and `up`) |
| `db-migrator-secret`, `db-api-secret`, `db-worker-secret` | `password` of `glob2_migrator`, `glob2_api` and `glob2_worker`, each mounted only by the service that uses it | Optional (regenerate: delete it and `up`) |
| `relay-spool` | Match records a relay has not uploaded yet, one directory per relay id (`relay-1`, `relay-2`, …), and the relays' slot locks | Optional |
| `caddy-data`, `caddy-config` | Certificates, ACME account, local CA | Optional (Caddy re-issues) |

### Database roles

No long-running service connects as a Postgres superuser. `POSTGRES_PASSWORD` is the
superuser's password (the `postgres` image's `glob2`); only `postgres` itself and
`init` see it, and it is blanked in the other services' environment.

| Role | Used by | Privileges |
| --- | --- | --- |
| `glob2` (superuser) | `init` only | Creates the roles below, sets their passwords, hands a database created before roles existed to `glob2_migrator`. |
| `glob2_migrator` | `init` | Owns every table, view, function and type in `public` and `graphile_worker`; runs the platform's and graphile-worker's migrations, then re-applies the grants. Not a superuser. |
| `glob2_api` | `platform-api`, the `platform` CLI | `SELECT`/`INSERT`/`UPDATE`/`DELETE` on the platform tables and the job queue; no DDL, no `TRUNCATE`. The admin audit log is append-only (`scrub_audit_log_account()`, owned by the migrator, is the one change allowed: account deletion). |
| `glob2_worker` | `platform-worker` | As `glob2_api`, minus `identities`, `device_credentials` and `admin_audit_log`; on `refresh_tokens`, `web_sessions` and `auth_flows` only the retention deletes. |
| none | `engine-agent` | Uses `platform-api`'s internal engine API instead. |

`init` writes a random password per role into its own volume (`db-<role>-secret`);
each service mounts only its own and reads it through `DATABASE_PASSWORD_FILE`. To
rotate a role's password, delete its `password` file
(`docker compose run --rm --no-deps init rm /var/lib/glob2/db/api/password`) and run
`docker compose up -d`: `init` writes a new one and updates the role. `glob2-migrate
roles` and `latest` (`packages/db/src/cli.ts`) do the same by hand.

The engine agent's key (`engine-agent-secret`, `agent.key`) lets it announce
itself, lease jobs of its sim version, read only the blobs a job it holds names,
store results and report them. A key written `<agentId>:<key>` in
`ENGINE_AGENT_KEYS` acts only as that agent.

All services except Postgres run as UID/GID 10001 with a read-only root
filesystem, all capabilities dropped and `no-new-privileges`; Postgres runs as its
own UID 70 under the same restrictions. Health checks: `platform-api` `/readyz`
(database reachable and the realtime LISTEN connection up), `relay` `/readyz`
(ticket keys loaded, not draining), Caddy `/livez`, Postgres `pg_isready`.

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
   `deploy/build-web-client.sh <served-dir>` builds it with the pinned Emscripten
   SDK in a container (`scons target=web release=1 web-package`, see
   [browser/README.md](../../browser/README.md)), writes the Brotli and gzip copies
   (`browser/precompress.py`) and installs the result into `<served-dir>`; set
   `GLOB2_WEB_CLIENT_DIR` in `.env` to that directory. Caddy serves it with the
   cross-origin isolation headers the threaded client needs. Without it `/play/`
   answers 404 and everything else works.
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

`test/deployment/platform_stack_smoke.py` performs these steps in an isolated
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

The simple default is one host: the stack serves its own web app at `/`, and
`GLOB2_PUBLIC_ORIGIN` is the only address players need. Keep it that way unless
you also run a separate public website.

To put a website at your apex domain, run this stack on a subdomain, say
`app.example.org`, and treat that subdomain as the instance: `GLOB2_DOMAIN`,
`GLOB2_PUBLIC_ORIGIN`, provider redirect URIs, invite links (`/j/<code>`), relay
URLs (`wss://app.example.org/relay/<id>`), the token issuer, the JWKS, the
`/.well-known` app-link files and the game's instance setting all use it. The
website stays static and links into the app:

| Website link | Goes to |
| --- | --- |
| Play in browser | `https://app.example.org/play/` |
| Sign in, online hub | `https://app.example.org/signin`, `https://app.example.org/` |
| Leaderboards, matches, players, maps | the app's pages (`/leaderboard`, `/matches/<id>`, `/players/<id>`, `/maps`) |

The app links back with `GLOB2_WEBSITE_URL` and `GLOB2_DOWNLOAD_URL` (build-time,
so rebuild the `caddy` image). With a website URL, a strip above the app's header
links to the website's home, game, learn, news and downloads pages, and Download
defaults to `<website>/downloads/`; without one, Download goes to the project's
release page.

The website needs no CORS or origin allow-list entry: it calls no app API from
browsers. If it shows live numbers, fetch them server-side or at build time from
the public read APIs (`/api/v1/leaderboards/<queue>`, `/api/v1/stats`). Do not
add the website to `web.allowedOrigins` in `instance.yaml`: that list grants
cookie-authenticated requests and `/realtime` from browsers.

When the apex used to be the instance, have the website redirect the old app
paths there with `301` (path and query kept): `/j/*`, `/play/*`, `/signin`,
`/auth/*`, `/api/*`, `/matches*`, `/players/*`, `/leaderboard*`, `/maps*` and
`/admin*`. Clients built for the old origin keep working: a former official
origin listed in `scons/official_instance.py` maps to the new one for stored
selections and invite links, while saved credentials stay with the origin that
issued them. `platform_stack_smoke.py --attach ... --website https://example.org`
checks the redirects.

#### The official instance

`glob2online.com` is the public website, **Globulation 2 Online**: an Astro site
in the separate
[Globulation2/glob2-online-website](https://github.com/Globulation2/glob2-online-website)
repository on Firebase Hosting (project `pharaoh-418820`), with the redirects
above in its `firebase.json`.
`app.glob2online.com` runs this stack and is the official instance origin
(`scons/official_instance.py`; its former origin is `https://glob2online.com`).
Website releases never restart platform services or matches. Public rating
snapshots are published to a bucket by the website repository's scheduled
publisher from `/api/v1/leaderboards/<queue>`.

#### Moving an instance to a new hostname

1. Point the new name at the host and add `GLOB2_ADDITIONAL_DOMAIN=<new name>`,
   keeping `GLOB2_DOMAIN` and `GLOB2_PUBLIC_ORIGIN`. Caddy certifies and serves
   both names, but relays and the realtime origin check still use the old origin.
   Check TLS and routes on the new name.
2. When no matches are running, set `GLOB2_DOMAIN` and `GLOB2_PUBLIC_ORIGIN` to the
   new name, update provider redirect URIs, and recreate `platform-api` and the
   relays. `/api/v1/instance` then advertises `wss://<new name>/realtime`, and the
   relay origin allow-list follows `GLOB2_PUBLIC_ORIGIN`. Do not mix old-origin
   sessions with new-origin relay URLs.
3. After sign-in and a real match pass on the new name, move the old name's DNS
   (for example to the website) and clear `GLOB2_ADDITIONAL_DOMAIN`, so Caddy
   stops renewing a certificate for a name it no longer serves.

Reload Caddy rather than replacing it while connections are active; its
`stream_close_delay` gives a short reconnection grace period, but replacing the
container closes sockets. `/play/*` sends COOP `same-origin` and COEP
`require-corp` for WebAssembly isolation; keep the game's workers and assets on
the app origin. The headers are limited to `/play/*` so sign-in pages keep normal
opener behaviour.

### Routes

| Path | Goes to |
| --- | --- |
| `/api/*`, `/realtime`, `/signin`, `/signin/*`, `/auth/*`, `/.well-known/*`, `/j/*` (invite pages) | `platform-api`, round robin over healthy replicas |
| `/relay/<relay id>` | that relay's WebSocket (path rewritten to `/relay`) |
| `/play/*` | the WebAssembly client, `GLOB2_WEB_CLIENT_DIR`: precompressed `.br`/`.gz` copies when present; `assets/*.data` (content-addressed) cached as immutable, everything else revalidated |
| everything else | the web app (single-page app with `index.html` fallback) |
| `/internal/*`, `/healthz`, `/readyz`, `/metrics` | `404` at the edge |

`/internal` is the relays' API (registration, heartbeats, match setup, record
upload, match end; see [rooms and matches](../multiplayer/rooms-and-matches.md#internal-api-for-relays))
and the engine agents' (`/internal/v1/engine`: job leases, results, blobs; see
[architecture](../multiplayer/architecture.md#engine-agents)). It is served by
`platform-api` on the backend network only; relays and agents reach it at
`http://platform-api:8080` with their bearer keys, and relays fetch the JWKS from
there too. Caddy never forwards it.

Caddy adds security headers to everything it serves: `Strict-Transport-Security`
(`max-age` from `GLOB2_HSTS_MAX_AGE`, default one year; `0` turns it off),
`X-Content-Type-Options: nosniff`, a `Referrer-Policy` and `X-Frame-Options: DENY`,
and a `Content-Security-Policy` with `frame-ancestors 'none'`:

- the web app: scripts, styles, fonts and data from the instance origin only;
- `/play/`: also inline scripts (the client's loader page), `'wasm-unsafe-eval'`,
  `blob:` workers, and `https:`/`wss:` connections (players choose instances and
  relays); `Cross-Origin-Opener-Policy`/`-Embedder-Policy` stay as before;
- API responses that set no policy of their own (`default-src 'none'`); the
  sign-in and invite pages keep their own stricter one.

The web app's schema library probes for `eval` once and falls back to checking
without it; browsers report that probe as a blocked `script-src` attempt, which is
expected.

Invite links `https://<origin>/j/<code>` are small pages rendered by the API; their
primary "Play in browser" button opens `web.browserClientUrl` from `instance.yaml`,
which defaults to `<origin>/play/`, and "Open in the Globulation 2 app" comes second
(phones on an instance with `appLinks` get the app first; see
[rooms and matches](../multiplayer/rooms-and-matches.md#invite-links)).

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

The apps take the domain from `scons/official_instance.py`: the Android build fills
the App Link host (`officialInstanceHost` in `AndroidManifest.xml`) from it, and
`mobile/ios.py` writes `applinks:<host>` into the iOS entitlements, both currently
`app.glob2online.com`. The Amazon and China editions leave online play out, so they
declare neither invite links nor associated domains.

#### Setting up the official instance

`app.glob2online.com` serves the iOS file already (Team ID `CL2MNNYQX3`, also in
Apple's CDN); `assetlinks.json` answers 404 until the Play app-signing SHA-256 is
added. The values the maintainer supplies:

| Value | Where it comes from |
| --- | --- |
| Google Play app-signing certificate SHA-256 | Play Console > the app > Test and release > App integrity > App signing key certificate, "SHA-256 certificate fingerprint" (uppercase hex with colons, as the config expects). Play re-signs every installed copy with this key, so it is the one phones check; the upload key is not. |
| Other Android signing keys (optional) | F-Droid signs its own APKs. Add F-Droid's certificate SHA-256 (`apksigner verify --print-certs <F-Droid APK>`) for F-Droid installs to get verified links; without it they open the invite page, whose "Open in the Globulation 2 app" button still works. Up to eight fingerprints are allowed. |
| Apple Team ID | The TestFlight workflow signs with team `CL2MNNYQX3`; confirm it on the Apple Developer account's Membership page. |
| Associated Domains capability | Enable it on the `org.globulation2.glob2` App ID (Certificates, Identifiers & Profiles), so the App Store provisioning profile carries `com.apple.developer.associated-domains`. Check an exported build with `codesign -d --entitlements - Glob2.app`: it must list `applinks:app.glob2online.com`. |

The release mirror's **App signing fingerprints** workflow reads these values with
the release credentials, enables Associated Domains on the App ID when it is
missing, and ends with a ready-to-paste `appLinks` block
([signing fingerprints for invite links](../mobile/development.md#signing-fingerprints-for-invite-links)).
Use the Play App Signing certificate it reports. The upload key and Amazon
certificates are listed for reference only.

Then add to the deployment's `instance.yaml` and recreate `platform-api`:

```yaml
appLinks:
  android:
    packageName: org.globulation2.glob2
    sha256CertFingerprints:
      - <Play app-signing SHA-256, AA:BB:… (32 bytes)>
  ios:
    appIds: [CL2MNNYQX3.org.globulation2.glob2]
```

Check the result:

```sh
curl -s https://app.glob2online.com/.well-known/assetlinks.json
curl -s https://app.glob2online.com/.well-known/apple-app-site-association
curl -s https://app-site-association.cdn-apple.com/a/v1/app.glob2online.com   # Apple's cached copy
curl -s 'https://digitalassetlinks.googleapis.com/v1/statements:list?source.web.site=https://app.glob2online.com&relation=delegate_permission/common.handle_all_urls'   # Google's view
adb shell pm verify-app-links --re-verify org.globulation2.glob2
adb shell pm get-app-links org.globulation2.glob2   # app.glob2online.com: verified
```

Android verifies when the app is installed or updated, so reinstall (or re-verify as
above) after changing the file. Then open an invite link from another app on each
phone; it should open the game at the room.

## Configuration

Everything is configured in `deploy/.env` (Compose variables, and the environment of
`platform-api`) and `deploy/instance.yaml`. `.env.example` lists every variable with
its default.

| Variable | Default | Meaning |
| --- | --- | --- |
| `GLOB2_DOMAIN` | `localhost` | Site name Caddy serves and certifies |
| `GLOB2_ADDITIONAL_DOMAIN` | unset | Optional second served hostname during cutover; does not change the canonical origin |
| `GLOB2_WEBSITE_URL`, `GLOB2_DOWNLOAD_URL` | unset | Web app links to a separately hosted public website and its download page (build-time) |
| `GLOB2_PUBLIC_ORIGIN` | `https://localhost:8443` | Origin clients use; also the relays' URL base |
| `GLOB2_BIND`, `GLOB2_HTTP_PORT`, `GLOB2_HTTPS_PORT` | `127.0.0.1`, `8080`, `8443` | Published address and ports |
| `GLOB2_TLS_MODE` | `auto` | `auto` or `internal` |
| `GLOB2_EDGE_TRUSTED_PROXIES` | `127.0.0.1/32` | Load balancer ranges Caddy trusts for client addresses |
| `POSTGRES_PASSWORD` | required | Password of the Postgres superuser (letters and digits); only `postgres` and `init` use it, the services have their own [roles](#database-roles) |
| `GLOB2_HSTS_MAX_AGE` | `31536000` | `Strict-Transport-Security` max-age Caddy sends (`0`: off) |
| `GLOB2_API_REPLICAS`, `GLOB2_WORKER_REPLICAS`, `GLOB2_ENGINE_AGENT_REPLICAS`, `GLOB2_RELAY_REPLICAS` | `2`, `1`, `1`, `1` | Replica counts |
| `ENGINE_CONCURRENCY`, `GLOB2_ENGINE_SCRATCH_SIZE` | `1`, `1g` | Jobs per agent, and its scratch tmpfs |
| `WARM_MAPS_PER_ENTRY`, `WARM_MAPS_MAX_PER_ENTRY` | `2`, `8` | Pre-generated quick-match maps per map pool entry (0: off), and the ceiling the pool rises to while an entry is busy |
| `GLOB2_BACKUP_DIR`, `GLOB2_BACKUP_KEEP` | `backups/` beside the env file's directory, `5` | Where `deploy/update-host.sh` keeps its pre-upgrade backups, and how many |
| `GLOB2_BACKUP_BUCKET` | unset | Cloud Storage bucket of the [scheduled backups](#scheduled-backups) |
| `GLOB2_DEPLOYED_REVISION_FILE` | `deployed-revision` beside the env file's directory | Where `deploy/update-host.sh` records the revision of each successful deployment, its rollback target |
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

On a single host, `deploy/configure-signin.py` makes both edits and restarts the
services in one step, taking the secret from standard input so that it stays out
of the command line and the shell history:

```sh
read -rs GOOGLE_SECRET   # paste the client secret, then Enter
printf %s "$GOOGLE_SECRET" | python3 deploy/configure-signin.py /path/to/deployment.env \
    google --client-id 1234-abc.apps.googleusercontent.com --restart
```

It edits only `auth.providers` in the file `GLOB2_INSTANCE_CONFIG` names (other
keys and comments stay), checks the result with PyYAML before writing, and writes
`GOOGLE_CLIENT_SECRET` to the env file. `--remove` takes the provider out again.
With only the `openid`, `email` and `profile` scopes, which Google counts as
non-sensitive, the app needs no scope verification; publish it ("In production")
so that any Google account can sign in, not only listed test users.

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
  Realtime sockets on a removed replica reconnect to another. Room presence is
  shared: each replica registers in `api_replicas`, heartbeats every 10 seconds and
  records its sockets' accounts in `realtime_presence`, so a player counts as
  connected while any replica holds a socket of theirs. A replica that stops
  heartbeating for 45 seconds (crashed, or cut off from the database) is expired by
  the others, and its players are marked disconnected in their rooms.
- **platform-worker** can run several replicas: every replica applies job results,
  one at a time holds the scheduler (matchmaker, ratings, warm maps).
- **engine-agent** replicas lease jobs from the same queue (`engine_jobs`, through
  `platform-api`); each runs `ENGINE_CONCURRENCY` jobs and polls every
  `ENGINE_POLL_MS` (1000) when idle.
  Each engine process may use up to 4-8 GB of address space
  (`ENGINE_MEMORY_MB`), so size concurrency by memory.
- **relay**: each replica registers its own URL, `wss://<host>/relay/<container id>`,
  derived from `GLOB2_PUBLIC_ORIGIN` and its container's host name, under a stable
  relay id: the first free slot `relay-1`, `relay-2`, … it claims (with `flock`) on
  the `relay-spool` volume for as long as it runs (`deploy/relay-entrypoint.sh`).
  A recreated relay therefore gets an id from the same set and re-submits what it
  had spooled, and pinned keys (`relay-1:<key>`) keep matching. At start-up a
  relay also adopts spooled matches of directories no running relay holds (after
  a scale-down, or from before stable ids); the platform accepts repeated uploads.
  Adding replicas is immediate. Removing one ends its matches unless it is drained
  first; use the [relay drain procedure](#draining-relays) rather than lowering the
  count.

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
docker compose run --rm --no-deps -T init tar czf - -C /var/lib/glob2 keys relay engine-agent db > keys-$(date +%F).tar.gz
```

`pg_dump` is consistent while the stack runs. Blobs are content-addressed and never
rewritten, so a blob archive taken right after the dump holds everything the dump
refers to. Store `keys-*.tar.gz` encrypted: it can sign tokens for any account.

To restore into a fresh stack (same `.env` and `instance.yaml`):

```sh
docker compose up -d --wait postgres
docker compose run --rm --no-deps -T init sh -c 'rm -f /var/lib/glob2/keys/*.pem /var/lib/glob2/relay/relay.key /var/lib/glob2/engine-agent/agent.key /var/lib/glob2/db/*/password; tar xzf - -C /var/lib/glob2' < keys-2026-10-01.tar.gz
docker compose run --rm --no-deps -T platform-worker tar xzf - -C /var/lib/glob2 < blobs-2026-10-01.tar.gz
docker compose exec -T postgres pg_restore -U glob2 -d glob2 --clean --if-exists --no-owner < glob2-2026-10-01.dump
docker compose up -d --wait
```

`pg_restore --no-owner` leaves the restored objects owned by the superuser; `init`
hands them to `glob2_migrator` again, re-applies the grants, then applies any
migrations newer than the dump. Check the restore on a
separate host or project (`docker compose -p glob2-restore …` with other ports)
before relying on it.

### Scheduled backups

`deploy/backup-to-gcs.sh <env-file>` backs the database up to a Google Cloud
Storage bucket, and a systemd timer runs it every day at 03:17 UTC (plus up to ten
minutes; a run missed while the host was down happens at the next boot). Each run:

1. takes a `pg_dump -Fc` of the database and checks that `pg_restore` can read it;
2. lists the ids of deleted accounts (`<name>.deleted-accounts.txt`, for restores,
   below);
3. uploads both to `daily/glob2-<UTC time>.*`, and copies them to `weekly/` when the
   newest weekly backup is seven days old or more, and to `monthly/` when the
   calendar month (UTC) has none yet.

The bucket's lifecycle rules ([`deploy/gcs-backup-lifecycle.json`](../../deploy/gcs-backup-lifecycle.json))
do the rotation:

| Tier | Kept |
| --- | --- |
| `daily/` | 7 days (lifecycle deletion can lag by up to a day) |
| `weekly/` | 35 days, so about five weekly backups |
| `monthly/` | indefinitely, one per calendar month |

Object names are never reused, so the host only creates and lists objects; it
cannot delete or overwrite a backup. These backups hold the database only. The blob
volume (replays, records, maps) and the keys still need the manual archives above;
the blob volume is content-addressed, so an older blob archive plus a newer dump
restores everything except files added in between.

**Setting up** (once per deployment; the official instance uses the bucket
`glob2-backups-pharaoh-418820` in `northamerica-northeast2` and the service account
`glob2-staging-host`):

```sh
gcloud storage buckets create gs://BUCKET --location REGION --uniform-bucket-level-access \
    --public-access-prevention --soft-delete-duration 0 \
    --lifecycle-file deploy/gcs-backup-lifecycle.json
gcloud iam service-accounts create glob2-host
for role in roles/storage.objectCreator roles/storage.objectViewer; do
  gcloud storage buckets add-iam-policy-binding gs://BUCKET \
      --member serviceAccount:glob2-host@PROJECT.iam.gserviceaccount.com --role $role
done
# The VM must run as that account with a scope that allows writing to Cloud
# Storage (the default scopes are read-only). This needs the VM stopped:
gcloud compute instances stop glob2-host --zone ZONE
gcloud compute instances set-service-account glob2-host --zone ZONE \
    --service-account glob2-host@PROJECT.iam.gserviceaccount.com --scopes cloud-platform
gcloud compute instances start glob2-host --zone ZONE
```

The account has no project-wide role, so `cloud-platform` scope gives it nothing
beyond the bucket. Soft delete is off so that deleted backups are really gone when
the lifecycle rules remove them. Then, on the host, add
`GLOB2_BACKUP_BUCKET=BUCKET` to the env file and install the timer:

```sh
sudo deploy/install-backup-timer.sh /opt/glob2/config/staging.env
sudo systemctl start glob2-backup.service        # one backup now
journalctl -u glob2-backup.service -n 20         # its log
systemctl list-timers glob2-backup.timer         # the next run
gcloud storage ls -l gs://BUCKET/daily/ gs://BUCKET/weekly/ gs://BUCKET/monthly/
```

The units run the scripts from the checkout, so a deployment updates them; rerun
`install-backup-timer.sh` only when `deploy/systemd/` changes.

**Restoring.** `deploy/restore-backup.sh` restores into a new database next to
the live one and never writes to `glob2`:

```sh
deploy/restore-backup.sh /opt/glob2/config/staging.env \
    gs://BUCKET/daily/glob2-20261003T031700Z.dump glob2_restore_20261003
```

It creates the database, restores the dump into it, applies newer migrations, and
then **re-applies deletions**: every account listed in the newest
`*.deleted-accounts.txt` in the bucket, or deleted in the live database if that is
still running, that is not deleted in the restored copy is deleted there again
with `platform admin delete`, the same scrub as the original deletion. The one gap
is a deletion made after the newest backup when the live database is also lost:
nothing records it, so such an account comes back and has to be deleted again.

Check the restored database (`docker compose exec postgres psql -U glob2 -d
glob2_restore_20261003`), then drop it, or put it into service:

```sh
docker compose stop platform-api platform-worker relay engine-agent
docker compose exec -T postgres psql -U glob2 -d postgres \
    -c 'ALTER DATABASE glob2 RENAME TO glob2_replaced' \
    -c 'ALTER DATABASE glob2_restore_20261003 RENAME TO glob2'
docker compose up -d --wait       # init hands the objects to glob2_migrator and re-grants
```

Drop `glob2_replaced` once the instance works again.

## Upgrades

Before every upgrade, take a [backup](#backups-and-restore) and read the release
notes for migrations and sim-version changes. `deploy/update-host.sh` (below)
takes the database dump and a copy of the served web client itself.

```sh
git pull                       # or set new GLOB2_*_IMAGE digests in .env
docker compose build           # skip with prebuilt images: docker compose pull
docker compose up -d --wait
```

On a single host, `deploy/update-host.sh <env-file> [git-ref]` does all of this in
one command, in an order that never leaves a half-upgraded instance:

1. **Backup.** A `pg_dump` and an archive of `GLOB2_WEB_CLIENT_DIR` go to
   `GLOB2_BACKUP_DIR/<UTC time>/` with the running revision; the newest
   `GLOB2_BACKUP_KEEP` are kept.
2. **Build**, while the old stack keeps serving: the WebAssembly client (with
   `deploy/build-web-client.sh`; Emscripten runs in a container, so the host needs
   only Docker) into the build tree, and every image with the checkout's sim
   version. The running images are first tagged `:previous`. A failed build
   changes nothing that runs.
3. **Swap:** `up --wait` (migrations run first, in `init`).
4. **Web client last:** only once the new stack is healthy is the new client
   installed at `/play/`, so browsers never load a client newer than the platform
   and engine agents behind it.

If the new stack does not become healthy, the script starts the `:previous`
images of the previous revision again and leaves the web client as it was. The
previous revision is the one the last successful run recorded in
`GLOB2_DEPLOYED_REVISION_FILE`, not the checkout's `HEAD`, so checking out the new
ref before running the script (to run its newest version) is safe. Before the first
recorded deployment the script assumes `HEAD` runs; on an instance deployed by
hand, write the running commit to that file first. It
does not restore the database: migrations are forward-only and must keep the
previous release working (expand, then contract in a later release), so the
previous release normally runs on the new schema. When it does not, restore the
dump the script took; it prints the `pg_restore` command.

Migration numbers: parallel branches reserve ranges (a branch adds its files after
the highest number on the integration branch and leaves a gap for the others), so
files are applied in number order on a fresh database. Kysely's migrator refuses a
database that has a later migration applied but not an earlier one, so deploy
branches that add migrations in number order.

`up` runs `init` first, which updates the database roles and applies new migrations
(forward only, each in a transaction) before the new API and worker start. `platform-api` and
`platform-worker` replicas are replaced together: realtime clients reconnect after
a few seconds. Caddy keeps WebSockets open across its own configuration reloads.

### Upgrading to database roles

Instances deployed before the [database roles](#database-roles) have every object
owned by the superuser and every service connected as it. The upgrade is the usual
`up` (or `deploy/update-host.sh`) with the new `compose.yaml`; nothing in `.env`
changes:

1. Take a [backup](#backups-and-restore).
2. `docker compose up -d --wait` (or `update-host.sh`). `init` generates the role
   passwords and the agent key, creates the roles, hands every table, view,
   sequence, function, type and the `graphile_worker` schema to `glob2_migrator`
   (idempotent, a few seconds), applies migrations 0010-0013 as `glob2_migrator`,
   then starts the services with their own roles. Migration 0010 moves queued
   engine jobs from graphile-worker to the `engine_jobs` lease queue: queued
   verify-match jobs run again (recovering matches whose verdict was lost), and
   queued map jobs older than an hour are closed as failed.
3. Check: `docker compose exec -T postgres psql -U glob2 -d glob2 -c "SELECT usename, application_name FROM pg_stat_activity WHERE datname = 'glob2'"`
   shows `glob2_api` and `glob2_worker` and no `glob2` sessions apart from your own,
   and `platform_stack_smoke.py --attach` passes.

Rolling back to a release without roles needs the superuser connection back:
restore the previous `compose.yaml` and its images; the services connect as `glob2`
again (a superuser can use objects `glob2_migrator` owns), and the extra columns
and tables of 0010-0013 are ignored by older code except that engine agents of the
older release take jobs from graphile-worker, where newer releases no longer put
them, so roll back the API and worker together with the agents.

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
Records not yet uploaded stay in `relay-spool` under the relay's id; the next relay
that starts with that id, or that finds the directory unclaimed, re-sends them.

### Sim versions and engine agents

A sim version is `VERSION_MINOR`, `NET_PROTOCOL_VERSION` and a hash of `SIM_REVISION`
(`src/game/SimRevision.h`, bumped with every simulation change) and the data files that
affect the simulation (`python3 deploy/sim_version.py` prints it for a source tree; a
tree from before `SIM_REVISION` keeps its earlier key). Players are only matched with the same version, and only an engine agent of
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

The engine stage builds the pinned SDL3 family with `scons/sdl3_dependencies.py`
from the engine source, so this works for releases from the SDL3 migration on. An
SDL2-era release (before #487) has no such helper; build its agent from that
release's own Dockerfile and dependencies instead.

The verifier must compute byte-identical games to the release's clients, so build
it with the compiler and flags of that release where they differ, and check it
against a recorded game ([replay verification](../development/headless-replays.md))
before trusting its verdicts.

Each image knows its own version (from its binary and data), registers it, and
takes only that version's jobs; `GET /api/v1/instance` lists every version with a
live agent. Remove the service when the version is retired.

### Images

The Dockerfile targets are `platform` (API, worker and CLI), `music-worker`, `engine-agent`,
`relay` and `caddy` (Caddy with the built web app). A `server-v*` tag pushed by
the owner to the release mirror `genixpro/glob2-release` runs
`.github/workflows/server-image.yml` (it skips every job in any other repository; a
fork or self-hoster builds images with `deploy/compose.yaml` instead), which
publishes all of them for linux/amd64 and linux/arm64 to
`ghcr.io/genixpro/glob2-release-<target>`, tagged with the Git tag and commit; engine
agents are also tagged `simver-<sim version>` and labelled
`org.glob2.sim-version`. Pin digests in production:

```dotenv
GLOB2_PLATFORM_IMAGE=ghcr.io/<owner>/<repository>-platform@sha256:…
GLOB2_MUSIC_IMAGE=ghcr.io/<owner>/<repository>-music-worker@sha256:…
GLOB2_ENGINE_AGENT_IMAGE=ghcr.io/<owner>/<repository>-engine-agent@sha256:…
GLOB2_RELAY_IMAGE=ghcr.io/<owner>/<repository>-relay@sha256:…
GLOB2_CADDY_IMAGE=ghcr.io/<owner>/<repository>-caddy@sha256:…
```

### Automatic deployment

The official instance, `https://app.glob2online.com` (VM `glob2-staging` in project
`pharaoh-418820`, zone `northamerica-northeast2-a`), is deployed by
`.github/workflows/deploy-online.yml`. Like the store release workflows it is
defined here but runs only in the owner's release mirror `genixpro/glob2-release`:
every job is skipped in any other repository, for any other actor, and off the
mirror's `master`. Mirror a reviewed commit there to make a new version of the
workflow take effect.

**What it deploys.** The host can only check out public commits, so:

- A push to the mirror's `master` (when enabled, below) and a dispatch with
  `ref` = `master` deploy the newest public `master` commit that the mirror's
  `master` contains (their merge base).
- A dispatch with another `ref` deploys that public branch, tag or full commit id.
  It must be on public `master` unless `allow_unmerged` is ticked (for example to
  return to a commit of an integration branch).

The `preflight` job writes the commit, its source and the trigger to the run
summary.

**How it runs.** The `deploy` job uses the `online-production` environment
(restricted to `master`; add required reviewers there to approve each deploy). It
authenticates to Google Cloud without keys through Workload Identity Federation,
adds a three-hour SSH key for the deploy user to the VM's instance metadata,
connects through an IAP TCP tunnel and pipes `deploy/online-deploy.sh` to the host.
That script refuses to start while another `update-host.sh` runs (a process check
plus a lock), records the running revision if there is no record yet, checks out
the commit, and starts `deploy/update-host.sh` detached under `nohup`, with its log
and exit status in `/opt/glob2/deploys/gh-<run id>-<attempt>/`. The job then polls
once a minute, runs the attached smoke test (`platform_stack_smoke.py --attach`,
with `GLOB2_ONLINE_WEBSITE` as `--website`), writes the result, the deployed and
previous revisions, the rollback status, the sim version and the backup directory
to the run summary, and removes its SSH key. Cancelling the run, or the run timing
out, does not stop the deploy on the host; check it there with
`sh /opt/glob2/src/deploy/online-deploy.sh status /opt/glob2/config/staging.env gh-<run>-<attempt>`.

One deploy runs at a time (concurrency group `deploy-online`). A run in progress
is never cancelled; a newer request replaces one that is still waiting. The job
waits on a GitHub-hosted runner for the whole deploy (30–50 minutes when the images
and the web client are rebuilt); standard runners are free for public repositories
such as the mirror, otherwise this is about 50 runner minutes per deploy.

**Trigger, enable and disable.**

```sh
gh workflow run deploy-online.yml -R genixpro/glob2-release -f ref=master
gh variable set AUTO_DEPLOY_ONLINE -R genixpro/glob2-release -b true   # deploy every mirrored master
gh variable set AUTO_DEPLOY_ONLINE -R genixpro/glob2-release -b false  # back to manual only
```

Without `AUTO_DEPLOY_ONLINE` = `true`, pushes run nothing. An automatic deploy is
skipped when the deployed revision already contains the commit (the same commit, or
a mirror that is behind what was deployed by hand); a dispatch always deploys.

**Roll back.** `update-host.sh` already returns to the previous release when the
new stack does not become healthy (the summary's rollback row says
`rolled-back`, `failed` or `unchanged`). To go back after a successful deploy,
dispatch again with `ref` = the previous revision from the summary (tick
`allow_unmerged` if it is not on public `master`). Database restores stay manual;
see [Upgrades](#upgrades).

**Repository variables** in the mirror: `GLOB2_ONLINE_WIF_PROVIDER`,
`GLOB2_ONLINE_SERVICE_ACCOUNT`, `GLOB2_ONLINE_PROJECT`, `GLOB2_ONLINE_ZONE`,
`GLOB2_ONLINE_INSTANCE`, `GLOB2_ONLINE_SSH_USER` (the host user that owns
`/opt/glob2` and is in the `docker` group), `GLOB2_ONLINE_ENV_FILE`
(`/opt/glob2/config/staging.env`), optionally `GLOB2_ONLINE_WEBSITE`
(`https://glob2online.com`) and `AUTO_DEPLOY_ONLINE`. None of them is a secret.

**Google Cloud identity.** Each piece, and why it exists:

| Resource | Scope | Why |
| --- | --- | --- |
| Provider `glob2-online-deploy` in pool `github-actions` | the pool | Accepts only GitHub OIDC tokens whose repository is `genixpro/glob2-release` (and its id), actor id the owner's, ref `refs/heads/master`, event `workflow_dispatch` or `push`, environment `online-production`, a GitHub-hosted runner and workflow `deploy-online.yml@refs/heads/master`. It maps `attribute.online_deploy_repository_id`, which no other provider in the pool sets. |
| Service account `glob2-online-deployer` | | The identity of the workflow; no keys. |
| `roles/iam.workloadIdentityUser` on the service account for `principalSet://…/github-actions/attribute.online_deploy_repository_id/1397722696` | the service account | Lets tokens from that provider act as it. |
| Custom role `glob2OnlineDeployInstance` (`compute.instances.get`, `compute.instances.setMetadata`) | the VM only | Read the VM and add or remove the short-lived SSH key in its own metadata. Nothing project-wide: `gcloud compute ssh` is not used because it also reads and tries to write project metadata. |
| `roles/iap.tunnelResourceAccessor` | the VM's IAP tunnel resource only | Open the IAP TCP tunnel to port 22. |
| Firewall rule `glob2-staging-iap-ssh`: tcp:22 from `35.235.240.0/20` to tag `glob2-staging` | the network | IAP's forwarding range, so SSH keeps working through IAP if the open `default-allow-ssh` rule is ever removed. |

Setting instance metadata is root-equivalent on the VM (as is the deploy user's
`docker` group), so the account is as powerful as a person deploying by hand, but
only on that VM. If Compute Engine asks for `iam.serviceAccounts.actAs` on the
VM's service account when setting metadata, grant `roles/iam.serviceAccountUser`
on that service account only (better: move the VM to the dedicated
`glob2-staging-host` account first). The commands that set this up:

```sh
P=pharaoh-418820 Z=northamerica-northeast2-a SA=glob2-online-deployer@pharaoh-418820.iam.gserviceaccount.com
POOL=projects/485653453075/locations/global/workloadIdentityPools/github-actions
gcloud iam service-accounts create glob2-online-deployer --project $P
gcloud iam workload-identity-pools providers create-oidc glob2-online-deploy --project $P \
    --location global --workload-identity-pool github-actions \
    --issuer-uri https://token.actions.githubusercontent.com \
    --attribute-mapping google.subject=assertion.sub,attribute.online_deploy_repository_id=assertion.repository_id \
    --attribute-condition "assertion.repository_id == '1397722696' && assertion.repository == 'genixpro/glob2-release' && assertion.actor_id == '6193625' && (assertion.event_name == 'workflow_dispatch' || assertion.event_name == 'push') && assertion.environment == 'online-production' && assertion.runner_environment == 'github-hosted' && assertion.ref == 'refs/heads/master' && assertion.workflow_ref == 'genixpro/glob2-release/.github/workflows/deploy-online.yml@refs/heads/master'"
gcloud iam service-accounts add-iam-policy-binding $SA --project $P --role roles/iam.workloadIdentityUser \
    --member principalSet://iam.googleapis.com/$POOL/attribute.online_deploy_repository_id/1397722696
gcloud iam roles create glob2OnlineDeployInstance --project $P \
    --permissions compute.instances.get,compute.instances.setMetadata
gcloud compute instances add-iam-policy-binding glob2-staging --zone $Z --project $P \
    --member serviceAccount:$SA --role projects/$P/roles/glob2OnlineDeployInstance
# gcloud has no command for a single instance's IAP tunnel policy, so call the
# API. This replaces the instance's tunnel policy; read it first with
# :getIamPolicy and add to it if it already has bindings.
curl -sS -X POST -H "Authorization: Bearer $(gcloud auth print-access-token)" \
    -H "Content-Type: application/json" \
    -d "{\"policy\":{\"bindings\":[{\"role\":\"roles/iap.tunnelResourceAccessor\",\"members\":[\"serviceAccount:$SA\"]}]}}" \
    "https://iap.googleapis.com/v1/projects/$P/iap_tunnel/zones/$Z/instances/glob2-staging:setIamPolicy"
gcloud compute firewall-rules create glob2-staging-iap-ssh --project $P --network default \
    --source-ranges 35.235.240.0/20 --allow tcp:22 --target-tags glob2-staging
```

To revoke the pipeline, delete the provider (or disable it with
`gcloud iam workload-identity-pools providers update-oidc … --disabled`).

## Operations

- Logs: `docker compose logs -f platform-api relay`; Compose keeps up to 100 MB per
  container (local driver, five 20 MB files).
- Relay metrics are Prometheus text at `http://<relay>:7495/metrics` on the backend
  network (not public).
- Admin CLI: `docker compose run --rm --no-deps platform-api platform admin grant|revoke|ban|delete …`
  ([what deleting keeps and removes](../multiplayer/identity.md#administration)).
- Failed verifications: a match whose verify job failed or was lost is marked
  `failed` and not rated, and the worker logs `match verification failed` at error
  level (alert on it). List them with `platform matches failed` and re-run one with
  `platform matches reverify <match id>` (`--force` replaces a queued job), or
  `POST /api/v1/admin/matches/<id>/reverify` as an administrator
  ([details](../multiplayer/ratings-and-matchmaking.md#applying-a-verdict-exactly-once)).

### Retention

The worker leader deletes old rows every minute (`apps/worker/src/maintenance.ts`,
at most 1000 rows per table and run) and collects blobs every six hours:

| Data | Kept |
| --- | --- |
| Refresh tokens | rotated or revoked: 7 days (reuse detection); expired: 30 days after expiry |
| Web sessions, provider sign-in flows | 30 days after expiry or revocation; 24 hours after expiry |
| Browser sign-in attempts | 7 days once finished |
| Guest accounts | deleted when unused for 90 days (no sign-in or realtime session, no device use) and they never played a match, host no open room and own no catalog map |
| Room chat | 30 days |
| Engine jobs | 30 days after completion; each match's latest succeeded verify job stays |
| Match proposals, finished queue tickets | 30 days |
| Engine agents not seen | 7 days |
| Spilled NOTIFY payloads | 1 hour |
| Blobs | unreferenced ones (no map version, preview, match artifact, upload, generated map (warm pool maps included), or match played on the map) 7 days after creation; stored files no `blobs` row names, 7 days after they were written |

Matches, participants, ratings, rating history, catalog maps, map download counts
(by account, or by IP address for downloads without an account) and the audit log
are kept. The official instance's [privacy policy](../mobile/privacy-policy.md)
states these periods to players; change it together with `maintenance.ts`.
- Migrations: `docker compose run --rm --no-deps init node packages/db/src/cli.ts status`.
- Stopping: `docker compose stop` drains relays (up to `GLOB2_RELAY_STOP_GRACE`);
  `docker compose down` keeps volumes; `down --volumes` deletes all data.

## The former YOG lobby

The YOG lobby and router, and their stack (`compose.legacy.yaml`), were removed
at the M9 cutover. Nothing is migrated from a YOG deployment: accounts, ratings,
match history and the map catalog start fresh on the platform. Archive the old
`lobby-data` and `router-data` volumes if you want to keep them; this stack never
reads them. Released clients that only speak YOG can no longer play online until
they are updated.

## Testing a deployment

```sh
python3 test/deployment/platform_stack_smoke.py --log-dir artifacts/platform-stack
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
leaves the stack running. CI runs it when `deploy/`, `test/deployment/`,
`src/relay/`, migrations or platform dependencies change, and on full runs.
`--match-e2e` also plays a rated quick match through the stack; see
[End-to-end test of the stack](#end-to-end-test-of-the-stack).

On a running deployment, the same checks run against the public origin with a
publicly trusted certificate, the deployed web client and the replica counts in
the env file (run on the host; nothing is started or removed):

```sh
python3 test/deployment/platform_stack_smoke.py --attach glob2-platform \
    --env-file /path/to/deployment.env --log-dir artifacts/live-smoke
```

`test/deployment/live_match_e2e.py` then plays a real match on the instance: two
guests create and join a room by invite code, start it with AI seats on a generated
map, and two headless native clients (`glob2 --turn-client`, built from the same
sim version) play it through the relay until a sudden-death rule ends it. It checks
that both clients' per-tick checksums agree, and, with `--psql`, that the relay
reported the match, uploaded its record and the verify-match job judged it
`verified`:

```sh
python3 test/deployment/live_match_e2e.py --origin https://play.example.org \
    --glob2 build/linux/client/release/src/glob2 --out artifacts/live-e2e \
    --psql "docker compose -p glob2-platform exec -T postgres psql -U glob2 -d glob2 -At"
```

`--mode queue --queue <id>` plays a rated quick match instead: two new local
accounts (the instance needs local sign-in) join a rated 1v1 queue, accept the
ranked prompt and play. Player A quits first, which leaves B the winner (B
would quit 30 s later otherwise), so the verified match is rated; with `--psql` the script also checks
that both players' ratings on the queue's ladder changed. `--engine-command`
replaces `--glob2` with a command prefix, for example a `docker run` of the
engine-agent image, and `--ca-file` trusts a private CA.

### End-to-end test of the stack

One command builds the stack from this checkout, starts it, plays a rated quick
match through it and tears it down again. It needs a Linux machine with Docker
(Compose v2) and Python 3; the clients use host networking, so Docker Desktop on
macOS does not work.

```sh
python3 test/deployment/platform_stack_smoke.py --jobs 8 --log-dir artifacts/stack-e2e --match-e2e
```

After the smoke checks above, it runs `live_match_e2e.py --mode queue` against the
fresh stack:

1. The instance has local sign-in and a small rated queue (`e2e-ranked`, one
   128×128 generated map).
2. Two new local accounts join the queue, get the ranked accept prompt and accept.
3. Two headless clients (`glob2 --turn-client`) play the match through a relay. They
   run from the engine-agent image, so client, relay and verifier share one build
   and sim version. Their per-tick checksums must agree.
4. Player A quits after 40 s, so B wins and the game ends (B would quit 30 s later
   otherwise). The relay uploads the match record and reports the end.
5. The verify-match job must judge the match `verified`. The worker must then apply
   ratings: `rating_status = applied`, and one won and one lost `rating_history` row
   on the `e2e-ranked` ladder, both with changed μ.

The first run builds every image (about 15 minutes on 4 cores); later runs on the
same Docker host reuse the BuildKit caches. The match adds about three minutes.
Everything ends up in the log directory: the compose logs, `results.json` of the
smoke checks, and under `match-e2e/` the clients' logs, results and checksum
traces, plus the end-to-end `results.json`. The exit status is 0 only if every
check passed. Add `--keep` to leave the stack running for inspection.

The web app's browser suites (`platform/apps/web/e2e`: page smoke tests and axe
accessibility checks on desktop and phone) run separately, against a seeded API:
`npm run build -w @glob2/web && npm run e2e -w @glob2/web` in `platform/`, with the
test Postgres running.

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
   reuse the BuildKit caches. The official instance is redeployed from GitHub Actions instead;
   see [Automatic deployment](#automatic-deployment).
6. **Check.** Run the attached smoke test and the live match above.

Approximate cost (2026 on-demand list prices, a Canadian region): the VM about
US$110 a month, the disk about US$10, the static address in use about US$3, a
Cloud DNS zone about US$0.20, plus egress. Stopping the VM stops the machine
charge; the disk and a reserved but unattached address still cost. To remove
everything: delete the VM, the address, the firewall rule and the DNS records.

## Limits

- One host. Postgres, the blob volume and Caddy are single instances; S3-compatible
  blob storage is not implemented yet (`BLOB_STORE=s3`; the `BlobStore` interface
  includes the optional `list()` the blob collector uses for stored files no row
  names).
- Relays on other hosts need manual setup (above).
- Until the engine reports its own data hash (`glob2 --sim-version`), the
  engine-agent image passes the hash computed by `deploy/sim_version.py` at build
  time; the agent refuses to start if the two ever disagree.
- The relay key is one shared secret for all relays; the engine-agent key likewise
  for all agents. A compromised agent (the engine runs untrusted files) can still
  report forged results for jobs it leases, such as match verdicts.
- Abuse limits that guard sign-in, password guessing, guests, browser sign-in
  attempts, uploads, catalog writes and chat are shared by all API replicas
  (Postgres counters). The general per-address request cap
  (`limits.apiPerMinute`) and the realtime connection and message limits are per
  replica.

### Optional AI Map Studio

AI Map Studio is an authoring service on the app host. It has a separate prepaid
map wallet; a delivered map or revision costs one map credit. Discussion is
included, subject to `chatPerHour` and the shared `providerCallsPerDay` operator
budget. These credits cannot fund Hive Mind. Only registered accounts use it.

Configure `mapStudio` in the instance file with `enabled`, `salesEnabled`, pinned
`textModel` and `imageModel`, `pipelineVersion`, a positive daily provider-call
budget, and one-time Stripe packs. Leave both flags false until real provider
quality/cost qualification and Stripe test-mode checkout/webhook verification
pass. No production prices are supplied by the repository. The daily call budget
bounds calls, not a currency amount; also configure a provider project spend cap.

The API uses `MAP_STRIPE_SECRET_KEY` and `MAP_STRIPE_WEBHOOK_SECRET`; the optional
worker alone uses `MAP_OPENAI_API_KEY`. Run the Compose profile with
`docker compose --profile ai-maps up -d --build`. The worker image contains Python,
Pillow, native assets, generator descriptions and a matching engine binary. Its
sources and binary must come from the same engine build context. Existing engine
agents consume `import-ai-map` jobs; the platform worker applies their results.

Expose the signed Stripe webhook at `/api/v1/map-studio/stripe`. Checkout return
pages only refresh account state. Credit fulfillment follows authoritative paid
sessions and handles delayed payments, refunds and disputes idempotently.

Generation requests are a durable database queue. One request per account may be
active. Reservations and request creation are transactional; delivery, catalog
registration and charging are transactional too. Provider attempts are journaled
before dispatch. Deterministic stages resume from private content-addressed
checkpoints. Unknown provider outcomes become `uncertain`, hold the credit, and
block further requests for that account until reconciled. Administrators can
mark a confirmed unrecoverable request failed using
`POST /api/v1/admin/map-studio/requests/<id>/fail`; this releases the credit and
records an audit entry. Never re-dispatch an uncertain paid provider request.

Deploy the additive studio-event migration first, then replace and drain all old
authoring workers before updating the API and web client. The migration backfills
anonymous daily provider-call totals and counts subsequent journal inserts
transactionally, including calls from old workers. Updated workers enforce their
budget against these totals; old workers still count private journal rows, which
the updated API removes on account deletion. Do not enable the updated deletion
path while old workers remain. Keep the previous long-poll route during client rollout.
The stage event journal and authorized artifact records are retained with each
thread; do not prune their cursor history independently of the thread.

Studio progress uses persistent SSE at
`/api/v1/map-studio/threads/<id>/events`. Preserve `text/event-stream`,
`Cache-Control: no-cache, no-transform`, and `Last-Event-ID` through the edge.
Caddy's reverse proxy flushes SSE responses as they arrive; any additional load
balancer must disable buffering for this content type and permit connections
with 15-second heartbeat intervals. Verify that an authenticated `curl -N` through
the public edge receives the initial comment immediately, then stage events
before generation finishes. Reconnect with the last event ID to check replay.
Streams do not own worker lifetimes; disconnecting never cancels generation.
Stream logs include delivered-event counts, cursors, connection duration and
closure errors, without message text or provider diagnostics. Event timestamps
provide stage duration and delivery latency evidence.

Monitor `studio_requests` status/age, `studio_attempts` usage/model, map-wallet
reservations, delivery failure rates and queue age. Pause sales or generation via
the instance flags; retain blobs and payment journals during rollback. Migrations
are additive and preserve existing Hive tables and historical purchase IDs.

## Music worker

The default Compose stack includes `music-worker` (Dockerfile target
`music-worker`, optional pinned `GLOB2_MUSIC_IMAGE`). It uses the worker database
role and blob volume on the internal backend network. FFmpeg runs only in this
service, independently of engine agents and sim versions. The image includes
FFmpeg, Python and the existing mastering dependencies. Website builds also build
the small WASM preview decoder with the game's pinned Opus dependencies.

Default limits are one conversion per worker, two CPUs, 12 GiB memory, 6 GiB
private temporary storage, 64 processes and a 30-minute processing deadline.
These allow optional mastering of a 15-minute stereo trio. Inputs are at most
512 MiB per mood and 8 MiB per cover (4096 pixels per side before thumbnailing).
Only direct media containers are accepted; decoder network protocols and
playlist/concat inputs are disabled. The API admits one buffered source upload
per replica at a time, so allow memory headroom for a 512 MiB request and HTTP
buffer copies. Each registered creator may create six releases/day, keep three
active uploads and submit 24 files/hour. Bulk downloads contain at most ten sets
and fit the game's 64 MiB archive limit.

The worker's container health check checks a fresh heartbeat after a database
round trip. Inspect queue/status totals and stored byte counts at
`GET /api/v1/admin/music-status`, or use the Music administration tab. Monitor
worker health, conversion failures, oldest pending jobs and blob-volume capacity.
A cancelled job stops its decoder group and removes temporary PCM. Retries remove
interrupted attempt directories for that release before starting again. The
platform scheduler expires abandoned sources after 24 hours and collects orphan
source files; successful conversion retains only final outputs.

Back up music with the existing PostgreSQL dump and `blobs` volume backup: both
metadata/references and media are necessary. Converted audio, artwork, waveform
summaries and ZIPs are protected by `music_assets` references during blob GC.
Withdrawn/hidden releases retain immutable output for administration but public
media routes deny access. Original uploads are temporary and cannot be recovered
after normal cleanup; creators should retain their source files. On restoration,
expired in-flight uploads should be re-uploaded if their sources are absent.
