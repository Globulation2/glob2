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

`Online::Services` (`OnlineServices.h`) holds the shared `InstanceConfig`,
`PlatformClient`, `MapCache` and quick-match search. `Application` owns one for the
run (`Online::ServicesOwner`, its first member, so the services outlive every screen):
`Online::services()` creates them on first use, and they are destroyed when the game
exits, which closes the connection. Tools and test harnesses that run without an
`Application` get process-lifetime services instead. Online objects take what they
need explicitly: `OnlineMatch` and `PlatformRoom` receive the map cache, and
`QuickMatch` the client. `Services::addHook` returns an id for `removeHook`. The client
is not started until a screen calls `client.start(origin)`.

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
the browser. In the browser `openUrl` and `copyText` run on the page's thread
(`MAIN_THREAD_EM_ASM`): the threaded runtime's application worker has no
`window`, `document` or clipboard. They run synchronously, while the click
still grants transient activation; a browser that refuses the new tab anyway
(Safari counts only the DOM event itself) gets a real link at the bottom of
the page (`glob2OpenUrl` in `browser/shell.html`), which the player taps.
`glob2Diagnostics.snapshot().opened` lists what opened and how
(`browser/tests/online-links.spec.js`). The hub also keeps an "Open page
again" button calling `openSignInPage()`. After a reconnect the client
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
| Quick match | `src/QuickMatchScreen.cpp` | The running search (the hub owns the queue cards): the timer, the opponent rating range, the relay region and round trip, the AI backfill countdown with the AI that would play and **Allow an AI opponent** (on by default), with Profile and Maps over the running search. It closes, back to the hub, when the search ends (the hub shows why) or becomes a match, so the match's results return to the hub; Back to online cancels the search, and the account chip opens sign-in or the account menu on the hub. |
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
`scons/official_instance.py` (currently `https://app.glob2online.com`), overridable
with `scons official_instance=https://example.org`. Every build path passes it to
the client as `GLOB2_OFFICIAL_INSTANCE_ORIGIN`, and the Android and iOS packaging
scripts derive the App Link host and associated domain from it.

When the official instance moves, the origin it leaves goes into `FORMER_ORIGINS`
there (`https://glob2online.com` became the public website when the app moved to
`app.glob2online.com`). The client then reads a stored selection of a former
origin as the official one (`Online::currentOrigin`), treats it as trusted, maps
its invite links (`https://glob2online.com/j/<code>`, which the website also
redirects) and `glob2://join?instance=` links to the official origin, and hides
its record from the server list. The record itself, with its device credential
and refresh token, stays keyed by the origin that issued it and is never copied.
Builds with another `official_instance` carry no former origins.

The browser shell selects its serving origin for a fresh profile, so hosted
`/play/` clients connect to their own platform even when the compiled default
predates a hostname cutover. On the official app host it also changes a saved
apex selection to `https://app.glob2online.com`. Custom instance selections stay
intact, and device credentials and refresh tokens remain keyed to their original
origin; the shell never copies them to the new origin. Initialization waits for
successful storage restoration before writing configuration.

