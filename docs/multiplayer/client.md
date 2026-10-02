# Online client

`src/online/` is the game's side of the online platform: one realtime
connection per instance with sign-in, token refresh and reconnection, the
instance setting and per-instance credentials, a content-addressed map cache,
and invite links. It has no screens of its own; the online hub and room
screens build on the API below. The platform's JSON shapes are defined in
`platform/packages/protocol` (`realtime.ts`, `resources.ts`); identity and
tokens are described in `docs/multiplayer/identity.md`.

## Threading and polling

Everything runs on the UI thread. `PlatformClient::update()` advances the
socket, HTTP requests and timers, and runs every callback; nothing blocks and no
callback runs on another thread. `Online::pump()`, called by `Application` every
frame, updates the shared client once it exists, so sign-in, refreshes and
keepalives continue while the player moves between screens. Screens poll state
from `onTimer` or register listeners.

## PlatformClient

`Online::services()` (`OnlineServices.h`) creates the shared `InstanceConfig`,
`PlatformClient` and `MapCache` on first use. The client is not started until a
screen calls `client.start(origin)`.

| Call | Effect |
| --- | --- |
| `start(origin)` / `stop()` | Sign in and connect to an instance (normalized origin); `stop` closes everything and fails outstanding requests with `cancelled`. |
| `connection()` | `Stopped`, `Connecting`, `Handshaking` (`session.hello` sent), `Online`, `Waiting` (backoff; `retryInMs()`, `retryNow()`). |
| `auth()`, `account()` | `SignedOut`, `SigningIn`, `SignedIn`; the `SelfAccount` fields `id`, `displayName`, `kind`, `role`, plus `raw` JSON. |
| `simSupported()`, `sessionId()`, `lastError()` | From `session.hello`; the last connection or sign-in problem for a status line. |
| `request(method, params, handler, timeoutMs)` | A realtime request. Queued until the socket is online; the handler receives `{ok, result, error}` exactly once, or never after `cancelRequest(id)`. |
| `addListener(event, handler)` | Server events by name (`""` for all), e.g. `room.state`, `match.start`. `addStateListener` fires on any connection, auth, account or handoff change. |
| `rest(method, path, body, handler)` | An authenticated REST call (`Authorization: Bearer`). A 401 refreshes the token once and retries. `refreshAccount()` and `rename(name)` wrap `/api/v1/accounts/me`. |
| `beginBrowserSignIn(mode, provider)` | `auth.handoff.begin`, then opens the system browser at `signInUrl`. `handoff()` holds the state, `confirmationCode` to show, `browserOpened`, `failure` and `conflict`. `openSignInPage()` retries opening; `cancelBrowserSignIn()` cancels. |
| `signInAsGuest()`, `signOut()` | Guest sign-in with this device's credential (creating a guest if needed); sign-out revokes the refresh token and stays signed out. |
| `accessToken()` | For other HTTP requests to the same instance, such as map downloads. |

Error codes in a failed response are the platform's `ErrorCode` values, or
local ones: `timeout`, `disconnected` (the socket closed while the request was
in flight), `cancelled`, `network` and `http_<status>`.

### Connection

