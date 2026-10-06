# Online platform architecture

Globulation 2's online play is being rebuilt on a new foundation: a TypeScript
platform service for accounts, rooms, matches, ratings and maps, and a C++ relay
that sequences turns. This guide describes the design, the contracts between the
parts, and how to work on the platform. Identity is covered in
[identity](identity.md), rooms, the match start sequence, tickets, relay placement
and the relays' internal API in [rooms and matches](rooms-and-matches.md), and
quick-match queues and ratings in
[ratings and matchmaking](ratings-and-matchmaking.md). The binary turn protocol
between clients and relays is owned by the turn-netcode work and documented in
`docs/multiplayer/turn-protocol.md`.

The platform replaced the legacy YOG lobby, router and IRC chat, which were
deleted at the cutover milestone (M9). There is no data import from YOG: accounts,
ratings and history start fresh.

## Components

```
 clients (desktop / mobile / browser)
   │ HTTPS + realtime WebSocket (JSON)          │ match WebSocket (binary turns)
   ▼                                            ▼
 platform-api (TypeScript, stateless, N)  ◄──► relay (C++, N, version-agnostic)
   │ Postgres: state, job queue, LISTEN/NOTIFY       │ uploads the match record
   │ blob store: fs volume (S3 optional)             │
   ▼                                                  │
 platform-worker (TypeScript): result intake, scheduler, matchmaker, ratings
 engine-agent (TypeScript + glob2 headless, one image per sim version):
   generate-map · validate-map · render-preview · verify-match · validate-ai
 Caddy: TLS, static web client and web app, /api, /realtime, /relay
```