```json
{
  "version": 1,
  "selected": "https://app.glob2online.com",
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
| Online hub | `src/OnlineHubScreen.*` | "Play online" on the main menu. Starts the client (a guest is created on first contact), account chip with browser sign-in and the confirmation code, quick-match cards from `InstanceInfo.queues`, Create room, Join by code, public rooms (`GET /api/v1/rooms`), recent matches (`GET /api/v1/players/{id}/matches`, with `match.updated` summaries seen since merged over them; hidden when empty), Profile & history and Maps (the profile and map-catalog screens), a leaderboard teaser (top five of the first rated queue, `GET /api/v1/leaderboards/{queue}`), offline and update-required banners, and invite links (`takePendingJoin`) with the trust prompt for other instances. |
| Room | `src/RoomScreen.*` over `RoomBackend` | One screen for online rooms (`Online::PlatformRoom`) and LAN rooms (`Lan::LanRoom`): Map / Players & Teams / Game Rules tabs, seats with controller, team and remove, invite (or how to join on the network), chat and ready. Phones get a Seats / Map / Rules / Chat bar and Start or Ready in the thumb corner, mirrored by the thumb-side setting. The host edits map and rules with the custom-game screen in room mode (`CustomGameScreen::useForRoom`); the server generates a random map from the generator descriptor (`src/online/RoomSetup.*`), and a premade or own map is uploaded and played as `{kind: "upload"}`. Members without a seat are listed under the seats, and Ready says why it is unavailable. |
| Starting match | `src/MatchStartScreen.*`, `src/online/OnlineMatch.*` | From `match.start` to the first tick: seat confirmed, map download by hash, engine load, relay connection (`Online::RelayTransport`), waiting for the other players' presence. A relay that refuses the match as new (Reject 5) is reported with `match.reconnect {relayUnavailable: true}` and the new assignment restarts the flow. |
| In-game connection HUD | `src/gui/ConnectionOverlay.*` | Every turn game (online and LAN) shows a permanent panel with each player's state and latency where the "waiting for players" notice was, details on click or tap, one-line notices when a player drops or returns, and centre cards for this client's reconnect (with the grace time and Leave match), catch-up progress (with Leave match, and "can't keep up" once the gap has not shrunk for 15 s) and desync rejoin. The reconnect card also shows when the socket still looks open but nothing has arrived from the relay for 1.5 s, or the horizon has not moved for 1.5 s (`TurnSession::linkStalled()`); after 5 s of silence the session drops the link and reconnects. After a gap or a reconnect the delay estimate starts over: in-flight pings, jitter samples, the buffer target and seat round trips are forgotten, and the backlog's arrival spread is ignored for a second. A player who left stays in the panel as Left; the message list starts below the panel. In every turn game the in-game menu has no Load or Save, and Leave match asks for confirmation, saying what leaving costs. Presentation only: it reads the snapshot `TurnMatchPresenter` (`src/gui/TurnMatchPresenter.*`) builds from the read-only `TurnSession`. Rows show each player's Ping (the relay's round trip to them, `SeatLatency`) or, once they fall a second behind, how far Behind they are (`Presence.lagTicks`); the footer shows your own Delay. Names, units, words and thresholds are in [connection quality](connection-quality.md). |
| Results | `src/EndGameScreen.*` | A turn match this colony wins goes straight here (not to the classic "You have won!" dialog). Online matches add the outcome banner, with the reason (the opponent who left, the prestige goal, the fight; `EndGameScreen::describe`), and the rating card, which `match.updated` updates live and `GET /api/v1/matches/{id}` re-reads every 10 s while it is open. The card says where the result is: waiting for the other players to leave (the match still runs on the relay), recording or verifying (it ended; the verifier replays it), then verified, unverifiable, unrated room match or draw; after 45 s of waiting or 60 s of verifying it says that it is taking longer and that the result will appear in the history. Whoever left sees Defeat ("You left the match. It counts as a loss.") at once with "Final result after the match ends" instead of waiting for the others. A link opens `<origin>/matches/<id>`. Room matches return to the room; quick matches offer **Rematch** (`Online::requestRematch` → `match.rematch`, an unrated room with the same players; it reads "Join X's rematch" after `match.rematchOffered`). |
| Settings › Online | `src/SettingsScreenOnline.cpp` | See above. |

Quick-match cards start the shared search (`Online::quickMatch()`) and open
`QuickMatchScreen`; a handler registered with `OnlineHubScreen::setQuickMatch`
replaces that. The hub registers `Online::setMatchHandler` (an assigned quick
match opens the starting screen) and `Online::setRematchHandler`, and attaches
`QuickMatchPresenter` so the match-found prompt appears over any screen. The
Room screen registers `Online::setRoomMapHandler` for the map browser's "Use in a
room".

`MatchAssignment` carries `mapTitle` and, for rated queue matches,
`ratingPreview {ladder, before, ifWon, ifLost, provisional}` for the greyed
"1528 → 1543?" before verification.

**Pausing.** Rooms and LAN games pause freely; queue matches carry a
[pause limit](turn-protocol.md#pause-limit) that the turn session enforces.
`GameGUI::pauseState` shows it: the menu (the touch sheet and, in network
matches, the desktop menu too) offers "Pause game (N left)", or a disabled "No
pauses left"; a pause the player has none left of is not sent. The Paused label
names who paused and, under a limit, when the game resumes by itself, and sits
above the phone HUD's action bar on a dark backing.

Leaving a match sends `Quit` to the relay; the connection stays open after the
game's session is gone until it is written (at most 3 s, and the shutdown screen
waits for it), so closing the window does not leave the seat in reconnect
grace.

A client that stops reading for a while (a backgrounded phone app, a long
reload) is not disconnected. The native WebSocket keeps at most 4096 unread
messages or 1 MiB. When that limit is reached it stops reading, and TCP flow
control holds the rest at the relay until the game drains the queue. The
browser cannot pause a WebSocket, so it queues up to 16384 messages or 4 MiB,
about eleven minutes of bundles at 25 per second.

**End-to-end check.** `OnlinePlayHarness` (`scons release=1 server=0
online-play-test`) drives the real screens against a live instance. In a room
run, the host signs in as a guest, creates a room with a one-minute sudden-death
timer and writes its code; the guest joins by the code, takes a seat and readies;
the host presses Start; both play. The guest leaves after `GLOB2_E2E_GUEST_LEAVE`
seconds (default 40; 0 stays to the end) by closing its window, or by the in-game
Quit with `GLOB2_E2E_LEAVE_BY=menu`. The host logs the other seat's presence as its
connection panel shows it, the game's end, and every change of the results card,
and waits until the platform has settled the result before returning to the room.
The `quick` role plays a casual quick match (AI backfill) and leaves after
`GLOB2_E2E_QUICK_LEAVE` seconds. Every stage is captured:

```sh
build/darwin/client/release/src/OnlinePlayHarness host https://app.glob2online.com artifacts/e2e &
sleep 20
build/darwin/client/release/src/OnlinePlayHarness guest https://app.glob2online.com artifacts/e2e
build/darwin/client/release/src/OnlinePlayHarness quick https://app.glob2online.com artifacts/e2e-quick
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

"Play this map" on a web map page opens `/play/?map=<mapId>&version=<sha256>&title=<title>`;
the browser shell passes `--room-map <mapId> <sha256> <title>`, which keeps the catalog
map (`Online::useMapInRoom`) for the next online room the player hosts. The game opens
Online and says so; the room screen takes the kept map once it is the host
(`Online::takePendingRoomMap`).

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
instances use `glob2://`. The Amazon and China editions have no online play and
declare none of these (`mobile/android.py` strips the invite intent filters,
`mobile/ios.py` the associated domain and URL scheme). They need the instance to serve
`/.well-known/assetlinks.json` (the release signing certificate's SHA-256) and
`/.well-known/apple-app-site-association` (team id plus
`org.globulation2.glob2`, path `/j/*`). For the official instance that is
`app.glob2online.com`; the public website at the apex serves neither file and
redirects `/j/*` to the app, so an apex invite opens in the browser first. What the
maintainer supplies for these files is in
[hosting: mobile app links](../hosting/README.md#mobile-app-links).

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
  screen state from canned data (`test/OnlineUIFixtures.h`).

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
