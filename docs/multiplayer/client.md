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

## Online screens

The quick-match, profile and map screens (multiplayer mock-up groups 3 and 7)
build on the client with the `Glob2UI::Screen` pattern. They are reached from
the online hub; the map chooser of the editor menu and the editor's own menu
also offer **Share online…**.

| Screen | File | What it does |
| --- | --- | --- |
| Quick match | `src/QuickMatchScreen.cpp` | Queue cards from `InstanceInfo.queues`; while searching, the timer, the opponent rating range, the relay region and round trip, the AI backfill countdown with the AI that would play and **Allow an AI opponent** (on by default). |
| Match found | `src/QuickMatchScreen.cpp` | Ranked queues: both players accept within the countdown, each player's answer shown live. AI-backfilled and casual matches: who you play, then a 3-second start countdown. |
| Profile | `src/OnlineProfileScreen.cpp` | Rating of each queue with its trend and provisional flag, win rate, typical game length and best map over recent games, the match list (filters All, Ranked, Rooms, vs AI) with Replay and the match page. |
| Maps | `src/OnlineMapsScreen.cpp` | Browse (search, size, colonies, sort, detail with the server preview, Use in a room, Like, map page, Report) and My maps (upload, checking, rejected with the reason, visibility, update, delete). |
| Share a map | `src/OnlineMapsScreen.cpp` (`MapShareScreen`) | Title, description and visibility (Unlisted by default), then the upload and the server's validation and preview. |

**Search state.** `Online::QuickMatch` (`src/online/QuickMatch.h`) holds the one
search: it probes the relays (`RelayProbe`), sends `queue.join`, follows
`queue.status`, answers `queue.proposal` with `queue.respond`, toggles backfill
with `queue.update` and leaves with `queue.leave`. It lives in the online
services and is pumped by `Online::pump()`, so a search continues while the
player opens Profile or Maps. `QuickMatchPresenter` shows the match-found
prompt over whichever screen is in front and flashes the window
(`SDL_FlashWindow`) when a match is found. There is no phone notification: the
platform layer has no local notifications for a backgrounded app.

**Hand-offs** (`src/online/OnlineHandoff.h`) connect screen groups built
separately. The connecting flow registers `Online::setMatchHandler`; the quick
match calls `Online::beginMatch(assignment)` with the `match.start`
`MatchAssignment` once the prompt closes. "Use in a room" downloads the map
into the map cache and calls `Online::useMapInRoom({mapId, hash, title, ...})`,
which the room screen registers with `setRoomMapHandler`. The results screen
offers a rematch through `Online::requestRematch`, registered by the room
screen: an unrated room with the same players. A hand-off made before its
handler exists is kept for it.

**Data.** The profile reads `GET /api/v1/players/{id}` (`PlayerProfile`: the
ratings with rank, the rating history for the trend lines and the aggregates)
and `GET /api/v1/players/{id}/matches` (a `MatchList`) for the list;
`summarizeProfile` falls back to the match list for anything the profile
leaves out. Replays come from `GET /api/v1/matches/{id}/artifacts/replay` and
open in the replay viewer; deep links go to `<origin>/players/<id>`,
`<origin>/matches/<id>` and `<origin>/maps/<id>`. Map previews are the server's
PNGs (`MapVersionInfo.previewUrl`), fetched with `PlatformClient::restRaw` and
decoded with SDL_image. Uploads send the map's uncompressed bytes to
`POST /api/v1/maps/{id}/versions?simVersion=<key>` and poll the version until
`validation` is `valid` or `invalid` and the preview is no longer pending
(`Online::MapShare`, `src/online/MapCatalog.h`).

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

A settings screen for the instance and linked accounts is part of the hub
mock-ups (plan section D); until then the instance is changed by editing the
file or following a link.

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

- `glob2-unit-tests` suites `QuickMatch`, `MapCatalog` and `OnlineResources`
  (`test/online/`): relay probes before `queue.join`, search progress, ranked
  prompts with live answers, AI backfill and its start countdown, requeues,
  declines with a cooldown, cancelling at every step, the share flow, catalog
  queries, hand-offs, and parsing of every protocol fixture the screens read.
- The `UIPresentation` suite and the mobile gallery capture every online
  screen state from canned data (`tools/OnlineScreenFixtures.h`).

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