- The socket is `wss://<host>/realtime` (text frames, at most 64 KiB, the
  platform's limit). The first request is `session.hello` with protocol 1, the
  client platform and version, the sim version and, when signed in, the access
  token. An `unauthenticated` answer drops the token, says hello anonymously and
  signs in again; `update_required` stops the client.
- Lost or failed connections retry after `min(60 s, 1 s × 2^attempt)`, reduced
  at random by up to half so clients dropped together spread out. A successful
  hello resets the backoff. A connection that does not open within 20 s counts as
  failed.
- The server pings every socket every 30 s; the transport answers. When nothing
  has arrived for 25 s the client also sends `session.ping` and reconnects if it
  is not answered within 10 s, which detects dead connections the operating
  system has not noticed.
- Requests in flight when the socket closes fail with `disconnected`; requests
  not yet sent wait for the next socket until their own timeout.

### Sign-in and tokens

On `start` the client signs in with what it remembers for the instance: the
refresh token, else the guest device credential, else (by default) a new guest
from `POST /api/v1/auth/guest`, keeping the returned credential. It connects
after that attempt finishes, with or without an account.

- Refresh tokens rotate on every use and reuse revokes the whole sign-in, so
  the client never has two refreshes in flight. Every new refresh token is
  written to disk before it is used.
- The access token is refreshed after its lifetime (`exp − iat` from the JWT,
  independent of the local clock) minus the larger of one minute and a fifth of
  the lifetime, but not before half of it: at 8 minutes for the default 10. The
  new token is attached to the socket with `session.authenticate`.
- A refused refresh token (expired, revoked or reused) is forgotten; the client
  falls back to the device credential when automatic sign-in is on. Network or
  server errors retry after 4–60 s while the current token lasts.
- `session.revoked` signs out and turns automatic sign-in off, so a sign-out
  elsewhere or a ban is not undone by the device credential.
- `signOut()` posts `/api/v1/auth/sign-out`, forgets the tokens, turns automatic
  sign-in off and reconnects anonymously. The device credential is kept: it is
  the only key to a guest account, and a later `signInAsGuest()` returns to it.

### Browser sign-in

`beginBrowserSignIn` asks for an attempt, keeps the resume token in memory only
and opens the instance's sign-in page (never a page on another origin) through
`GAGCore::ApplicationHost::openUrl`: `SDL_OpenURL` natively, `window.open` in
the browser. Browsers allow `window.open` reliably only inside a click handler,
so a web hub shows the code with an "Open sign-in page" button calling
`openSignInPage()` when `browserOpened` is false. After a reconnect the client
sends `auth.handoff.resume`, so a phone that lost its socket while the browser
was in front still receives `auth.handoff.completed`. Without a result the
attempt fails locally as `expired` one minute after its expiry.

## Instances and stored credentials

`InstanceConfig` keeps the selected instance (the official one,
`OFFICIAL_INSTANCE_ORIGIN`, by default) and, per
instance origin, the device credential, refresh token, automatic sign-in flag,
last display name and trust. It is stored in `online/instances.json` in the user
directory through `FileManager` (written atomically with mode 0600 for new files)
and, in the browser, in the site's IndexedDB-backed storage, synced after each
write with `ApplicationHost::persistStorage()`. Credentials are not in
`preferences.txt`, which players attach to bug reports.

The official origin is one build-time setting: `DEFAULT_ORIGIN` in
`scons/official_instance.py` (currently `https://glob2online.com`), overridable
with `scons official_instance=https://example.org`. Every build path passes it to
the client as `GLOB2_OFFICIAL_INSTANCE_ORIGIN`, and the Android and iOS packaging
scripts derive the App Link host and associated domain from it.

```json
{
  "version": 1,
  "selected": "https://glob2online.com",
  "instances": {
    "https://games.example.org": {
      "trusted": true, "autoSignIn": true,
      "deviceCredential": "<43 characters>", "refreshToken": "<opaque>",
      "lastDisplayName": "Guest-0042"
    }
  }
}
```

Origins are normalized (lowercase, no default port, no path); `http://` is
accepted only for loopback development instances. Credentials of one instance
are only ever sent to that instance.

**Trust.** The official and the selected instance are trusted. An invite link
to any other instance needs the player's confirmation (`isTrusted`, then
`trust(origin, remember)`); the prompt's "remember" box starts ticked
(`REMEMBER_TRUST_BY_DEFAULT`). Unremembered trust lasts until the game exits.

Settings › Online chooses the instance (the official one, a remembered one,
or any address, checked with `GET /api/v1/instance` before switching), shows
the display name and the sign-in methods linked on that instance, and signs
out. "Forget" drops everything remembered about an instance.

## Play screens

The play path is built from these screens (`Glob2UI::Screen` pattern,
`docs/development/ui-framework.md`), following the approved multiplayer
mock-ups:

