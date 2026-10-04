# Rooms and matches

This guide describes how players get from a room or a quick-match queue into a
match on a relay, and how the match comes back to the platform when it ends. It
covers the realtime room methods, map sources and uploads, the start sequence,
match tickets, relay placement, the `/internal` API that relays call, and invite
links. The [architecture guide](architecture.md) explains the parts these pieces
run in. The relay itself is documented in `docs/multiplayer/relay.md`, which the
relay work adds. Queues and ratings are covered in
[ratings and matchmaking](ratings-and-matchmaking.md).

Code: `platform/apps/api/src/play/` (rooms, tickets, realtime delivery),
`platform/apps/api/src/routes/{play,internal,invite}.ts`, and
`platform/packages/play/src/play/` (start sequence, relay placement, map sources,
match-end intake), which both the API and the worker use. Data: migration
`0005_rooms_matches.sql`.

## Rooms

A room has a host, members, seats, a map, teams with alliances, rules,
experiments and chat. It lives in Postgres. Every change bumps the room's
`revision` and publishes `{t: "room", roomId}` on the `realtime` NOTIFY channel.
Each API replica then re-reads the room and sends `room.state` to the members whose
sockets it holds. Clients ignore states older than the last revision they saw.

**Seats.** Seat *i* always plays map team *i*. `teams[i].alliance` groups teams into
sides. The number of seats equals the map's team count, and changing the map
resizes both. A seat is open, human, AI, or locked. A locked seat is empty, and
nobody may take it. An empty or locked seat's team is closed when the match starts,
exactly like a "Closed" colony in a custom game: the MatchSetup lists it as a `closed`
seat, the engine removes its colony, and it has lost from the start. The taken seats
become the match's players, numbered `0..p-1` in room seat order on their own map
teams, so a player's match seat (ticket `seat`, `match_participants.seat`) differs from
its room seat when an empty seat comes before it. See
[MatchSetup to GameHeader](turn-protocol.md#matchsetup-to-gameheader).

**Sim versions.** A room carries its host's sim version. A client whose sim version
differs gets `update_required` from `room.join`, and a client whose version the
instance does not serve gets it from `room.create` and `room.join`. Joining an
account to a room makes it leave any other room it is in.

**Permissions.** These defaults came from the mock-ups and are provisional. They
are collected in `ROOM_RULES` in `platform/apps/api/src/play/rooms.ts`, so they
are easy to change:

| Rule | Default |
| --- | --- |
| Members move themselves into open, unlocked seats (`setSeat` `self`), and leave their own seat (`open`) | yes |
| A new member (`room.join` by code or invite) takes the first open, unlocked seat; in a full room they stay an unseated member (`joinTakesOpenSeat`) | yes |
| Adding AIs, locking seats, emptying other seats, changing settings and starting | host only |
| Changing the map, teams, rules or experiments clears every Ready | yes |
| The host is ready by starting; there is no force start past unready players | yes |
| Seats that must be taken to start | 2 |
| The host leaving closes the room (`room.closed` `host_closed`) | yes |
| The host can remove a member from an open room (`room.kick`) | yes |
| A kicked player cannot rejoin that room for | 10 min |
| Open room closes when its host has been disconnected for | 120 s |
| Disconnected members are removed from open rooms after | 10 min |
| Open rooms close after no change for | 12 h |
| Members per room | 24 |

`room.update` takes the revision the host last saw and answers `conflict` when the
room has changed since then. Room settings are frozen (`conflict`) while the room is
starting or in a match. When the match ends, or is cancelled because nobody reached
the relay, the room reopens with every Ready cleared.

**Kicking.** `room.kick {roomId, accountId}` is host only and works while the room is
open (not while a match is starting or running). The member's seat opens, they get
`room.closed` with reason `kicked`, and the other members get the new `room.state`.
The ban is kept in `room_kicks`: `room.join` answers `forbidden` with
`details.until` until it runs out, and the room sweep deletes expired bans. A ban
is per room, so the player can still join other rooms by the same host.

**Presence.** A member is connected while any API replica holds a socket for the
account. When a replica's last socket for an account closes, it marks the account
disconnected in its rooms. A replica that still holds a socket for a member listed
as disconnected marks it connected again when it next delivers that room.

**Chat.** `room.chat` reaches every member through the same fan-out. A muted account
(`accounts.muted_until`, set by moderators) gets `forbidden`. The mute is read when
the message is sent, so it applies without signing in again. Each account may send 8
messages per 10 seconds on each replica.

**Invite codes.** Codes are 10 characters from a 32-symbol alphabet without `0`,
`O`, `1` or `I`, which gives 50 bits. Lookups ignore case. Each replica allows an
account and an address 10 failed `room.join` lookups per 10 minutes, and
`GET /api/v1/invites/{code}` and `/j/{code}` are limited to 60 per minute per address.
A code expires when its room closes.

**Public list.** `GET /api/v1/rooms?simVersion=<key>` lists open public rooms of a sim
version, most recently changed first, with a `cursor` for the next page. Each
`RoomSummary` names its map: a generated map by its generator, a catalog map by its
catalog title with `mapPreviewUrl` (the version's server preview) once that preview is
ready.

## Map sources

The `hash` in a `MatchSetup` map source is always the SHA-256 of the bytes clients
load. Every participant downloads those bytes from
`GET /api/v1/blobs/maps/{hash}`.

| Source | How the room gets its hash and team count |
| --- | --- |
| Catalog `{kind: "catalog", hash, mapId?}` | A valid `map_versions` row of a map that is not hidden and is public, unlisted or the host's own, saved by an engine no newer than the room's (`minVersionMinor`). See [Map catalog](architecture.md#map-catalog) |
| Upload `{kind: "upload", format, hash}` | The host's own upload for the room's sim version, once validated. The client uploads a premade or own map chosen in the room's map editor this way (`PlatformRoom::usePremadeMap`) |
| Generator `{kind: "generated", generator}` | A `generate-map` job. `params.teams` is required and sets the team count. The hash is filled in when the job finishes |

`RoomState.mapTitle` names a catalog or uploaded map: the catalog title, or the
title the engine read from the uploaded file, else its file name without the
extension. Generated maps have none; clients name them from the generator.

**Generated maps.** Generation is deterministic for a sim version, so
`generated_maps` keeps one row per descriptor (SHA-256 of the canonical descriptor
JSON) and sim version. Rooms and quick-match starts that ask for the same descriptor
share one job. While a room waits, it shows `mapStatus: "pending"`. The worker applies
the agent's result, records the map blob as public, and NOTIFYs `map_jobs`. The API
replicas then re-check rooms with a pending map, and the room moves to
`mapStatus: "ready"` with the hash, or to `"failed"` with `mapProblem`. A failed
generation may be requested again after a minute.

**Uploads.** `POST /api/v1/uploads?format=map|save&simVersion=<key>&fileName=…`
takes the raw file as `application/octet-stream`, up to `UPLOAD_MAX_BYTES` (16 MiB by
default). Without `simVersion` (the web app's upload form), the newest version the
instance serves is used. Each replica allows 30 uploads per account per hour. The
bytes become a private blob, and an engine agent of that sim version validates them
with a `validate-map` job. `GET /api/v1/uploads/{id}` (owner only) returns the
`MapUpload` resource with its status, map facts, and, for a save, the players
recorded in it.

- Plain and gzip-compressed files (the game's `.map.gz`) are both accepted. The API
  unpacks gzip (up to 64 MiB unpacked) before storing, so the blob, its hash and the
  map hash are those of the bytes the engine loads.
- Before anything is stored, the API reads the map header
  (`checkMapFile` in `@glob2/core`). A file that is empty, not a map, from a newer or
  a no longer loadable format, a save sent as a map (or the reverse), or too large is
  answered at once with `400` (`413` for an oversized body), a message meant for
  players, and `details.problem` (`empty`, `not_a_map`, `newer_version`,
  `older_version`, `save_not_map`, `map_not_save`, `too_large`, `corrupt_gzip`).
- Reasons from the engine agent (`MapUpload.reason`, `MapVersionInfo.reason`) are
  also written for players ("The game couldn't load this map; …").
- Uploading the same bytes again returns the existing upload.
- Bytes that someone has already validated for that sim version reuse the
  result, without a new job.
- In a room, a save's `reteaming` list (name, team, account) is checked against the
  team count. Returning players take the seat of the team they played, because seat
  *i* is team *i*.

**Downloads.** Public blobs, such as generated maps, and versions of public or
unlisted catalog maps need no sign-in. A private upload or private catalog map is
served to an account that uploaded those bytes (and, for catalog maps, to
moderators), a member of a room that uses them, or a participant of a match played
on them. Every other
caller gets `404`. Responses carry `ETag: "<hash>"` and an immutable cache lifetime.

**Warm maps.** Queue starts first take a pre-generated map of the queue, sim
version and pool entry from the warm map pool (`takeWarmMap` in
`packages/play/src/warmMaps.ts`, wired in the worker's `main.ts`; see
[Warm map pool](architecture.md#warm-map-pool)). Warm maps are generated with one
team per queue seat, as on-demand maps are. When the pool is empty or turned off
(`WARM_MAPS_PER_ENTRY=0`), the starter generates on demand and waits up to 60 s.

## Start sequence

Rooms (`room.start`, in the API) and queues (`PlatformMatchStarter`, in the worker)
share one sequence, `createMatch()` in `platform/packages/play/src/play/start.ts`:

1. **AccessPolicy.** For rooms, the host is checked with `canHost`, and every
   other seated human with `canJoin`, again at start. For queues, every human is
   checked with `canQueue`. A denial is `access_denied`, with the reason and any
   `requiredEntitlement` in `details`.
2. **Map.** The room's map must be ready. A queue start takes a warm map or
   generates one.
3. **Placement.** The match goes on a relay, as described in
   [relay placement](#relay-placement). Without an available relay the start fails
   with `unavailable` and nothing is recorded. A room returns to `open`, and the
   matchmaker retries and then requeues.
4. **Record.** The `matches` row stores the exact `MatchSetup`, a seed the platform
   chose, the map hash, the relay and the status `starting`. One
   `match_participants` row is written per seat.
   - Room seeds are random.
   - Queue seeds, and generator seeds when no warm map is used, derive from the
     proposal id. A retried start therefore reuses the same generated map.
5. **Tickets and `match.start`.** The start publishes `{t: "matchStart", matchId}`.
   Each replica signs a ticket for every seated human whose socket it holds and
   sends `match.start` (`MatchAssignment`).
   - For a queue match, the worker's `queue.matchFound` event triggers this
     instead: the replica forwards `queue.matchFound` and follows it with
     `match.start`.
   - `match.reconnect` signs a new ticket for a seat in a `starting` or `running`
     match.
   - A client that missed `match.start` fetches its assignment with
     `match.reconnect`.

`PlatformMatchStarter` is idempotent per proposal id. It returns the existing
match for a proposal, and a unique index on `matches.proposal_id` settles
concurrent starts.

A match is `starting` until its relay lists it in a heartbeat's `activeMatchIds`,
which makes it `running`. A match still `starting` after 10 minutes is cancelled by
the worker's scheduler, and its room reopens.

A room start is several transactions (`starting`, then the match, then
`in_match`). If the API process dies between them, the room sweep
(`RoomService.recoverStarting`, every 30 s on each replica) finds rooms still
`starting` after two minutes (`rooms.starting_since`): a room whose match was
created since it started is resumed (`in_match`, and its players get
`match.start` again; clients ignore a repeat), any other reopens with
`RoomState.notice` telling the members to start again. A start that fails for
any other reason than access or readiness also reopens the room with a notice;
the next start clears it.

## Match tickets

Tickets are EdDSA JWTs signed with the same key as access tokens and published in
the same JWKS (`/.well-known/jwks.json`). To rotate keys, publish the new key first.
Keep the old one published until every ticket it signed has expired, at least
`TICKET_SECONDS`.

| Field | Value |
| --- | --- |
| header | `alg: EdDSA`, `typ: glob2-match+jwt`, `kid` of the signing key |
| `iss` | `PUBLIC_ORIGIN` |
| `aud` | `glob2-relay` |
| `sub`, `accountId` | The player's account id |
| `jti` | Random UUID |
| `iat`, `exp` | Issue time and expiry, 15 minutes later (`TICKET_SECONDS`) |
| `matchId` | The match |
| `seat` | The player's `MatchSetup` seat number |
| `humanSeats` | Every human seat of the match, in order (the same in all of its tickets) |
| `simVersion` | The setup's sim version object |
| `relayUrl` | The relay's registered `publicUrl`, verbatim |
| `entitlements` | The account's active entitlement keys (empty today; relays ignore them) |

`MatchAssignment.mapUrl` is `<origin>/api/v1/blobs/maps/<hash>`.

## Relay placement

A relay is available when it is not draining, it heartbeated within the last 45 s
(three intervals), and its load is below `capacity.maxMatches`. Its load is the
larger of the matches it reports and the `starting` or `running` matches placed on
it.

There may be very few relays, so a match is placed even when no relay is near its
players. Region is a preference only:

- With round-trip probes, the platform takes the relay whose region has the
  smallest worst round trip among the players who measured it. Relays in regions
  nobody measured rank after every measured one.
- Otherwise it prefers the queue proposal's region.
- Ties go to the lowest load fraction, then to the relay id.

Players send probes in `queue.join` `regions`, and in `room.create` and `room.join`
`regions` for rooms.

**Refusals.** A draining or full relay refuses a new match with turn-protocol
`Reject(5)`. The client then calls `match.reconnect` with `relayUnavailable: true`.
While no relay has reported the match running, the platform moves it to another
available relay, excluding the one that refused, and sends every player a new
`match.start`. A match can be moved at most five times. Once a relay runs the
match, the flag only re-issues a ticket for that relay.

## Internal API for relays

Relays authenticate every call with `Authorization: Bearer <relay key>`. The
operator configures the keys on the platform in `RELAY_KEYS` (comma-separated) or
`RELAY_KEYS_FILE` (one entry per line, `#` comments), and on each relay in
`GLOB2_RELAY_KEY`.

- An entry `<key>` lets the relay act as any relay id.
- An entry `<relayId>:<key>` pins the key to one relay id. A pinned key is refused
  (`403`) for another relay id and for matches placed on another relay.
- Keys must be at least 32 characters.
- Without any key, relays cannot register, so matches cannot start.

Keys are compared in constant time.

| Call | Body → response | Notes |
| --- | --- | --- |
| `POST /internal/v1/relays/register` | `RelayRegistration` → `RelayRegistrationResponse` (`heartbeatIntervalSeconds: 15`, `jwksUrl`) | Upsert. Re-registering resets load and drain state |
| `POST /internal/v1/relays/heartbeat` | `RelayHeartbeat` → `{ok: true}`, or `404` `{ok: false, reregister: true}` for an unknown relay | Updates load and draining. Listed `starting` matches become `running`, and every listed match records `relay_seen_at` |
| `GET /internal/v1/matches/{id}/setup` | → the stored `MatchSetup` | Semantically identical to what clients received. Key order may differ |
| `PUT /internal/v1/matches/{id}/record` | G2MR bytes (`application/vnd.glob2.match-record`, up to `RECORD_MAX_BYTES`, 64 MiB) → `RelayRecordReceipt` | Stored as the match's `record` artifact. Repeatable. After the end report, only the reported bytes are accepted (`409` otherwise) |
| `POST /internal/v1/matches/{id}/end` | `RelayMatchEnded` → `RelayMatchEndedResponse` | `409` before the record upload, or when `record.sha256` or `simVersion` differ. A repeat answers `{ok: true, duplicate: true}` and changes nothing |

**Lost relays.** The worker's scheduler checks every 30 s for `running` matches
that their relay has not listed in a heartbeat for 180 s
(`LOST_MATCH_GRACE_SECONDS`, `abortMatchesOnLostRelays` in
`packages/play/src/play/intake.ts`). This covers a relay that died and one that
restarted and forgot its matches. Such a match ends with end reason `aborted`,
verification `not_applicable` and rating status `not_rated`, so it changes no
rating. Its room reopens, and its players get `match.updated` with `endReason:
"aborted"`. If the relay was alive after all and its end report arrives later,
the report replaces the abort, and verification and ratings proceed as usual.

**Match-end intake.** The first end report does the following:

- It sets the match's status (`ended`), end reason, final tick, desync flag and end
  time, and keeps the report as `end_report`.
- For each seat it sets the disconnect count and quit tick, and stores the seat's
  entry of `network` (the relay's `RelayNetworkSummary`) in
  `match_participants.network` (migration 0009). A `network` that does not
  validate is dropped with a warning; it never makes the report fail.
- When the reason is `abandoned`, every seat that quit before the report's final
  tick gets the outcome `abandoned`; a seat that left at the end did not abandon.
  The verdict refines this (`recordVerification`): seats of a verified winning
  (or drawing) team keep `won` (`draw`), and a seat that quit at or after the
  verified final tick takes its team's outcome. Turn protocol 2 reports a game
  whose loser dropped at the end as `abandoned` once the loser's reconnect grace
  runs out, after the winner has already left the finished game.
- It reopens the room and NOTIFYs `match_updates`, so participants get
  `match.updated`.
- It then submits a `verify-match` job with the setup and the record's hash. A
  repeated report re-submits the job only if none exists, for example when
  enqueueing failed the first time.

## Queues over realtime

The API handles `queue.join`, `queue.leave` and `queue.respond`.

- `queue.join` checks the sim version (`update_required`) and
  `AccessPolicy.canQueue` (`access_denied`) first, then calls `joinQueue` from
  `@glob2/play`.
- Errors map to codes: guest in a rated queue → `forbidden`; already queued →
  `conflict`; decline cooldown → `rate_limited`, with `until` in `details`; unknown
  queue → `not_found`.
- Each replica listens on `queue_events` and forwards each event to the account's
  sockets. It listens on `match_updates` and sends `match.updated` (`MatchSummary`)
  to the match's human participants.

- `queue.update {ticketId, allowAiOpponent}` changes AI backfill on a waiting
  search; after a `queue.respond` the API sends the proposal again to everyone in
  it, so the prompt shows who accepted.

**Searching several queues at once.** Instances list `'queue.multi'` in
`InstanceInfo.features`; a client sends `queueIds` with `queue.join` only then.
One search holds a waiting ticket in `queueId` and in each of `queueIds`, sharing
`queue_tickets.search_id` (migration 0033) and their `created_at`. The result adds
`searchId` and `tickets` (`{queueId, ticketId}`, the `queueId` ticket first).
Every queue must exist and pass `canQueue`, and a guest's search may not include
a rated queue (`forbidden`); an account has one search at a time (`conflict`).

- The matchmaker does not group, prompt or send `queue.status` for a ticket whose
  account has a ticket in a prompt (`status = 'proposed'`), so the search is
  offered one match at a time; the claim of a proposal repeats that check.
- When the prompt falls through for someone else, or the match fails to start,
  the prompted ticket is requeued and the others carry on, all keeping their place.
- When its match starts, or the player declines or times out, the search's other
  waiting tickets are cancelled.
- `queue.leave` and `queue.update` act on the whole search, whichever of its
  tickets they name.
- AI backfill stays per queue: whichever queue fills first ends the search.

## Rematch after a quick match

`match.rematch {matchId, regions?}` turns an ended quick match into an unrated
link room with the same map (the match's map source, as a room map selection),
rules and experiments, as decided for Q9. Only human players of the match may
ask; room matches go back to their room instead (`conflict`), and a match that
has not ended answers `conflict`. The first player to ask hosts the room
(`RoomSettings.rematchOf` records the match) and every other human player gets
`match.rematchOffered {matchId, roomId, code, host}`; anyone asking again, or
answering the offer with `match.rematch` or `room.join {code}`, joins that room.

## Invite links

`https://<instance>/j/<code>` is served by the API as a small server-rendered page,
so link previews get OpenGraph tags (`og:title`, `og:description`, `og:url`,
`og:site_name`) without running scripts.

- **Play in browser** (the primary action) opens the web client with
  `?join=<code>`. The client's URL is `web.browserClientUrl` in `instance.yaml`,
  `<origin>/play/` by default.
- **Open in the Globulation 2 app** links to
  `glob2://join?instance=<origin>&code=<code>`, with a note that it needs the
  installed game. The page never opens it by itself. After a click, a
  nonce-allowed inline script waits about 1.5 s; if the page did not lose focus or
  get hidden (the app did not take over), it shows "The app didn't open" with
  links to the browser client and the instance home.
- On an instance with verified app links (`appLinks` in `instance.yaml`, the
  official domain), phones normally open `/j/` links in the app directly. A phone
  that still shows the page (by user agent: Android, iPhone, iPad, iPod) gets the
  app button first. On Android it is
  `intent://join?…#Intent;scheme=glob2;package=<package>;S.browser_fallback_url=<browser client link>;end`,
  so a missing app lands in the browser client instead of failing silently.
- An unknown or expired code gets a `404` page that says so. It offers the browser
  client without a code and the instance home, and runs no script.

Caddy routes `/j/*` to the API, as it routes `/api` and `/realtime`; `/internal`
is never served publicly, and relays reach it on the backend network (see the
[self-hosting guide](../hosting/README.md)).

## Not done yet

- **Client side.** The room screen and the client side of `glob2://` and `?join=`
  wait for approved mock-ups and the client work.
