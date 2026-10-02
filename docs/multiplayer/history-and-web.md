# Match history, leaderboards and the web app

The platform keeps what the verifier and rating updater record (plan section G):
matches, participants, rating history, per-team statistics and 512-tick
timelines, and the replay of every verified match. This page describes the
read-only REST API over that history and the web app built on it (`platform/apps/web`).
The writers are described in [ratings and matchmaking](ratings-and-matchmaking.md)
and [architecture](architecture.md#engine-agents).

## REST

All shapes are in `platform/packages/protocol/src/history.ts` (with JSON Schema
and fixtures under `fixtures/`). Lists are newest first and page with an opaque
`cursor`; `limit` is optional.

| Endpoint | Returns |
| --- | --- |
| `GET /api/v1/leaderboards/{queueId}?provisional=include\|exclude` | `LeaderboardPage`: registered, active accounts ranked by ordinal; provisional ratings flagged, or left out with `exclude` |
| `GET /api/v1/leaderboards/{queueId}/ai` | `AiLeaderboard`: AI entities grouped by sim version, newest first; `current` marks versions an engine agent serves |
| `GET /api/v1/players/{accountId}` | `PlayerProfile`: ratings (with rank), rating history (oldest first), ten recent matches, aggregates |
| `GET /api/v1/players/{accountId}/matches?queue=` | `MatchList`; `queue` is a queue id or `room` |
| `GET /api/v1/matches?queue=` | `MatchList` of recent public matches: ended quick-match games and matches of public rooms |
| `GET /api/v1/matches/{id}` | `MatchDetail`: summary, setup, team statistics and timelines, artifacts, map, verification detail, economy curves, connection quality per human player |
| `GET /api/v1/matches/{id}/artifacts/{replay\|result\|record}` | The file, as an attachment |
| `GET /api/v1/admin/matches?q=&status=` | Moderators: `MatchList` of any status; `q` is a match, room or account id, part of a player name, or a relay id |

Aggregates come from the views of migration 0004 and cover verified matches of
the last 90 days: win rates by queue, map (catalog title when the map is public
or unlisted) and generator, the player's median and mean game length, and the
units/buildings/prestige curve of their latest verified match next to their own
average at each tick. The match page has the same curve for every human player.

Verification detail is read from the match's succeeded `verify-match` job:
diverged seats, the unverifiable reason, `orderRejections` (seats whose orders
the engine refused, with per-reason counts) and the rating note.

`MatchDetail.network` condenses each human player's entry of the relay's network
summary (`match_participants.network`; `apps/api/src/history/network.ts`): round
trip to the relay and how far the player's game ran behind the match clock (median
and 95th percentile, in ms), disconnects, time offline inside the grace period,
orders sequenced and deferred, rejoins after a checksum disagreement, and how the
player left. `quality` is a rough label for the page only: `poor` when the round-trip
p95 is at least 400 ms, the lag p95 at least 2 s, there were 3 or more disconnects,
30 s or more offline, or a rejoin; otherwise `fair` when the round-trip p95 is at least
200 ms, the lag p95 at least 1 s, there was a disconnect, or more than 5% of orders
were deferred; otherwise `good`. It is public with the match, and absent for matches
whose relay sent no summary.

### Visibility

- Leaderboards show registered, active accounts only. Guests are never ranked;
  banned accounts drop off. AI entities are listed separately and never combined
  across sim versions.
- Profiles of deleted accounts do not exist. Banned accounts answer 404 except to
  moderators, who also see the account status. Guests get a minimal profile
  (`detail: "minimal"`): their account and matches, no ratings or aggregates.
- Any match is reachable by id, so match links can be shared. The public list
  leaves out link-only rooms.
- Replays and verifier results are public, with `Access-Control-Allow-Origin: *`
  so a separately hosted browser client can fetch them. The raw match record is
  for the match's players and moderators.

## Web app

`platform/apps/web` is a React single-page app served by Caddy for every path
that is not routed to the API or the browser client. It uses the paper theme of
the game's menus (`libgag/include/ui/Theme.h`) and the game's font (a Latin subset
of `data/fonts/sans.ttf`), and lays out for phone width.

| Route | Page |
| --- | --- |
| `/` | Instance, play in browser and download links, leaderboard teasers, recent matches |
| `/leaderboard`, `/leaderboard/{queueId}` | Ladder of a rated queue, then its AI ladder |
| `/players/{accountId}` | Ratings, rating graph, aggregates, economy curves, match history |
| `/matches`, `/matches/{id}` | Recent matches; match page with players, rating changes, connection quality, timelines, verification, replay |
| `/maps`, `/maps/mine`, `/maps/new`, `/maps/{id}` | Map catalog, my maps, upload, map page (preview, versions, like, report, owner edits) |
| `/admin/accounts`, `/admin/matches`, `/admin/reports` | Moderation: account search, rename, mute and (administrators) ban; match lookup; map report queue with hide and unhide |

The game links to `/players/<id>`, `/matches/<id>`, `/maps/<id>` and
`/leaderboard/<queueId>`; keep these routes stable. Invite pages (`/j/<code>`)
and sign-in (`/signin`) stay server-rendered by the API. The web app uses the
web session cookie that `/signin` sets; its writes are same-origin requests,
which pass the API's cross-site check.

`VITE_DOWNLOAD_URL` at build time sets the home page's download link
(default `https://globulation2.org/`), and `VITE_WEBSITE_URL` adds a footer link
to a separately hosted public website (none by default). The Compose deployment
passes them from `GLOB2_DOWNLOAD_URL` and `GLOB2_WEBSITE_URL`.

### Watch in browser

The match page links "Watch in browser" to `/play/?replay=<replay URL>`. The
browser shell (`browser/shell.html`) downloads that replay while the game loads,
writes it to the in-memory `/tmp` (never to saved storage), and starts the game
with `-replay <file>`, the existing replay viewer. It accepts http(s) URLs only,
sends credentials only to its own origin, and caps the file at 64 MiB. When the
download fails, the game starts at the main menu. `glob2Diagnostics.snapshot()`
reports `watchReplay` (`none`, `downloading`, `ready` or `failed`).

## Tests

```sh
cd platform
npx vitest run apps/api/test/history.test.ts   # REST against Postgres
npx vitest run apps/web                        # page components (jsdom)
npm run build -w @glob2/web && npm run e2e -w @glob2/web
```

The browser smoke test starts `apps/web/e2e/server.ts`: a fresh test database
seeded by `apps/api/test/historySeed.ts`, the API, and a front server that routes
like the Caddyfile. When a browser game build exists
(`build/emscripten/client/release`, or `GLOB2_WEB_CLIENT_DIR`) it also plays the
seeded replay (`browser/tests/fixtures/cross-replay.replay`) through Watch in
browser. `SCREENSHOT_DIR=<dir>` saves desktop and phone screenshots of each page.
