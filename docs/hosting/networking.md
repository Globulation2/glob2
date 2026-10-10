# DNS, TLS and application links

Expose the public edge, separate the website from the app and configure native app invitation links.

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
repository on Firebase Hosting (project `pharaoh-418820`), with the [app-route redirects](#separate-public-website-and-app) in its `firebase.json`.
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
[architecture](../multiplayer/engine-agents.md#engine-agents)). It is served by
`platform-api` on the backend network only; relays and agents reach it at
`http://platform-api:8080` with their bearer keys, and relays fetch the JWKS from
there too. Caddy never forwards it.

Caddy adds security headers to everything it serves: `Strict-Transport-Security`
(`max-age` from `GLOB2_HSTS_MAX_AGE`, default one year; `0` turns it off),
`X-Content-Type-Options: nosniff`, a `Referrer-Policy` and `X-Frame-Options: DENY`,
and a `Content-Security-Policy` with `frame-ancestors 'none'`:

- the web app: scripts, styles, fonts and data from the instance origin only;
  `/music/decode-worker.js` additionally permits `'wasm-unsafe-eval'` to compile
  the Opus decoder inside its dedicated worker; JavaScript `eval` stays blocked;
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
`mobile/ios.py` writes `applinks:<host>` into the iOS entitlements, both using the default host
`app.glob2online.com`. The Amazon and China editions leave online play out, so they
declare neither invite links nor associated domains.

#### Setting up the official instance

Configure the official instance with its registered application identities.
The workflow records Apple Team ID `CL2MNNYQX3`; confirm it against the account
before updating the association files. Android associations require the Play
app-signing certificate. Supply these values:

| Value | Where it comes from |
| --- | --- |
| Google Play app-signing certificate SHA-256 | Play Console > the app > Test and release > App integrity > App signing key certificate, "SHA-256 certificate fingerprint" (uppercase hex with colons, as the config expects). Play re-signs every installed copy with this key, so it is the one phones check; the upload key is not. |
| Other Android signing keys (optional) | F-Droid signs its own APKs. Add F-Droid's certificate SHA-256 (`apksigner verify --print-certs <F-Droid APK>`) for F-Droid installs to get verified links; without it they open the invite page, whose "Open in the Globulation 2 app" button still works. Up to eight fingerprints are allowed. |
| Apple Team ID | The TestFlight workflow signs with team `CL2MNNYQX3`; confirm it on the Apple Developer account's Membership page. |
| Associated Domains capability | Enable it on the `org.globulation2.glob2` App ID (Certificates, Identifiers & Profiles), so the App Store provisioning profile carries `com.apple.developer.associated-domains`. Check an exported build with `codesign -d --entitlements - Glob2.app`: it must list `applinks:app.glob2online.com`. |

The release mirror's **App signing fingerprints** workflow reads these values with
the release credentials, enables Associated Domains on the App ID when it is
missing, and ends with a ready-to-paste `appLinks` block
([signing fingerprints for invite links](../mobile/ios.md#signing-fingerprints-for-invite-links)).
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

[Hosting index](README.md) · [Documentation index](../README.md).