| Screen | Code | What it does |
| --- | --- | --- |
| Online hub | `src/OnlineHubScreen.*` | "Play online" on the main menu. Starts the client (a guest is created on first contact), account chip with browser sign-in and the confirmation code, quick-match cards from `InstanceInfo.queues`, Create room, Join by code, public rooms (`GET /api/v1/rooms`), recent matches (hidden when empty), offline and update-required banners, and invite links (`takePendingJoin`) with the trust prompt for other instances. The legacy YOG lobby is reachable only through its "Legacy server" footer link until the cutover. |
| Room | `src/RoomScreen.*` over `RoomBackend` | One screen for online rooms (`Online::PlatformRoom`) and LAN rooms (`Lan::LanRoom`): Map / Players & Teams / Game Rules tabs, seats with controller, team and remove, invite (or how to join on the network), chat and ready. Phones get a Seats / Map / Rules / Chat bar and Start or Ready in the thumb corner, mirrored by the thumb-side setting. The host edits map and rules with the custom-game screen in room mode (`CustomGameScreen::useForRoom`); the server generates the map from the generator descriptor (`src/online/RoomSetup.*`). |
| Starting match | `src/MatchStartScreen.*`, `src/online/OnlineMatch.*` | From `match.start` to the first tick: seat confirmed, map download by hash, engine load, relay connection (`Online::RelayTransport`), waiting for the other players' presence. A relay that refuses the match as new (Reject 5) is reported with `match.reconnect {relayUnavailable: true}` and the new assignment restarts the flow. |
| In-game connection HUD | `src/gui/ConnectionOverlay.*` | Every turn game (online and LAN) shows a permanent panel with each player's state and latency where the "waiting for players" notice was, details on click or tap, one-line notices when a player drops or returns, and centre cards for this client's reconnect (with the grace time and Leave match), catch-up progress and desync rejoin. Presentation only: it reads `Engine::turnConnectionSnapshot()`. Other players' latency is their lag behind the relay from `Presence.lagTicks`; the relay does not report their round trip. |
| Results | `src/EndGameScreen.*` | Online matches add the outcome banner and the rating card, which `match.updated` updates live (verifying, verified, unverifiable, unrated room match, draw), and a link to `<origin>/matches/<id>`. Room matches return to the room. |
| Settings › Online | `src/SettingsScreenOnline.cpp` | See above. |

Quick-match cards call the handler registered with
`OnlineHubScreen::setQuickMatch` (the queue screens); without one the hub says
quick match is not available yet.

Assignment fields the client reads beyond `MatchAssignment`: an optional
`mapTitle`, and an optional `ratingPreview {ladder, before, ifWon, ifLost,
provisional}` for the greyed "1528 → 1543?" before verification. Until the
platform sends them the card shows "Verifying result…" without numbers.
Recent matches are the `match.updated` summaries this process has seen, until
the platform has history endpoints.

**End-to-end check.** `OnlinePlayHarness` (`scons release=1 server=0
online-play-test`) drives the real screens against a live instance in two
processes: the host signs in as a guest, creates a room and writes its code;
the guest joins by the code, takes a seat and readies; the host presses Start;
both play; after a minute the guest closes its window, the host wins and
reaches the results screen, then returns to the room. Every stage is captured:

```sh
build/darwin/client/release/src/OnlinePlayHarness host https://glob2online.com artifacts/e2e &
sleep 20
build/darwin/client/release/src/OnlinePlayHarness guest https://glob2online.com artifacts/e2e
```

## Map cache

`MapCache` stores platform maps by the SHA-256 of their decompressed bytes as
`online/maps/<hash>.map.gz` (saved games: `<hash>.game.gz`), readable like any gzip map through
`FileManager::openInflatingInputStreamBackend`. `fetch(origin, hash, headers)`
returns a polled download of `<origin>/api/v1/blobs/maps/<hash>` (the endpoint
arrives with rooms in M4; `MapCache::blobPath` is the only place naming it),
accepting gzip or raw bytes, refusing anything whose hash differs, and storing
it. Maps are limited to 64 MiB decompressed; the cache keeps at most 256 MiB,
evicting the least recently used. An index (`online/maps/index.json`) keeps
sizes and use order across restarts; unindexed files are removed at startup.
LAN guests store the maps they download from a host in the same cache.

