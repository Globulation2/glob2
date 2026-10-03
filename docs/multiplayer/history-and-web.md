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
| `GET /api/v1/stats` | `InstanceStats` for the home page: players online (seen in the last 15 minutes, in an open room, or in a match that has not ended), live matches, matches of the last 24 hours. Counts only; each API replica caches them for 30 seconds |

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
player left. `quality` is a rough label for the page only: the worst of the median
round trip rated as Ping and the median lag rated as Behind on the shared
[connection-quality](connection-quality.md) table (fair from 150 ms and 1 s, poor from
300 ms and 2 s), `poor` for 3 or more disconnects, 30 s or more offline or a rejoin,
and `fair` for a disconnect or more than 5% of orders deferred. The page writes each
Ping and Behind with its unit and word ("84 ms · Good", 95th percentile underneath)
and states the limits in its legend. It is public with the match, and absent for matches
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
that is not routed to the API or the browser client, and lays out for phone
width (320 px up).

### Look

The site is made from the game's own world, the way its menus are: pages sit on
the menu colony (`data/gfx/menu-colony.png`) under a paper panel, and use the
game's sprites (globs, buildings, flags, resources), wordmark, icon and team
colours. The home page's colony has globs walking and flying across it, using
the game's walk and flight cycles.

- **Tokens** live in `src/styles/tokens.css`: colours, type, spacing, radii and
  shadows for two themes. *Meadow* (light) is the menu paper theme
  (`libgag/include/ui/Theme.h`) with the wordmark's green and gold; *Night
  colony* (dark) is the in-match touch theme's aubergine and gold
  (`src/ui/FrontendUI.cpp`). The theme follows the system unless the viewer
  picks one with the header toggle (stored in that browser only; `public/theme.js`
  applies it before the first paint).
- **Type**: Glob2 Sans (the game font, a Latin subset of `data/fonts/sans.ttf`)
  for headings and numbers, Nunito (SIL OFL, self-hosted) for body text.
- **Team colours** are the engine's (`src/team/Team.cpp`: hue `team × 360 / teams`,
  HSV 0.8/0.9), computed in `src/colors.ts`. Swatches show the exact colour;
  chart lines use the same hue adjusted to keep 3:1 contrast in either theme.
- **Art** in `src/art/` is generated from `data/gfx` and `data/highres` by
  `apps/web/art/build_art.py`, which also writes the server-rendered pages'
  images and fonts to `apps/api/src/web/static/`. See `apps/web/art/README.md`
  for sources, licences and contrast ratios.
- **Motion** is decoration only: the colony can be paused, pauses off screen,
  and stands still under `prefers-reduced-motion`.

The sign-in and invite pages (`apps/api/src/web/pages.ts`) share the tokens and
the colony backdrop; their assets are served from `/signin/assets/`. Invite
pages carry an OpenGraph image for link previews.

| Route | Page |
| --- | --- |
| `/` | Colony hero with play in browser, download and join-with-code, live stats, how the game plays, leaderboard teasers, most liked maps, recent matches, instance |
| `/leaderboard`, `/leaderboard/{queueId}` | Ladder of a rated queue, then its AI ladder |
| `/players/{accountId}` | Ratings, rating graph, aggregates, economy curves, match history |
| `/matches`, `/matches/{id}` | Recent matches; match page with players, rating changes, connection quality, timelines, verification, replay |
| `/maps`, `/maps/mine`, `/maps/new`, `/maps/{id}` | Map catalog, my maps, upload, map page (preview, versions, like, report, owner edits) |
| `/account` | The signed-in account: sign-in methods, delete |
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

The first script holds the home, leaderboard and match-list pages; the player,
match, map, account and moderation pages and the protocol schemas (used to check
the instance description) load on demand.

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
seeded by `apps/api/test/historySeed.ts` plus catalog maps with real engine
previews and an open room (`apps/web/e2e/showcase.ts`), the API with local
accounts enabled, and a front server that routes like the Caddyfile. When a
browser game build exists (`build/emscripten/client/release`, or
`GLOB2_WEB_CLIENT_DIR`) it also plays the seeded replay
(`browser/tests/fixtures/cross-replay.replay`) through Watch in browser.

Every page, including the sign-in and invite pages, is checked with axe in both
themes on desktop and phone and must have no violations: `a11y.spec.ts` runs
axe's default rules, `smoke.spec.ts` the WCAG 2.2 A/AA and best-practice tags
after each page's interactions. The phone run also checks that no page scrolls
sideways at 320 and 430 px and that buttons, nav links and form fields are at
least 44 px. Other checks: keyboard skip link and focus after navigation, the
theme toggle, and the colony animation (pause, reduced motion).
`SCREENSHOT_DIR=<dir>` saves desktop and phone screenshots of each page in both
themes; `AXE_REPORT=<file>` writes the axe results as JSON lines.