| Part | Code | Role |
| --- | --- | --- |
| `platform-api` | `platform/apps/api` | Public REST (`/api/v1`), realtime WebSocket (`/realtime`), browser sign-in pages (`/signin`, `/auth/<provider>/…`), JWKS (`/.well-known/jwks.json`), internal endpoints for relays and agents (`/internal`), health (`/healthz`, `/readyz`). Stateless; run any number of replicas. |
| `platform-worker` | `platform/apps/worker` | Applies engine-job results (recording verify-match verdicts and history, applying ratings, completing map jobs); runs the scheduler (maintenance, matchmaker, rating sweep, warm map pool, relay sweep) on the one replica holding the leader lock. The process code only: the domain logic it runs is in `@glob2/play`. |
| `engine-agent` | `platform/apps/engine-agent` | Runs engine jobs for exactly one sim version with its glob2 binary; see [Engine agents](#engine-agents). |
| web app | `platform/apps/web` | Home, leaderboards, player and match pages, map catalog, moderation (React + Vite); see [match history and the web app](history-and-web.md). Sign-in and invite pages are rendered by `platform-api`. |
| relay | `src/relay/` (M2) | Clock and turn sequencing for matches; trusts only signed tickets. |
| contracts | `platform/packages/protocol` | Every JSON shape, exported as JSON Schema with fixtures for C++. |
| data | `platform/packages/db` | SQL migrations, typed Kysely access, pub/sub, leader lock. |
| match domain | `platform/packages/play` | Shared by the API and the worker: ratings, queue tickets and proposals, the match start sequence, relay placement, map sources, match-end intake, catalog job results, the warm map pool and stored-JSON decoding. Test doubles and fixtures are exported as `@glob2/play/testing`. |
| plumbing | `platform/packages/core` | Configuration, logging, AccessPolicy, blob store, job queue, engine-agent liveness, shutdown. |

Apps depend on packages, never on each other: ESLint rejects imports of an app
package (`@glob2/api`, `@glob2/worker`, `@glob2/engine-agent`, `@glob2/web`)
from anywhere else.

## Principles

- **The platform never parses engine binary formats.** A match is described by a
  JSON `MatchSetup`; one C++ function turns it into a `GameHeader` for live
  clients and the verifier alike. Maps, saves, records and replays are opaque
  blobs addressed by SHA-256.
- **The relay trusts only signed match tickets** and never simulates. It knows
  nothing about sim versions beyond checking that all tickets of a match agree.
- **Engine work runs in engine-agent jobs**, partitioned by sim version: map
  generation (deterministic only per platform, so it runs once and every client
  downloads the same bytes), map validation, previews and match verification.
- **Postgres is the only stateful service.** It provides the job queue
  (graphile-worker), cross-replica pub/sub (LISTEN/NOTIFY) and leader election
  (advisory locks). There is no Redis, so a self-hosted instance runs one
  database, a blob volume and the services above.
- **Every action that admits play goes through `AccessPolicy`.** It is the only
  hook a paid feature would need; see below.

## Simulation versions

A sim version is the triple `VERSION_MINOR` + `NET_PROTOCOL_VERSION` (both in
`src/app/Version.h`) + the build's simulation hash (SHA-256 over `SIM_REVISION` from
`src/game/SimRevision.h` and the data files that affect simulation, computed by the
engine and by `deploy/sim_version.py`). Two builds with the same sim version must
produce identical games; every simulation change bumps `SIM_REVISION`
([turn protocol](turn-protocol.md#simulation-version)).

- Protocol schema `SimVersion` is `{versionMinor, netProtocol, dataHash}`; its
  canonical string key is `simVersionKey()`: `<minor>-<net>-<dataHash>`, used in
  database columns (domain `sim_version_key`), engine task identifiers and URLs.
- Rooms, queue tickets, matches and AI rating entities carry a sim version.
  Players are only ever grouped with others of the same version.
- An instance serves the versions it has engine agents for: agents register in
  `engine_agents` and `GET /api/v1/instance` lists every version with an agent
  seen in the last five minutes. A client whose version is not served gets
  `simSupported: false` from `session.hello` (it can still sign in) and
  `update_required` from room, queue and match requests. The hello result also
  lists the served versions, so the client can say whether it or the server
  needs updating rather than always asking the player to update.
- Serving an older version means running an engine-agent image of that version
  alongside the current one. AI rating entities are keyed by (AI id, sim
  version), so AI revisions are never combined.

## Contracts: the protocol package

`platform/packages/protocol` is the single source of truth for every JSON
document that crosses a process boundary. Schemas are TypeBox objects (plain JSON
Schema 2020-12), so TypeScript gets static types and validators, and every other
consumer gets the same schema as a file.

**Strictness.** Documents an untrusted party sends, and documents that must mean
exactly one thing to every engine (`MatchSetup`, requests, job payloads), reject
unknown properties. Documents the platform emits (responses, events, REST
resources) allow unknown properties, so newer servers can add fields without
breaking older clients. Some rules cannot be expressed in JSON Schema; those are
*semantic* checks (`matchSetupProblems()`) and a document is valid only if it
passes both.

| Area | Schemas |
| --- | --- |
| Match description | `SimVersion`, `MatchSetup`, `MapSource`, `GeneratorDescriptor`, `MatchRules` |
| Tickets | `MatchTicketHeader`, `MatchTicketClaims` |
| Relay ↔ platform | `RelayRegistration(Response)`, `RelayHeartbeat(Response)`, `RelayMatchEnded` |
| Realtime | `RealtimeRequest`, `RealtimeResponse`, `RealtimeEvent`, `RealtimeServerMessage`, `MatchAssignment`, and per method `Realtime<Method>Params`/`Result`, per event `RealtimeEvent<Event>` |
| REST | accounts and auth, `InstanceInfo`, rooms, matches, maps, leaderboards, `ErrorBody` |
| Engine jobs | `EngineJob`, `EngineJobResult`, and per kind payload and result |

**Fixtures for other languages.** `platform/packages/protocol/fixtures/` holds:

- `schemas/<Name>.schema.json`: one self-contained JSON Schema per registered name;
- `valid/<Name>/<case>.json` and `invalid/<Name>/<case>.json`: documents every
  implementation must accept or reject; `manifest.json` lists each with its
  schema, expected validity, and for invalid cases whether the schema or the
  semantic stage rejects it;
- `tickets/`: a fixture-only Ed25519 JWKS and signed tickets (valid, tampered,
  wrong algorithm, wrong type, unknown key, expired, wrong audience) with the
  verification time in the manifest.

The fixtures are generated: edit `scripts/fixtureCases.ts` (or a schema) and run
`npm run fixtures` in `platform/`. A test fails when the committed files are out
of date, and another validates every fixture with an independent JSON Schema
validator. C++ contract tests read the manifest and must reach the same verdicts.

### MatchSetup and the GameHeader

`MatchSetup` field names follow the `GameHeader` accessors they set, and every
rule is required, so there are no defaults for two builds to disagree on.

| MatchSetup | GameHeader |
| --- | --- |
| `seed` | `setRandomSeed` |
| `seats[i]` (numbered 0..k-1) | `BasePlayer` *i*: `human` → `P_IP` on every client and in the verifier (as networked games do today, so heavy checksums agree), `ai` → `P_AI + id`; `name`, `team`; `setNumberOfPlayers(k)` |
| `seats[i].aiConfig` | `setAIConfig(i, …)` |
| `teams[t].alliance` (teams listed 0..n-1, n = map team count) | `setAllyTeamNumber(t, alliance + 1)` |
| `rules.prestigeVictory`, `rules.suddenDeathMinutes` | prestige and sudden-death winning conditions (minutes × 60 × 25 ticks) |
| `rules.mapDiscovered`, `rules.allyTeamsFixed` | `setMapDiscovered`, `setAllyTeamsFixed` |
| economy and combat rules | the setter of the same name (`setResourceScarcityLevel`, …) |
| `experiments` | `ExperimentSet` keys; an unknown key is an error, not ignored |
| `map.hash` | the map or save the client loads (SHA-256 of the decompressed bytes) |

Several seats may share a team: a human and an AI on one team is the custom-game
shared control; several humans may also share a team. AI ids are the CLI names
from `src/ai/AINames.cpp`; JavaScript controllers are not accepted online yet.
The game speed choice of the custom-game lobby is not part of a setup: the relay
owns the clock. Game latency and order rate are set by the converter for the new
netcode, not by the setup.

## Realtime messages

Clients hold one WebSocket to `/realtime` carrying JSON frames:
`{"type":"request","id","method","params"}` from the client, and
`{"type":"response","id","ok",…}` or `{"type":"event","event","data"}` from the
server. The first request is `session.hello`. The initial method set covers
sessions, browser sign-in handoff, rooms (create, join, leave, update with
optimistic revision, seats, ready, chat, start), queues and match reconnect;
events cover handoff completion, room state and chat, queue progress and
match start. Room changes fan out to sockets on every API replica through
NOTIFY (only ids and revisions travel; replicas re-read state).

Implemented: the envelope, `session.*` and `auth.handoff.*` (see
[identity](identity.md#realtime-sessions)), and `room.*`, `queue.*` and
`match.reconnect` (see [rooms and matches](rooms-and-matches.md)). Each replica
indexes its sockets by account, sign-in and pending handoff, and listens on one
NOTIFY channel (`realtime`). Anything addressed to a socket is published there and
delivered by the replica holding it (`platform/apps/api/src/realtime/hub.ts`):
`{t: "event", to: {account | family | connection}, …}`, `{t: "handoff", attemptId}`,
and the room and match messages `{t: "room" | "roomChat" | "roomClosed" |
"matchStart", …}`, which carry ids only. Replicas also listen on `queue_events`
and `match_updates` (from the worker's matchmaker and ratings, and from match-end
intake) and on `map_jobs` (finished map generations and upload validations). Malformed frames close
the socket (1007/1008); a request with bad params gets a `bad_request`
response with the schema issues in `details`.

## AccessPolicy

`AccessPolicy` (`platform/packages/core/src/accessPolicy.ts`) has three checks,
`canHost`, `canJoin` and `canQueue`, each returning allowed or a denial with a
reason (mapped to the `access_denied` error code). The only implementation is
`allow-all`, selected in `instance.yaml`. The `entitlements` table records product
grants; match tickets carry an `entitlements` claim that relays ignore. Product
rules that are not about access, such as keeping guests out of rated queues,
belong to the feature that owns them, not to the policy.

## Colony skin ownership

Skin identity (`colony_skins`) is separate from immutable published paint
(`colony_skin_versions`). Each version references two blobs, a colour atlas
(`texture_sha256`) and a material map (`material_sha256`), plus the layout
`colony-v2`, building color and manifest digest. Database triggers reject edits
and deletion of published versions; moderation disables the parent skin. Blob
garbage collection retains both images of every published version and all referenced sprite derivatives. Account deletion removes private drafts
and equipment, replaces owned skin names with “Deleted skin” and disables their
paint while preserving immutable version identifiers for match history. Guest
retention keeps accounts referenced by skin reports so moderation records remain
valid. Publishing, draft saves and reporting use shared database rate limits
across API replicas.

`colony_skin_equipment` records a selected version. The equipment service checks
active registered accounts, unexpired and unrevoked entitlements, and ownership
of custom designs. Match creation must revalidate selection. `match_colony_skins`
reserves frozen per-team appearances and public signed assertions, separate from
private join tickets. The first assignment delivery freezes all team choices in
one transaction, including default appearances. Shared colonies use the lowest
numbered human seat's selection. Reconnects refresh assertion lifetime without
changing content. The distinct `glob2-colony-skin+jwt` type and
`glob2-colony-renderer` audience bind account, match, team and version; they
cannot be used as relay join tickets. `GET /api/v1/matches/:id/skins` exposes
already frozen appearances for spectators/history without freezing new choices.
Native clients verify these assertions before downloading paint. Checkout requires
server-side Stripe configuration; without it, purchases remain unavailable.

Authenticated `GET /api/v1/skins` lists presets and the caller's versions;
`PUT /api/v1/skins/equipped` accepts a version ID or null to restore default art,
plus an optional RGB building color. The chosen color is independent of the
immutable preset paint and is frozen/signed alongside the version for a match.
`POST /api/v1/skins/publish` requires the designer entitlement. It accepts a name,
optional owned skin ID for another version, RGB building color, optional swarm
mesh and integer `swarmViewAngle` (0–359, default 0), and two base64 PNG or WebP images in layout `colony-v2`. Both are still
512×512 pixels made of four 256×256 quadrants: worker top-left, warrior
top-right, explorer bottom-left, swarm bottom-right.

- `imageBase64`, the colour atlas, is at most 1 MiB. The server re-encodes it as
  an opaque sRGB lossless WebP without metadata.
- `materialBase64`, the material map, is at most 256 KiB. Every pixel is grey
  (R = G = B), opaque, and a material id below the count registered in
  `libgag/shaders/skin-materials.json` (mirrored as `COLONY_SKIN_MATERIALS` in
  the protocol package). Anything else is a 400. The server re-encodes it as an
  8-bit lossless WebP. Native clients built with the registry shade ids beyond
  their own catalogue as matte, so later materials degrade gracefully; clients
  from before the registry reject such a skin and keep that team's previous
  appearance, since they only knew ids 0 to 3.

The version's `manifestSha256` is described below; native clients recompute it.
Publishing identical content again returns the existing version. Publication and
equipment are immediate. The publication transaction queues `skin:render:<revision>`
with three bounded attempts, deduplicated by immutable version and render revision.
`skin-render-worker` registers its revision and backfills published designs and
presets at startup. Its one native OpenGL process runs with Mesa/llvmpipe under
Xvfb, a five-minute timeout and resource limits. `colony_skin_sprites` records
`pending`, `ready` or `failed`; manifests and page references commit together only
after every image is validated and stored. The library exposes `softwareStatus`
and explains preparing or unavailable software artwork while keeping equipment
available. Ready derivatives are immutable. Renderer upgrades create new records
without rewriting published paint.

A signed optional `softwareSprites` descriptor carries `format`,
`manifestSha256` and `renderRevision`. Its optional `source` identifies the immutable
paint, material and canonical source manifest separately from WebP wire renditions.
Clients verify this signed identity before accepting a bundle; older descriptors
use the signed version identity. Appearance refresh pins the first ready
bundle to the match; later renderer revisions cannot replace it. Its addresses
come from the trusted instance origin:
`GET /api/v1/skins/versions/:id/sprites/:manifestHash/manifest` and
`.../pages/:pageHash`. These endpoints serve only ready, linked blobs and retain
the source version's moderation checks. Blob garbage collection includes both
bundle manifests and pages.
`GET /api/v1/skins/versions/:id/texture` serves the colour atlas and
`GET /api/v1/skins/versions/:id/material` the material map, both as `image/webp`
with the blob SHA-256 as ETag; disabled skins return 404. Older published sources remain immutable. The API caches lossless WebP wire
renditions in `image_webp_renditions`, signs their exact texture/material hashes
and recomputes the wire manifest hash while retaining version IDs. Apply migration
0041 and 0042 before deploying the API and worker together with the WebP-only client.
Old clients that require PNG skins need upgrading; existing signed PNG tickets
must be refreshed before a new client can install their appearances. Skin image
requests include `?sha256=<wire hash>` so cached PNG responses from earlier
releases cannot satisfy requests for the new renditions. Map catalog
preview URLs end in `preview.webp`; existing engine-produced PNG preview sources
are converted through the same persistent rendition cache. End-user PNG/WebP
uploads remain accepted as imports on the server.

Raw uploads and arbitrary blob keys are never served
by these endpoints. The designer can open any owned version or copy a preset
into a new design. Publishing an edit updates the design's display name and
creates an immutable content version; previously equipped versions and frozen
match appearances retain their paint. Equipping the new version is a separate
choice. “Make a separate design” publishes the current canvas under a new identity.

The `/skins` route opens Colony Studio, a mesh painting workspace beside the
shared persistent sidebar. All app pages retain this sidebar, with a compact icon
rail below 1100 pixels and a drawer for expanded navigation.
Brush and eraser paint every surface underneath the cursor, including hidden
surfaces. The eyedropper samples visible geometry. Horizontal right-drag,
Alt-drag or the Rotate tool turns the model; touch uses explicit Paint/Rotate tools and
two-finger pinch zoom. The view menu and +/− keys also adjust inspection zoom.
Animation starts paused and painting freezes its displayed
pose. Each stroke and accepted pattern is one undo transaction. The toolbox,
material swatches (one sphere per registered material, grouped as in the
registry, shaded by the game's own material GLSL including fur shells), model
and pose strips float over the viewport; shop, saved
skins, settings and patterns are dialogs that preserve the document. The workspace
and its dialogs use the web application’s shared Meadow and Night colony themes,
following the device setting or saved preference. Skin settings includes the shared
theme control; changing themes preserves paint and editing state.

Glob meshes share paint coordinates across matching front/back and top/bottom
surfaces, including limb pairs exchanged by their flipping gait. Brush coverage
includes every projected contributor to a texel, applying its strongest coverage
once. Changing a shared texel changes all matching surfaces.
Eyedropper and projected patterns use the closest visible contributor deterministically.
Pattern previews always render the baked atlas, including this repetition.
Camera-projected stripes, spots, checker, chevrons, waves and speckles use the
paused pose and chosen inspection angle. Curated solid, mirrored bands/spots and
mottled fills use per-mesh rest-space compatibility charts, with limited sizes and
densities. Both keep the existing atlas layouts. No UV painting UI or layers are
exposed. Copying raw paint between models is a separate action with a result preview.

The inspection camera stays at a fixed height and angle while the model rotates
around its upright axis; vertical drags do not tilt it. The swarm's
separate **Choose final view** mode changes only azimuth around its standardized
camera ring; accepting it restores the inspection camera. Unit game rendering
continues to select animation directions normally. Building color is separate
from painted color and also colors the rendered material swatches. Without
WebGL2, saved skins and the shop remain accessible while the viewport offers a retry.

Each version also names the swarm mesh its paint is laid out for (`swarmMesh`):
`classic`, the original swarm and the default, or one of the generated shapes
`crown`, `clutch`, `toadstool`, `coral`, `skep` and `bloom`. The protocol's
`SWARM_MESHES` and the game's `src/online/SwarmMeshCatalog.h` list the same ids in
the same order. Because paint is laid out per mesh, the mesh belongs to the
immutable version, and the same paint on two meshes is two versions. The manifest
digest is SHA-256 over the compact JSON object `skinId`, `textureSha256`,
`materialSha256`, `layout`, `buildingColor`, in exactly that key order, followed
by `swarmMesh` only when it is not `classic`, then `swarmViewAngle` only when it is
nonzero. Zero therefore retains all existing manifest hashes. Older clients reject
nonzero angles through their manifest check and use classic cosmetic fallback;
current game clients recompute the complete manifest before
showing a skin. The swarm's paint and materials always come from the swarm
quadrant, whichever mesh is chosen. Clients without mesh choice reject skins for
other meshes and show classic art for that team, rather than painting them onto
the classic swarm. Likewise, an API that finds a stored mesh id it does not know
(after a rollback) omits that version from skin lists and match appearances
instead of signing it, and restores such a draft on the classic swarm.

Registered active accounts can save one private working canvas with
`PUT /api/v1/skins/draft` and restore it with `GET /api/v1/skins/draft`, without
buying the designer unlock. Drafts carry `imageBase64` and `materialBase64` with
the same validation as publishing; one bounded atlas and material map are stored
per account and are never served by public image routes. A save supplies the last observed revision (null for the first save).
Drafts may also retain an owned skin ID so edits resume as new versions of that
design, and they keep the chosen swarm mesh and final view angle. Concurrent or stale saves return
409 rather than overwrite another device's work. The designer also offers a separate account-scoped device draft for offline
backup before resolving conflicts. A debounced recovery record is stored separately
from the explicit device checkpoint, scoped by account, including the last known
account revision. Async restore/open operations preserve any newer local edits
instead of overwriting them. Checkout saves recovery
before navigation and returns to the Shop dialog. Publishing and equipping remain explicit.

Match pages show their frozen colony looks and let signed-in players submit a
reason to `POST /api/v1/skins/versions/:id/reports`. Each account reports a version
at most once. Moderators review the paginated open/closed queue at
`GET /api/v1/admin/skin-reports`, then resolve a report with
`POST /api/v1/admin/skin-reports/:id/resolve` (`dismissed` or `disabled`, with a
reason). Resolution, optional skin disable, and audit records commit together;
concurrent retries do not duplicate the resolution audit. Moderators can restore
or disable a skin using `POST /api/v1/admin/skins/:id/moderation`.

Disabling affects the entire design, including every published version. Public
texture requests return 404, equip/publish checks refuse it, and refreshed match
assertions omit it. Frozen snapshot rows and immutable images remain intact, so
restoration uses the original content. The private moderator endpoints
`GET /api/v1/admin/skins/versions/:id/texture` and `.../material` permit review
of disabled paint with `private, no-store` caching. Existing clients still require moderation refresh
and a local hide control before this provides complete in-match moderation.

## Data model

Migrations are forward-only plain SQL in `platform/packages/db/migrations/`,
applied in order by `glob2-migrate` (`npm run migrate -- latest`); the Kysely
types in `src/schema.ts` are checked column for column against the migrated
database in tests.

| Area | Tables |
| --- | --- |
| Identity | `accounts`, `identities`, `device_credentials`, `refresh_tokens`, `signin_attempts`, `web_sessions`, `auth_flows`, `entitlements`, `admin_audit_log` |
| Infrastructure | `blobs`, `relays` (registration, load, drain, last heartbeat), `engine_agents`, `engine_jobs`, `warm_maps` (the quick-match pool over `generated_maps`) |
| Rooms | `rooms` (settings JSON, revision), `room_members` (with relay round trips), `room_seats` (with locks), `room_chat_messages`, `room_kicks` |
| Map sources | `map_uploads` (private uploads and their validation), `generated_maps` (one generation per descriptor and sim version) |
| Matches | `matches` (the exact `MatchSetup`, seed, map hash, relay and placement attempts, verification, the relay's end report), `match_participants`, `match_team_stats`, `match_artifacts` |
| Ratings | `rating_entities` (an account, or an AI at one sim version), `ratings` (OpenSkill μ/σ per ladder, ordinal generated), `rating_history` (per-match change) |
| Quick match | `queue_tickets` (one active ticket per account), `match_proposals` and `match_proposal_seats` (groups and accept prompts), `queue_cooldowns` |
| Maps | `maps` (owner, visibility, moderation, counters, latest version), `map_versions` (content hash, size, dimensions, team count, preview, validation), `map_likes`, `map_reports`, `map_downloads`; see [Map catalog](#map-catalog) |

Hashes are lowercase hex (`sha256_hex` domain), ids are UUIDs, and enumerations
are text with CHECK constraints so they can grow without type migrations.

### Stored JSON documents

Document-shaped columns (`matches.setup` and `end_report`, `engine_jobs.result`,
`rooms.settings`, `region_rtts`, `match_proposals.map`, generator descriptors and
map facts) are validated against the protocol schemas when they are written, and
decoded again when they are read: `readStored(format, value)` in
`platform/packages/play/src/stored.ts`. Rows outlive the code that wrote them, so
a read

1. takes the document's version from `schemaVersion` (a document without one is
   version 1);
2. refuses a version newer than the code knows, which happens when a newer
   replica wrote the row during a rolling upgrade;
3. upgrades an older version one step at a time through the format's `upgrades`
   table; and
4. checks the result against the current schema and its semantic rules.

A row that cannot be decoded raises `StoredDataError`. Write paths and single-item
reads fail with it (an internal error, logged with the column and the schema
issues), rather than act on a misread document. List pages degrade instead: the
public room list leaves the room out, and match summaries fall back to the
`sim_version` column. The relay's setup endpoint and verify jobs always receive
the current MatchSetup version.

To change a stored shape incompatibly, bump the format's version (for MatchSetup,
`MATCH_SETUP_SCHEMA_VERSION`, which writers stamp into the document), add an
`upgrades[old]` step, and add a frozen document of the old version under
`platform/packages/play/test/fixtures/stored/`. That test decodes every frozen
document of every version, so an old row can never silently stop reading.

## Coordination

- **Job queue.** graphile-worker tables in the same database carry platform jobs
  such as `platform:engine-job-result`. Engine jobs are rows of `engine_jobs`
  instead: `submitEngineJob()` inserts one (so a job submitted in a transaction
  exists exactly when it commits), and an agent of that sim version leases it
  through `platform-api` ([Engine agents](#engine-agents)). Its report is enqueued
  under `platform:engine-job-result` in the same transaction; the worker validates
  it against the kind's result schema and completes the row (a result that breaks
  the contract is recorded as a failure). Deterministic engine failures are
  reported, other errors retry.
  A scheduler sweep (`play/jobSweep.ts`) gives up, through the same result path,
  jobs whose report was accepted but whose result task was lost, and queued jobs
  that no agent of their sim version took for six hours.
- **Pub/sub.** `PgPubSub` keeps one listening connection per process,
  reconnects with backoff and re-listens. Every NOTIFY goes through `notify()`
  (`packages/db/src/notify.ts`; a test fails on any other `pg_notify`): a payload
  over Postgres' 8000-byte limit is stored in `notification_payloads` and the
  notification carries only its id, which `PgPubSub` reads back before
  dispatching, in order. Still publish identifiers and re-read state. Delivery is
  at most once: after a reconnect, reconnect listeners re-read what may have been
  missed. The API re-sends `room.state` of its sockets' rooms, `match.start` for
  their starting and running matches, accept prompts and recent `match.updated`,
  and re-checks pending browser sign-ins (`RealtimeHub.resync`); `/readyz` fails
  while the listener is down.
- **Leader lock.** `LeaderElection` holds a session advisory lock on a
  connection with TCP keepalive and a query timeout; if the leader dies Postgres
  releases the lock and another replica takes over within the retry interval.
  The leader re-checks every 5 s, and before each scheduler task run, that its
  session still holds the lock. With `fencing`, every new leader bumps an epoch
  in `leader_leases`, and the matchmaker's transactions call `assertLease()`,
  which share-locks the lease row: a stale leader that has not yet noticed its
  lost session cannot commit proposals or starts once a newer leader exists.

## Configuration and operation

Each service reads secrets and deployment settings from the environment (or a
`.env` file; see `platform/.env.example`) and instance settings from
`instance.yaml` (see `platform/instance.example.yaml`): name, guest access,
sign-in providers (secrets referenced by environment-variable name), access
policy, queues, and the browser client URL that invite pages link to. Relays
authenticate to `/internal` with keys from `RELAY_KEYS` or `RELAY_KEYS_FILE` (see
[rooms and matches](rooms-and-matches.md#internal-api-for-relays)); upload and
match-record size limits are `UPLOAD_MAX_BYTES` and `RECORD_MAX_BYTES`. Services
log structured JSON to stdout, expose health
endpoints where they serve HTTP, and on SIGTERM stop taking work, finish what is
running and close connections within `SHUTDOWN_GRACE_SECONDS`. The Compose stack
that runs all of them, with Caddy and Postgres, is described in the
[self-hosting guide](../hosting/README.md).

## Engine agents

An engine agent (`platform/apps/engine-agent`) wraps one glob2 binary and runs
the jobs that need the engine. It runs each job as a separate headless process
with a stripped environment. Because that process loads uploaded maps, saves and
match records with the legacy C++ loader, the agent holds no database or blob-store
credentials at all: it reaches the platform only through `platform-api`'s internal
engine API, with a bearer agent key (`ENGINE_AGENT_KEYS`/`ENGINE_AGENT_KEYS_FILE`
on the API, `ENGINE_AGENT_KEY_FILE` on the agent; shapes in the protocol package's
`jobs.ts`):

| Call | Purpose |
| --- | --- |
| `POST /internal/v1/engine/agents/heartbeat`, `DELETE …/agents/{id}` | Announce the agent's sim version and kinds (`engine_agents`). |
| `POST /internal/v1/engine/jobs/lease` | The oldest queued job of the agent's sim version and kinds, with a lease token, or `204`. |
| `POST …/jobs/{id}/extend`, `…/release` | Keep the lease while the engine runs; give the job back for a retry. |
| `POST …/jobs/{id}/result` | Report the result or failure (idempotent per lease). |
| `GET /internal/v1/engine/blobs/{sha256}` | A blob whose hash the leased job's payload contains, and no other. |
| `PUT /internal/v1/engine/blobs?contentType=&visibility=` | Store an output blob by content (known content types only). |

Calls about a job carry its lease token in `X-Glob2-Lease`.

### Sim version and partitioning

At startup the agent learns its sim version from the binary rather than from
configuration, so a mislabelled image cannot serve the wrong version:

- `VERSION_MINOR` and `NET_PROTOCOL_VERSION` come from `glob2 --headless-catalog`
  (`save_version`, `protocol_version`).
- The data hash comes from the binary when it reports one: a `data_hash` field in
  the catalog, or `glob2 --sim-version`, which prints
  `{"versionMinor","netProtocol","dataHash"}`. Both are being added with the engine
  integration work. A binary without the flag does not reject it but starts the
  game, so the agent only runs `--sim-version` when the catalog lists
  `sim_version` under `commands` (or `ENGINE_PROBE_SIM_VERSION=1`).
- Until then, `ENGINE_DATA_HASH` (or a full `ENGINE_SIM_VERSION` key) supplies
  the hash. Any value that disagrees with what the binary reports stops the
  agent at startup.

The agent then registers and leases only jobs of its own sim version. A job can
therefore reach only a binary that computes the same games. A job that reaches
the wrong version anyway is a routing bug and fails loudly.

### Job contracts

Payloads and results are the protocol's `engineJobs` schemas. The agent checks
every result against its kind's schema before reporting it. Maps and saves are
stored decompressed, so their key is the SHA-256 of the bytes clients load. Every
blob the agent stores is also registered in `blobs`.

| Kind | Engine command | Result |
| --- | --- | --- |
| `generate-map` | `--generate-map --generator <method> --map-seed <seed> --candidates <n> --param k=v… --write-map true --output-dir` (structured; method ids and revisions from the catalog) | map blob hash, size, dimensions, team count, chosen seed, start quality |
| `validate-map` | `--preview-map <file> --json report.json` (the game's own loader, no simulation) | `valid: true` with the decompressed hash, dimensions, team count and the file's format version, or `valid: false` with a reason |
| `render-preview` | `--preview-map <file> --output preview.png --preview-size <px>` | PNG blob hash and pixel size |
| `verify-match` | `--verify-match <record> --map <file> --out <dir>` | `verified`/`diverged` with the outcome, team statistics and timelines, or `unverifiable` |

Before running the generator, the agent checks the descriptor against the
catalog. An unknown or editor-only generator, a revision this binary does not
produce, a parameter the generator lacks or a value outside its registered values
is a `bad_request`. So is a non-zero `startingUnitLevel`, which the structured
command cannot express yet.

Validation rejects these files, with a reason:

- files that are not maps or saves, and corrupt gzip;
- files over `ENGINE_MAX_MAP_BYTES` (decompressed, so a gzip bomb stops at the
  limit);
- maps written by a newer engine;
- a map uploaded as a save, or a save uploaded as a map;
- maps with a side over 512 tiles, or with 0 or more than 12 teams;
- anything the loader refuses.

Validation reads the file's format version from the first fields of the map
header, after the engine has loaded the file. This small read in
`engineCli.ts` is the only binary parsing in the platform. It is needed because
the map report gives the engine's version, not the file's.

**`--verify-match` output.** The engine writes (see
[headless replays](../development/headless-replays.md#verifying-a-match-record)), and
the agent reads:

- `<out>/verdict.json`: `{"verdict":"verified"|"diverged"|"unverifiable","seats"?,"reason"?}`;
- `<out>/result.json`: the `HeadlessRunner` game result (team outcomes,
  elimination ticks, prestige, counters and the 512-tick `history`);
- `<out>/match.replay`.

A `verdict.json` decides the verdict whatever the exit code. Without one, exit 2
is an invalid request and anything else is an engine failure. The agent stores
the replay and result.json as blobs (`replayHash`, `resultHash`). Each team in
the outcome carries the final counters as `statistics` and the history as
`timeline` points `{tick, units, buildings, prestige, hp, attack, defense}`.
All parsing of engine output lives in `apps/engine-agent/src/engineCli.ts`; a
change to the engine's command line or output files changes only that module
(and its fixtures, which were captured from a real binary).

**Failures.** Failures are handled by how likely they are to repeat:

- Input problems (`bad_request`) and contract breaks (`internal`) are reported at
  once.
- Timeouts, crashes and transfer errors give the job back (released with a
  backoff of 5 s, 10 s, 20 s …) for another lease.
- On a job's last attempt, any remaining error is reported as `internal`, so the
  platform is never left waiting for a result.

**Process limits.** Each engine process runs in its own scratch directory, which
is also its `HOME` and `GLOB2_USER_DIR`. It gets a minimal environment, and its
whole process group is killed at the wall-clock timeout
(`ENGINE_TIMEOUT_{GENERATE,INSPECT,VERIFY}_S`). It also runs under `ulimit` CPU
and file-size limits, and on Linux an address-space limit (`ENGINE_MEMORY_MB`).
Output files are read back only up to a size limit.

### Results on the platform

The worker's `platform:engine-job-result` handler completes the `engine_jobs`
row. For `verify-match` it also records the verdict and applies ratings in the
same transaction, and stores team statistics and timelines in `match_team_stats`.
It links the record, replay and result blobs in `match_artifacts`, but only blobs
registered in `blobs`. For a generate-map job, it marks the generated map ready or
failed, whether a room, an on-demand queue start or the warm pool asked for it.

The aggregate views from migration 0004 cover verified, ended matches of the
last 90 days:

| View | Contents |
| --- | --- |
| `match_results_view` | One row per seat of every verified match, with queue, generator and map. |
| `recent_win_rates_view` | Games, wins and win rate per player, by queue, map or generator. A player is an account, or an AI id at one sim version. |
| `recent_game_lengths_view` | Mean, median, p90 and maximum length in ticks, by queue and by generator. |
| `team_timeline_view` | The 512-tick samples of each team as rows. |
| `account_economy_curves_view` | A player's units, buildings and prestige at each tick of each match, next to their own average at that tick. |

### Warm map pool

The worker leader runs `WarmMapPool.refill()` every 10 seconds. A sim version
counts as served when an agent running `generate-map` was seen in the last five
minutes. For each queue, served sim version and map pool entry, the pool keeps
`WARM_MAPS_PER_ENTRY` untaken maps (default 2, 0 turns the pool off), either
ready or still generating, and more while an entry is busy: as many as were taken
in the last 15 minutes, up to `WARM_MAPS_MAX_PER_ENTRY` (default 8). For any
shortfall it requests generated maps with fresh seeds.

The pool is a layer over `generated_maps`, not a second generation cache. Each
warm map is a `generated_maps` row requested ahead of time through the same
`requestGeneration` call, and completed by the same `applyMapJobResult`, as room
maps and on-demand queue starts. `warm_maps` holds only the pool bookkeeping:
the queue, the pool entry, the generated map's descriptor hash, and when and for
which match it was taken. Readiness, the job, the map hash and any failure are
read from `generated_maps`.

- An entry that fails three times in ten minutes waits for the window to pass.
  This happens, for example, when the configured revision is not the binary's.
- Maps still generating after 30 minutes are given up and replaced.
- Maps of entries removed from `instance.yaml` are dropped.
- A pool map dropped before it was taken is deleted with its generated map, so
  its blob is collected. A taken map keeps its generated map, like any map a
  match was played on; taken rows are deleted from `warm_maps` after 24 hours.

`takeWarmMap(db, queueId, simVersionKey, { entry?, matchId? })` (exported by
`@glob2/play`) gives a match starter the oldest ready map, using
`FOR UPDATE SKIP LOCKED`. It returns the descriptor with its seed (MatchSetup
`map.generator`), the map hash (`map.hash`) and the generation result, or
`undefined` if none is ready. The next refill replaces a taken map.

Engine agent freshness (`ENGINE_AGENT_FRESH_SECONDS`, `freshAgentSimVersions` in
`@glob2/core`) is the one definition the pool, the API's served-version list and
the stale engine-job sweep use.

### Scaling and operation

- **More throughput:** run more agents of the same image. They lease from the
  same queue (`FOR UPDATE SKIP LOCKED`), and each runs `ENGINE_CONCURRENCY` jobs at
  once, polling every `ENGINE_POLL_MS` when idle.
  Verification is the costly kind, since it runs whole games, so size
  `ENGINE_TIMEOUT_VERIFY_S` and the replica count for the longest games played.
- **An agent dies mid-job:** its lease (two minutes, renewed every 40 seconds while
  the engine runs) runs out and another agent leases the job; each lease counts
  as an attempt (three by default). When the last attempt's lease runs out, the
  worker's scheduler reports the job failed (`failAbandonedEngineJobs`), so nothing
  waits forever. Results are applied once, keyed by job id.
- **Serving an older sim version** (a verifier image for an old version, so its
  matches can still be verified and its rooms still get maps):
  1. Build the engine at that version's tag, with the same compiler image and
     flags as its release. The image must compute byte-identical games, so
     check it with the replay verification guide
     (`docs/development/headless-replays.md`).
  2. Build the agent image from that binary plus this wrapper.
  3. Set `ENGINE_DATA_HASH` if the old binary cannot report its own hash.
  4. Run the image next to the current one. Once it registers,
     `GET /api/v1/instance` lists the version as served, and its jobs flow to it.
  5. When no agent of a version has been seen for five minutes, clients of that
     version get `update_required`.

## Map catalog

The catalog (milestone M7) keeps shared maps and their versions. The REST routes
are in `apps/api/src/maps/` (`routes.ts`; rules and views in `catalog.ts`). The
worker applies engine-job results to versions (`packages/play/src/play/catalog.ts`).
Product defaults that are still provisional live in `CATALOG_RULES`.

**Maps and versions.** A map has an owner, a title, a description, a visibility,
how it was made (`hand` or `generator`, with the generator descriptor if known)
and a moderation flag. Each upload of different bytes is a new version, keyed by the
SHA-256 of the bytes clients load. A version records the sim version that checked
it and, once valid, its dimensions, team count, the format version it was saved
with (`minVersionMinor`, the oldest engine that can load it), the map name stored in
the file and a preview. `maps.latest_version_id` points at the newest valid
version. Listings show and filter on it.

**Upload.**

1. `POST /api/v1/maps` with `CreateMapRequest` creates the map (`201 MapInfo`).
2. `POST /api/v1/maps/{id}/versions?simVersion=<key>&notes=…` takes the
   file, plain or gzip-compressed, as `application/octet-stream`, up to
   `UPLOAD_MAX_BYTES`. The API unpacks gzip and checks the map header first, with
   the same plain-language `400` answers as room uploads
   ([rooms and matches](rooms-and-matches.md)). The upload names its sim version;
   without one (web uploads), the newest version the instance serves is used. Only
   the owner may upload. If the same bytes were already checked as a room upload
   (`/api/v1/uploads`) for that sim version, the version reuses the verdict.

   The web app's upload form checks the file with `POST /api/v1/uploads` first and
   polls it; only a file the game loads gets a map (step 1) and a version, so a
   failed upload leaves no empty map page behind.
3. The bytes become a private blob. The version row is written with two job ids,
   and only then are the jobs submitted, so a fast result always finds its row:
   - `validate-map` (format `map`);
   - `render-preview` (512 px).
4. The worker applies the results: facts and `validation: valid`, or `invalid`
   with a reason, then the preview. A file whose validated hash differs from the
   upload (for example a gzip-compressed upload) is invalid.
5. Bytes already checked for the same sim version, on any map, reuse that
   validation and preview without new jobs. Uploading the same bytes to the same
   map again answers the existing version (`200`).

Each replica allows 20 new maps and 20 version uploads per account per hour, and
a map keeps at most 50 versions. Owners edit title, description and visibility
with `PATCH /api/v1/maps/{id}`. They delete a map with `DELETE /api/v1/maps/{id}`,
or one version with `DELETE /api/v1/maps/{id}/versions/{hash}`. Deleting removes
the catalog rows but keeps the blobs, because matches and rooms may still use the
bytes.

**Visibility.** Moderators and administrators see every map. Every other caller
gets `404` for a map they may not see, so its existence does not leak.

| Visibility | Listed | Map, versions, file, preview, blob by hash |
| --- | --- | --- |
| `public` | in `GET /api/v1/maps` and the owner's public list | anyone, signed in or not |
| `unlisted` (default) | no | anyone with the id or hash |
| `private` | no | the owner |
| hidden by a moderator | no | the owner (with `hiddenReason`) |

Pending and invalid versions are shown only to the owner. Guests may create
unlisted and private maps but not publish them. `GET /api/v1/blobs/maps/{hash}`
applies the same rules to catalog versions, alongside its upload, room and match
rules. Rooms may choose `{kind: "catalog", hash, mapId?}` when the version is
valid, its map is not hidden, the map is public, unlisted or the host's own, and
`minVersionMinor` is no newer than the room's engine.

**Browsing.** `GET /api/v1/maps` lists public maps that have a valid version. It
takes these filters:

- `owner=me` lists every map the caller owns; `owner=<accountId>` lists that
  account's public maps, or all of them for the owner and moderators;
- `teams` (exact), `minSide` and `maxSide` (the larger side, in tiles), `madeWith`
  and `q` (title search) filter on the latest version;
- `sort` is `recent` (default), `likes`, `plays` or `downloads`, newest or
  highest first;
- `limit` (default 30, at most 100) and `cursor` page through the results.

`GET /api/v1/maps/{id}` returns `MapDetail`: the map, its versions (newest first)
and what the caller may do (`viewer.owner`, `moderator`, `liked`, `reported`).
`GET /api/v1/maps/{id}/versions/{hash}` returns one version. `…/file` serves the
bytes as an attachment, and `…/preview.webp` serves the preview.

**Stats.**

- **Plays:** ended matches on any version of the map, counted when the relay's end
  report is applied (once per match).
- **Downloads:** `…/file` requests, counted once per downloader and day (by account,
  or by address when signed out). The owner's own downloads do not count.
- **Likes:** `PUT` and `DELETE /api/v1/maps/{id}/like` count registered accounts once
  each, and answer `MapLikeResult`.

**Reports and moderation.** Any signed-in account may report a map it can see with
`POST /api/v1/maps/{id}/reports` (`MapReportRequest`). Each account may have one
open report per map; a repeat answers the open one. Each account may file 10 reports
per hour. Moderators and administrators can then use these routes:

| Route | Effect |
| --- | --- |
| `GET /api/v1/admin/map-reports?status=open\|resolved\|dismissed\|all&mapId=&cursor=` | Reports, newest first, with the map and reporter |
| `POST /api/v1/admin/map-reports/{id}/resolve` | `ResolveMapReportRequest`: `resolved` or `dismissed`, an optional note, and optionally hide the map |
| `POST /api/v1/admin/maps/{id}/hide` | Hide with a reason (`MapHideRequest`) |
| `POST /api/v1/admin/maps/{id}/unhide` | Show again |

Administrators may also delete any map. Every moderation action is written to
`admin_audit_log` with `target_type` `map`.

## Working on the platform

The workspace needs Node 22.18 or newer (TypeScript runs directly through Node's
type stripping, so there is no build step except for the web app).

```sh
cd platform
npm ci
docker run -d --name glob2-pg -p 127.0.0.1:55432:5432 \
  -e POSTGRES_USER=glob2 -e POSTGRES_PASSWORD=glob2 postgres:16
npm run check        # lint, format check, type check, tests
npm run fixtures     # regenerate protocol fixtures after schema changes
DATABASE_URL=postgres://glob2:glob2@127.0.0.1:55432/glob2 npm run migrate -- latest
DATABASE_URL=… node apps/api/src/main.ts
```

Tests create and drop their own databases on the server named by
`TEST_DATABASE_URL` (default `postgres://glob2:glob2@127.0.0.1:55432/postgres`).
CI runs the `platform` job in `.github/workflows/build.yml` against a Postgres
service whenever `platform/` changes; generated protocol fixtures also select the
native jobs, which hold the C++ contract tests. The web app's Playwright suites
(`apps/web/e2e`: page smoke tests and axe accessibility checks) run locally with
`npm run build -w @glob2/web && npm run e2e -w @glob2/web` against a seeded API
on the test Postgres. The whole deployed stack, including a rated match, has its
own one-command test; see
[End-to-end test of the stack](../hosting/README.md#end-to-end-test-of-the-stack).

## Delivery milestones

Each milestone is one or more reviewable pull requests; YOG kept working until M9.

| | Milestone | Content |
| --- | --- | --- |
| M0 | Foundations | This workspace, protocol contracts and fixtures, data model, CI job, design docs; C++ JSON, HTTP fetch, WebSocket text mode and the `LockstepSession` interface |
| M1 | Turn netcode core | Turn sequencer, jitter buffer, client session, match record, multi-client harness, `--verify-match` |
| M2 | Relay and LAN | `role=relay`; LAN on the new netcode; first playtest of the new netcode's feel |
| M3 | Identity | Accounts, guests, providers, handoff sign-in, tokens and JWKS, platform client, admin CLI, hub sign-in |
| M4 | Rooms and matches | Rooms, uploads, generation jobs, tickets, relay registration and allocation, invite links, room screen, compose stack v2 |
| M5 | Verification and ratings | Verify jobs, OpenSkill, AI entities, leaderboard, profile and match pages, post-game screen |
| M6 | Quick match | Queue config, matchmaker, region probes, AI backfill, warm map pool |
| M7 | Map catalog | Upload, browse, previews, moderation |
| M8 | Admin and polish | Admin pages, connection HUD, phone layouts |
| M9 | Cutover | Delete YOG, IRC and the server and router roles; update docs (done) |

The original plan referred to `src/net/gateway/` for server patterns; that
directory was removed when transport moved to native WSS, and its equivalents now
live in `src/net/NetTransport.cpp`, `src/net/WssTransport.cpp`,
`src/net/ServerControl.cpp` and `deploy/`.


## Colony skin payments

Stripe-hosted checkout uses three server-defined products: `designer`, `stripes`,
and `spots`. The API seeds two immutable presets (colour atlas and material map
from `platform/apps/api/assets/skins/`) with stable IDs;
existing versions and moderation decisions are never overwritten at startup.
The web designer includes the store, account purchase history, and explicit
payment reconciliation after returning from Checkout. Configure `STRIPE_SECRET_KEY`, `STRIPE_WEBHOOK_SECRET`, and the
corresponding `STRIPE_PRICE_DESIGNER`, `STRIPE_PRICE_STRIPES`, `STRIPE_PRICE_SPOTS`.
Prices must be active fixed one-time Stripe prices. Test keys are the default;
live keys require explicit `STRIPE_LIVE_ENABLED=true`. Missing configuration
leaves painting available and purchases unavailable.

`POST /api/v1/skins/checkout` accepts a product and UUID request ID. Registered
active accounts are required. The server records the purchase before creating
Checkout, uses its ID as Stripe's idempotency key, and reuses pending purchases.
`GET /api/v1/skins/products` returns configured price/currency and availability;
`GET /api/v1/skins/purchases` lists the caller's recent purchases. A redirect
never grants access. `POST /api/v1/skins/purchases/reconcile` takes an owned
purchase ID and reads current Stripe state before changing its entitlement.

Configure the signed `/api/v1/skins/stripe-webhook` endpoint for
`checkout.session.completed`, `checkout.session.async_payment_succeeded`,
`checkout.session.async_payment_failed`, `checkout.session.expired`,
`charge.refunded`, `charge.dispute.created`, `charge.dispute.updated`, and
`charge.dispute.closed`. The scoped parser retains exact request bytes for SDK
signature validation. Event IDs are recorded after successful processing, and
purchase row locks serialize reconciliation. Every reconciliation re-reads the
session and verifies mode, owner, product price and quantity. It grants one
entitlement per purchase only after payment. Any refund revokes that grant;
unresolved/lost disputes suspend it and won/closed-warning disputes restore it
when the payment remains paid. Other grants for the same product are unaffected.

The implementation follows Stripe's [fulfillment guidance](https://docs.stripe.com/checkout/fulfillment)
and [webhook signature requirements](https://docs.stripe.com/webhooks).
When configured, each API process runs a reconciliation sweep every minute.
A sweep claims up to five due purchases with `FOR UPDATE SKIP LOCKED` and a
10-minute database lease before contacting Stripe. Successful pending/disputed
checks repeat after 15 minutes; settled payments repeat daily. Provider failures
keep the lease delay, and process crashes become retryable when it expires.
Shutdown stops taking further work and waits for the current check. This recovers
missed webhooks without overlapping checks across API replicas. Webhooks and the
account's check-payment action still reconcile immediately.

A repeated checkout reuses the recorded Stripe session instead of relying on
Stripe's [finite idempotency-key retention](https://docs.stripe.com/api/idempotent_requests).
Each purchase fixes the session expiry at 23 hours after its creation; fresh
creation requests stop after 22 hours. Retries keep the same expiry and key, so
an old creation request cannot produce another payable session after key expiry.
Creation and recording its ID are serialized under the purchase row lock.

If a crash loses the checkout ID, recovery starts after 24 hours, when its
creation window is closed. It reads [Checkout session pages](https://docs.stripe.com/api/checkout/sessions/list)
within that fixed window, validates purchase/account metadata, and persists the
pagination cursor. Each sweep reads at most one page per purchase. A recovered
session then undergoes the normal payment and price checks. Only a complete
scan with no match marks the purchase failed, allowing a new request; a scan
error leaves the purchase pending for retry. Expired recorded sessions also
become failed purchases. Production provider checkout verification is still
required before enabling a store.

### Game client appearance loading

Match assignments carry signed `glob2-colony-skin+jwt` assertions, bound to the
instance, match, team, account, immutable version (colour atlas and material
map hashes and manifest) and chosen building color. `SkinAuthorization` verifies Ed25519 with OpenSSL natively and asynchronous
WebCrypto in the browser. Assertions have a maximum 24-hour lifetime, with
30 seconds of clock tolerance. The client derives download URLs from its trusted
instance origin and verified version ID; an assignment cannot supply a texture
or key-server URL.

`SkinDownloads` fetches JWKS with a 64 KiB limit, then each version's colour
atlas (1 MiB limit) and material map (256 KiB limit), four at a time. It verifies
the signed SHA-256 values and 512×512 still WebP dimensions before image decoding. The loader stays attached to the view and refreshes the
trusted match appearance endpoint every minute, with a 512 KiB response limit.
A complete valid snapshot removes omitted teams immediately; additions require
fresh signature and texture verification. Failed or malformed refreshes retain
currently authorized paint until its signed expiry, then restore classic art.
The endpoint is not cacheable. Cached bytes are rechecked before reuse, and the cache
retains at most 64 managed textures. Failure leaves the classic appearance in
place. Downloads are polled from the view and never gate simulation startup.
Verified textures and building colors live in `MapRenderState`, not team state
or saved simulation data. The saved device preference **Show colony skins** is
available in Settings > Display and the in-game Options dialog. Turning it off
immediately restores classic units, swarms and building colors locally; verified
appearance refreshes continue, so turning it back on uses current authorization.
Software rendering uses the published sprite bundle for workers, warriors,
explorers and the selected swarm mesh and angle. Other buildings and zoomed-out
unit markers keep the signed building color. Without ready artwork, classic
sprites remain visible with that color. Software clients download neither paint
images nor meshes and create no OpenGL context for these skins. Unknown bundle
formats retain this fallback; optional metadata keeps old claims compatible.

The view verifies the signed descriptor, manifest hash and source identity before
requesting pages, then verifies each page's SHA-256, byte count and static WebP
header before decoding. Manifests are limited to 64 KiB, pages to 2 MiB compressed
and their fixed 1024×1024 (unit) or 128×128 (swarm) dimensions. Up to four fetches
run concurrently and at most one page decodes per view poll. Content-addressed
pages are shared between teams, with a 64 MiB decoded LRU cache and 256 MiB disk
cache whose bytes are revalidated on reuse. Completed offscreen requests release
their slots without decoding; camera movement cannot block subsequent downloads.
If disk writes fail, verified compressed buffers share the four-slot budget until
decoding, so artwork remains available without an unbounded memory queue.
Unused decoded pages are evicted;
expiry or moderation removes installed appearance. Existing animation mapping,
shadows, fog, zoom, clipping and the Show colony skins preference apply to both
rendering paths. This is presentation state and does not alter saves, simulation
checksums or `SIM_REVISION`.
Skin meshes are installed under `data/skins/colony-v1`; they share the web
designer's UV layout, each model sampling its own `colony-v2` quadrant. The browser ships them in an on-demand `skins` package
requested when visible paint is available. Classic rendering continues during
the download; hidden or unskinned colonies do not initiate it. Failed package requests retry at most every ten
seconds without stopping the match.

Each verified skin also selects its swarm mesh: `swarm.gsk`, derived from the
original art, for `classic`, or `swarm-<id>.gsk` for a shape generated by
`tools/skins/generate_swarms.py` (see the
[unit animation tooling](../../tools/unit-animation/README.md)). A skin naming a
mesh this client does not know is rejected like any other invalid assertion, and
a mesh file that fails to load leaves that colony's swarm on the classic sprite.
A nonzero final angle reconstructs model-space positions, rotates around the
world vertical axis, then applies the same game projection. Normals rotate with
the model. Each transformed mesh receives a fresh render-cache identity. Height,
target, radius, scale and ground alignment stay fixed around the ring. These are
appearance-only changes; simulation state and version gates are unchanged.

`tools/skins/export_views.py` (pinned Blender 3.6.23) emits versioned `.view.json`
sidecars bound to each GSK SHA-256 and the corresponding native constants in
`src/online/SkinViewTransforms.h`. It recovers the original unit/source camera and
separate depth scaling without changing GSK payloads, UVs, topology or poses.
Regenerate sidecars/constants whenever source mesh exports change. The web
viewport, brush and pattern engine share these transforms; the native rendering
path uses the generated swarm constants. `studio_thumbnails.py` regenerates the
model and action thumbnails from the shipped meshes.

Online replay recordings and native profile downloads have an optional
`<recording>.appearance.json` companion containing format version 1, instance
origin, match ID and SHA-256 of the recording. Replay bytes and version gates are
unchanged. Keep this companion with a copied or renamed replay (renaming both).
The client accepts at most 1 KiB of metadata, hashes recordings up to 64 MiB in
small chunks, and uses it only for an already trusted instance and matching
recording. Missing, stale, malformed or untrusted metadata leaves classic art.
Playback fetches fresh signed match appearance immediately and retains the usual
moderation refresh and local hiding. Companion write failures do not fail a
recording. Browser watch links create the same temporary companion only for the
hosting instance's exact match-replay route, without cross-origin redirects.

The live mesh renderer supports desktop OpenGL and WebGL2. A visible-scene
prepass rasterizes missing mesh/pose/paint combinations into persistent atlases
before map drawing. A least-recently-used cache holds at most 1,024 tiles across
four 2,048-square RGBA pages and a shared depth attachment (80 MiB maximum).
Paint revisions, mesh reloads and fresh texture lifetimes get distinct keys;
context teardown discards the cache. The prepass protects visible hits before
evicting old tiles, and overflow draws regenerate evicted poses on demand.
Identical units, wrapped copies and subsequent frames reuse those tiles, while their
composites retain the original ground-unit/building/air-unit order and visibility
rules. Unit meshes retain the original action/direction shadow layer beneath
the live geometry, using the same logical canvas (including HD shadow art when
available). The layer is emitted only after a mesh tile is ready, so fallback
cannot draw it twice. Requests are sorted by mesh and pose to share geometry uploads between
team textures. WebGL2 uses explicit GLSL ES shaders and an interleaved vertex
buffer; both backends restore the map renderer's state after the prepass. Context restoration recreates these resources
from retained meshes and paint. Native mobile rendering, live spectator
attachment and full performance validation remain required
before release.
## AI Map Studio

The optional map studio lives at `/map-studio` on the online app host, reached
through **Build in AI Map Studio** in the Maps library. The persistent main sidebar groups
the studio under Maps. The public
static website can link into it; it does not hold accounts, credits or authoring
state. The maintained image-authoring modules were ported from the separate
`Globulation2/glob2-ai-map-generation` prototype (GPL-3.0-or-later, originally
extracted from Glob2 commit `1888710f5e948013e0b0859c61a6f1588144a213`).

`packages/billing` shares credit arithmetic and hosted-checkout mechanics while
preserving Hive's durable namespace and adding separate map wallets/purchases.
`packages/map-studio` owns transactional threads, messages, generation snapshots
and delivery. `packages/engine` shares the bounded native adapter and blob I/O
between the engine agent and authoring worker. `apps/ai-map-worker` journals provider attempts and runs bounded
Python reference/crop processes. Native conversion is an `import-ai-map` engine
job routed by simulation version. Its map, preview, categorical export and report
are private blobs. Provider keys never reach Python or engine subprocesses.

REST under `/api/v1/map-studio` provides account state, thread creation/listing,
messages, explicit generation, checkout, per-request progress and authorized stage
images. Thread creation accepts an optional client UUID; retrying the same owner,
UUID and title returns the original project. Clients persist this UUID and the
first message request ID before sending so an unknown HTTP outcome does not
create a duplicate project. The browser reads a repeatable-read thread snapshot
with an event cursor, then opens `/threads/:id/events` as a persistent SSE stream. Events and their
state changes commit together; a locked per-thread counter preserves commit
order. Postgres notifications wake readers, while reconnect listeners and
15-second heartbeat catch-up recover missed notifications. `Last-Event-ID`
resumes delivery; clients deduplicate cursors. Session authorization is checked
on every catch-up, and slow connections close instead of buffering indefinitely.
The old long-poll route remains available for older clients.

Stage, artifact and check events retain the creation journey with the owning
thread. Image descriptors reference owner-authorized routes, never arbitrary
blob hashes. Older requests recover only recorded images and delivery summaries
from a safe checkpoint allowlist; missing historical checks are not invented.
Events and descriptors participate in account export and cascade on deletion.
The workspace beside the shared sidebar separates chat from the inspected map, supports
following live stages or inspecting history, and displays playability checks.
The separate no-credit landing page preserves draft writing and access to saved
projects; active last-credit generations open their workspace. Drafts, pending
submission identities and revision settings survive same-tab refresh and checkout.
Payment-return URLs trigger wallet refresh without granting credits themselves.
Messages cost no map credits but require an available
map credit. A Generate action reserves one credit; a successful validated
delivery consumes it and failures return it. Each request snapshots the rolling
conversation and accumulated design brief, settings, parent version and pipeline
version. A parent revision retains its dimensions/player count; changing these
starts a fresh map in the same thread. Previous versions remain immutable; edited downloads must be uploaded as a new
catalog map instead of replacing an AI artifact.

Delivered versions become independent private catalog entries. Publishing changes
only the selected map's visibility; conversation and sibling drafts stay private.
The studio can create a link-only room from a selected catalog version and send
the player to browser play with its invite. Existing room/match authorization
permits participants to fetch the map without accessing the authoring thread.
Private previews are served by catalog authorization, not public blob metadata.

The account’s **Download my data** export includes its Studio threads, messages,
revision inputs and checkpoints, provider attempts, and separate map-credit wallet,
ledger, purchases and usage. It includes only the owner’s data and omits internal
worker lease credentials. Catalog exports also include map authoring metadata.
Account deletion removes private Studio history and import jobs and releases
unfinished generation reservations. `studio_provider_usage` retains only daily
UTC call totals, maintained by a journal-insert trigger, so deleting projects
cannot replenish the service's provider-call budget. These totals contain no
account identity or conversation data; financial ledger and purchase records
remain separate from the deleted authoring history.

The supported envelope is independent 128/256/512-cell sides and 2–8 colonies.
The post-import native report gates valid starts, walking connectivity, nearby
wheat/timber, buildable ground and fertile grass. These are minimum opening checks,
not proof of competitive balance, long-term economy or human enjoyment. Qualify
model outputs with modern-AI games, sustained growth checks and human play review
before enabling sales. See the hosting guide for flags, credentials and recovery.


## JavaScript AI library

`/api/v1/ais` provides a versioned catalogue for local-play JavaScript controllers.
The website owns publishing; the native settings library discovers, downloads and
installs exact releases. The contracts live in `packages/protocol/src/ais.ts`.

| Route | Behaviour |
| --- | --- |
| `GET /api/v1/ais` | `q`, comma-separated `tags`, `sort=likes|newest|updated|downloads`, `favourites=true`, `owner=me`, keyset `cursor` and bounded `limit` |
| `GET /api/v1/ais/{id}` | AI identity, versions and historical validation evidence |
| `POST /api/v1/ai-uploads` | Private staged UTF-8 source bytes; queues/coalesces validation |
| `GET /api/v1/ai-uploads/{id}` | Owner-only persistent checklist and retryable failure |
| `POST /api/v1/ais` / `POST /api/v1/ais/{id}/versions` | Atomically consume a passing upload into a new AI or owned release |
| `PATCH /api/v1/ais/{id}` | Owner edits catalogue metadata and visibility |
| `DELETE /api/v1/ais/{id}` | Owner removes a catalogue AI |
| `GET /api/v1/ais/{id}/versions/{versionId}/file` | Exact bytes, safe `.js` attachment, daily deduplicated version download accounting |
| `PUT` / `DELETE /api/v1/ais/{id}/like` or `/favourite` | Idempotent registered-account social actions |
| `POST /api/v1/ais/{id}/reports` | Map-library reporting reasons and limits |
| `/api/v1/admin/ais/...` | Moderator report queue, resolution, hide and unhide |

AI identities own likes and private favourites. Immutable `ai_versions` own source
hashes, version labels, notes and download statistics. `ai_validations` bind seven
required checks to the exact hash, simulation version and validation-suite revision.
Publication locks the staged upload and parent AI; concurrent retries return the
same publication, and duplicate labels or source within an AI are rejected. Public
is the publication default; private, unlisted and moderator-hidden visibility use
the map catalogue's access conventions. Account export and deletion include AI data.

Only an active agent advertising `validate-ai` enables upload validation. Agents
probe the real engine inside Linux namespace isolation before advertising it.
The worker keeps historical reports and schedules validation for new supported
engine versions. Errors never erase a previous passing report. Expired staging
uploads and unreferenced validation blobs are reclaimed by maintenance and blob GC.
After seven days, abandoned validations without uploads or published releases are
collected, including queued jobs whose validator disappeared; active leases and
reported results are allowed to finish. Published infrastructure failures can retry
even after the original job has aged out of job history. A retry of the same source
after infrastructure failure starts another job.

Suite 1 pins two script-free fixtures (two and four players) under
`apps/engine-agent/fixtures/ais`. Each controller runs against built-in opponents
for at most 4,096 ticks, repeats from the same saved initial state, and continues
from tick 2,048. Legitimate early endings restore the final save. Full per-tick
records are compared; controller disablement is reported separately from process
exit. Changing fixtures or validation semantics requires a new suite revision.

Installation verifies the hash before startup checks and durable storage. Optional
local provenance stores origin, AI identity, version identity and hash. Explicit
updates preserve local IDs and roll back on persistence failure; saves and replays
embed source exactly as before. No local gameplay telemetry, competitive ratings,
tournament registration or multiplayer custom controllers are introduced.
## AI coding Studio

The opt-in `aiStudio` service provides private, single-file JavaScript projects at
`/ai-studio`. Chat edits, restores, imports and saved manual edits retain immutable
source revisions. Every model request names its expected revision and credit cap;
concurrent edits are rejected while that request is active. Replies stream through
durable, cursor-based polling events. Only a complete replacement updates code.
The assistant has no execution tools: checks, playtests and further repair prompts
are explicit user actions. New projects use the profile-2 starter; imported files
retain their API profile.

`apps/api/src/ai-studio` owns projects, request dispatch and provider-result recovery.
Each API replica dispatches one model request at a time.
Its SQL tables use `ai_studio_`, including a separate wallet, ledger, calls and
purchases. Billing shares the Hive/Map accounting implementation through an explicit
product registry. Reservations precede dispatch; known usage settles once, even if
generated source is invalid. Unknown outcomes retain their reservation and are
never redispatched. A journaled provider result can finish after a process restart or a transient
settlement/database failure without another model call. The project stays locked
until finalization or cancellation is recorded. A late result from an expired
request is retained as reconciliation evidence and never revives the old edit.
An administrator reconciles unknown usage at `POST /api/v1/ai-studio/reconcile`,
providing `requestId`, verified `usage` (`input`, `cachedInput`, `output`) and an
`evidence` explanation. Zero usage is appropriate only after confirming no billable
work occurred. Verified charges never exceed the original reservation; the
operator absorbs any overrun, with full measured usage and the adjustment recorded
in the ledger. Reconciliation does not apply an old edit to a newer draft.

Run checks uses the existing isolated AI validator and source-hash-bound upload
receipts; publishing uses the unchanged AI Library API. A saved project retains
its source and validation blobs until deletion. Account exports include Studio
projects, revisions, requests, events, playtests and billing. Deletion fences
request completion, removes project content, and retains financial audit records.

Live playtests use `/play/studio.html`, the browser engine's temporary-profile entry.
A version-1, same-origin message bridge verifies the parent window, run ID, revision,
source limits and the pinned two-player map hash. Source executes inside Glob2's
JavaScript runtime. Tests default to seed 19 and Numbi; Nicowar is also available.
The existing AI-only local spectator path supplies camera, pause and speed controls.
Runtime diagnostics and results are bounded and belong to the tested revision;
editing a draft never changes an ongoing match. Browser results cannot authorize
publication. Closing the workspace ends its local test.

The Studio parent document requires COOP/COEP headers, so crossing into or out of
its route performs a document navigation. Only the dedicated game entry permits
same-origin framing; ordinary game, account and admin pages remain unframeable.
Monaco worker responses also carry the parent's COEP policy.
Monaco and its language workers load on desktop only; narrow screens retain a plain
text editor. Local unsaved-draft recovery is scoped to account and project.

Enable only after a maintainer playthrough. Configure `aiStudio.enabled`, `model`,
`rate` (version and integer credits per million input/cached-input/output tokens),
`maxRequestCredits`, and `maxOutputTokens` in the instance config, plus
`AI_STUDIO_OPENAI_API_KEY`. Credit sales additionally need `salesEnabled`, configured
`packs`, `AI_STUDIO_STRIPE_SECRET_KEY`, and `AI_STUDIO_STRIPE_WEBHOOK_SECRET`; the
Stripe callback is `/api/v1/ai-studio/stripe`. Keep rates and caps aligned with the
selected model; changing a rate does not change a previously reserved call.

Studio checkout submissions carry a client-generated purchase UUID. Retrying a lost
response reuses that purchase, its captured credit pack and its Stripe idempotency
key. Returning from checkout starts a new attempt for a subsequent purchase.
Known open checkout sessions are retrieved; unknown outcomes older than 23 hours
require reconciliation rather than redispatch, because Stripe may expire
[idempotency keys after 24 hours](https://docs.stripe.com/api/idempotent_requests).
The existing Hive and Map checkout callers retain their separate products and
balances.

Monitor `ai_studio_requests` status/lease ages, `ai_studio_calls` with `uncertain`
status, validation queue age, and browser launch errors. Studio request failures
are structured API logs. Playtest summaries are client-reported development data,
not competitive ratings or trusted match results. V1 has no multiplayer custom
controllers, remote live games, community remixing, subscriptions, or shared credits.

## Community music

`@glob2/music` owns release views, source retention and streaming ZIP export;
`protocol/src/music.ts` defines portable JSON schemas and generated fixtures.
`apps/api/src/music/routes.ts` exposes `/api/v1/music` for catalogue search,
creator drafts, uploads, inspection/conversion/publication, likes, reports and
individual/set/bulk downloads. Browsing, playback and downloads are anonymous;
publishing and reversible likes require registered accounts. Likes are the
rating, with one row per account/release. Search covers title, artist, description
and tags, with tag, licence, AI and duration filters. Pagination orders by score,
creation time and UUID; clients retain selected UUIDs across result pages.

Processing state changes and Graphile job insertion commit in one transaction.
Transient failures retain source references for up to three attempts; terminal
state is committed before source cleanup. The sweeper protects active draft
references while collecting abandoned uploads after 24 hours without activity.

`apps/music-worker` independently consumes `music-inspect` and `music-convert`
Graphile jobs. It never leases simulation-version engine jobs. It stages sources
from the private `music-uploads/` namespace, invokes `glob2music.community`, then
stores final audio, cover, waveform summaries and ZIPs in the content-addressed
blob store. PostgreSQL holds release metadata, processing state, likes, reports
and `music_assets` references. Response envelopes also include waveform peaks.
Original uploads and PCM are deleted after conversion, cancellation or terminal
failure; the scheduler expires abandoned uploads after 24 hours and sweeps orphan
source blobs. A release advisory lock and conditional state transitions make
repeated job delivery safe, including cancellation during conversion.

Published content and embedded tags are immutable. Creators withdraw releases;
moderators hide/unhide them through the Music administration tab and resolve
reports with an audit entry. Unavailable releases are removed from public lists
and every media/download route. Their previously installed copies remain local.
Private previews and public files pass through release visibility checks instead
of the generic public blob endpoint. Music asset references protect retained
files from blob garbage collection.

The React catalogue and dedicated game screens share Calm/Building/Combat labels,
waveform colors, preview controls and primary action placement. The website's
AudioWorklet receives bounded mixed PCM from a WASM build of the native preview
code. See [music pipeline](../assets/music-pipeline.md#community-releases) for the
self-contained file format and [hosting](../hosting/README.md#music-worker) for
operational limits.

## AI Music Studio

`/music-studio` provides CPU-only conversational soundtrack authoring, reached
through **Build in AI Music Studio** in the Music library. The persistent main sidebar groups
the studio under Music. REST under
`/api/v1/music-studio` owns account state, projects, messages, explicit generation,
cancellation, private artifacts and checkout. `packages/music-studio` owns the
transactional journal and delivery; `apps/ai-music-worker` owns provider calls,
bounded agent tools and isolated Python execution. The score/rendering contracts
are described in the [music pipeline](../assets/music-pipeline.md#online-ai-music-studio).
Discussion and composition calls use distinct strict JSON response schemas
through the provider's response format; the worker also validates discussion
fields and tool actions before using them. A prompt alone does not establish
that transport contract. Only the final answer message is consumed; commentary
and intermediate JSON messages are not concatenated into composer actions.
Each source write schedules trusted score validation and rendering automatically;
the next model call receives the resulting checks to repair any failures.
This prevents repeated source rewrites from consuming the action budget before
a candidate is ever validated. The three-render limit still applies.
Common project/message and credit-pack schemas live in `protocol/src/studioCommon.ts`;
studio-specific settings, products and balances remain separate.

Music uses its own wallets, ledger and Stripe purchases. A generation reserves one
credit; creating its private immutable music release and consuming the credit
commit together. Failure or cancellation returns the reservation once. Unknown
provider outcomes keep the reservation pending for administrative reconciliation
at `POST /api/v1/admin/music-studio/requests/:id/fail`. Provider attempts are
journalled before dispatch and anonymous daily usage survives history deletion.
Known rejections are replayed as failures without another provider call. Completed
provider results survive worker recovery; only outcomes that cannot be established
from the journal require reconciliation. Render cycles are recorded before execution,
and completed candidates retain reports plus content-addressed score and audio references.
A worker restart consumes an interrupted cycle or resumes a completed candidate;
it does not silently reset the three-cycle budget.

Both studios use `apps/api/src/http/studioEvents.ts` for ordered SSE replay and
reconnect/authentication behavior, with separate notification channels. Stage,
check, candidate and composer-progress events remain with each request. History
snapshots omit worker-only source/configuration checkpoints; account exports
include them without lease credentials. Account deletion fences workers and
returns unfinished reservations before removing authoring history.

Delivered revisions use existing music-library authorization and playback.
Publishing requires confirmation of the license selected before generation; it
exposes only that release and final check results. License and attribution are
embedded in Opus tags before validation, preserving the native three-file ZIP
format. Uploaded audio cannot replace generated revisions. Withdrawing a release
uses existing library controls; it does not reveal private authoring artifacts.

Deleting a Music Studio conversation removes its private source, messages, candidates
and event history; finished releases remain independently managed in the music
library. Free-chat usage is retained as zero-credit ledger entries and anonymous
daily provider counters survive history/account deletion. Private pipeline source
fingerprints cover worker tools, Python sources and dependency/asset definitions,
and fence recovery across an incompatible worker upgrade. Successful history deletion
also clears that conversation's browser draft and checkout-return state; other projects
are retained.