## Invite links

| Form | Source |
| --- | --- |
| `glob2://join?instance=<origin>&code=<code>` | any instance; custom scheme |
| `https://<instance>/j/<code>` | the instance's web link (landing page in `apps/web`) |
| `?join=<code>` | the web client's page; the instance is the page's origin |
| `--join <link or code> [--instance <origin>]` | command line; a bare code without `--instance` means the official instance |

A link becomes the **pending join** (`Online::pendingJoin()`,
`takePendingJoin()` in `InviteLink.h`). The online hub takes it, asks for trust
when needed, connects to the instance and sends `room.join`. Links arrive:

- at launch, as a command-line argument (Windows, Linux, and the browser shell,
  which passes `--join <code> --instance <origin>` for `?join=`);
- while running on macOS and iOS (URL events, which SDL delivers as
  `SDL_DROPFILE`; `Application::frame` takes invite links out of the event
  stream);
- on Android through `Glob2Activity.takeLaunchLink()` (`onCreate` and
  `onNewIntent`; the activity is `singleTask`), polled by `Online::pump()`;
- for iOS universal links through `mobile/ios/LaunchLinks.mm`, which adds
  `application:continueUserActivity:restorationHandler:` to SDL's app delegate.

A running desktop game is not handed links from a second launch in v1: the
second launch joins directly. In-client "Join by code" covers the rest.

**Registration.**

| Platform | Where |
| --- | --- |
| Windows installer | `windows/win32_installer.nsi`: `HKCR\glob2` URL protocol → `glob2.exe "%1"` |
| macOS | `darwin/Info.plist` `CFBundleURLTypes` |
| Linux (deb, rpm, Flatpak, Snap) | `data/glob2.desktop`: `Exec=glob2 %u`, `MimeType=x-scheme-handler/glob2` |
| Android | intent filter for `glob2://join` and a verified App Link for `https://<official host>/j/` (`officialInstanceHost` placeholder) |
| iOS | `CFBundleURLTypes` and `applinks:<official host>` (written by `mobile/ios.py`) |

App Links and universal links cover the official domain only; self-hosted
instances use `glob2://`. They need the instance to serve
`/.well-known/assetlinks.json` (the release signing certificate's SHA-256) and
`/.well-known/apple-app-site-association` (team id plus
`org.globulation2.glob2`, path `/j/*`).

## Sim version

`session.hello` reports `SimVersion::local()`: `VERSION_MINOR`,
`NET_PROTOCOL_VERSION` and the simulation data hash (`simDataHash()` in
`src/online/SimVersion.cpp`, the same value `--sim-version` prints). Only a
process that never initialized the Toolkit file system (some unit tests) reports
64 zeros, which the platform answers with `simSupported: false`.

## Tests

- `glob2-unit-tests` (`test/online/`): envelope codec, timestamps, token
  lifetimes and refresh scheduling, backoff, SHA-256 vectors, origin
  normalization, configuration persistence and trust, link parsing and launch
  arguments, the map cache (verification, eviction, index recovery, downloads),
  and `PlatformClient` against a scripted socket, HTTP and clock (sign-in paths,
  hello, correlation, timeouts, events, queued requests, backoff, refresh and
  its serialisation, refusal fallback, keepalive, browser sign-in with resume,
  revocation, sign-out).
- `tests/online/test_platform_client.py` runs the real platform API (with a
  one-minute access token) behind a local TLS proxy and drives
  `platform-client-probe` (built by `transport-test`): guest creation,
  `session.hello`, request correlation and errors, REST, a dropped connection,
  a scheduled refresh, `auth.handoff.begin`/`cancel`, a returning launch,
  refresh-token reuse revoking the sign-in with fallback to the device
  credential, and sign-out. It needs `GLOB2_PLATFORM_DIR` (a `platform/`
  checkout with dependencies installed) and `GLOB2_PLATFORM_DATABASE_URL` (a
  Postgres role that may create databases); see the file's docstring.
