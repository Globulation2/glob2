# Online platform architecture

Globulation 2's online play is being rebuilt on a new foundation: a TypeScript
platform service for accounts, rooms, matches, ratings and maps, and a C++ relay
that sequences turns. This guide describes the design, the contracts between the
parts, and how to work on the platform. Identity is covered in
[identity](identity.md). The binary turn protocol between clients and relays is
owned by the turn-netcode work and documented in `docs/multiplayer/turn-protocol.md`.

The legacy YOG lobby, router and LAN code keep working unchanged until the
cutover milestone (M9), when they are deleted. There is no data import from YOG.

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
   generate-map · validate-map · render-preview · verify-match
 Caddy: TLS, static web client and web app, /api, /realtime, /relay
```

| Part | Code | Role |
| --- | --- | --- |
| `platform-api` | `platform/apps/api` | Public REST (`/api/v1`), realtime WebSocket (`/realtime`), internal endpoints for relays and agents (`/internal`), health (`/healthz`, `/readyz`). Stateless; run any number of replicas. |
| `platform-worker` | `platform/apps/worker` | Applies engine-job results; runs the scheduler (maintenance now, matchmaker and rating sweeps later) on the one replica holding the leader lock. |
| `engine-agent` | `platform/apps/engine-agent` | Runs engine jobs for exactly one sim version. |
| web app | `platform/apps/web` | Sign-in pages, invite landing, profiles, leaderboards, maps (React + Vite). |
| relay | `src/relay/` (M2) | Clock and turn sequencing for matches; trusts only signed tickets. |
| contracts | `platform/packages/protocol` | Every JSON shape, exported as JSON Schema with fixtures for C++. |
| data | `platform/packages/db` | SQL migrations, typed Kysely access, pub/sub, leader lock. |
| plumbing | `platform/packages/core` | Configuration, logging, AccessPolicy, blob store, job queue, shutdown. |

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
`src/Version.h`) + the build's simulation data hash (SHA-256 over the data files
that affect simulation, computed by the engine). Two builds with the same sim
version must produce identical games.

- Protocol schema `SimVersion` is `{versionMinor, netProtocol, dataHash}`; its
  canonical string key is `simVersionKey()`: `<minor>-<net>-<dataHash>`, used in
  database columns (domain `sim_version_key`), engine task identifiers and URLs.
- Rooms, queue tickets, matches and AI rating entities carry a sim version.
  Players are only ever grouped with others of the same version.
- An instance serves the versions it has engine agents for: agents register in
  `engine_agents` and `GET /api/v1/instance` lists every version with an agent
  seen in the last five minutes. A client whose version is not served gets
  `simSupported: false` from `session.hello` (it can still sign in) and
  `update_required` from room, queue and match requests.
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

## AccessPolicy

`AccessPolicy` (`platform/packages/core/src/accessPolicy.ts`) has three checks,
`canHost`, `canJoin` and `canQueue`, each returning allowed or a denial with a
reason (mapped to the `access_denied` error code). The only implementation is
`allow-all`, selected in `instance.yaml`. An `entitlements` table exists and stays
empty; match tickets carry an `entitlements` claim that relays ignore. Product
rules that are not about access, such as keeping guests out of rated queues,
belong to the feature that owns them, not to the policy.

## Data model

Migrations are forward-only plain SQL in `platform/packages/db/migrations/`,
applied in order by `glob2-migrate` (`npm run migrate -- latest`); the Kysely
types in `src/schema.ts` are checked column for column against the migrated
database in tests.

| Area | Tables |
| --- | --- |
| Identity | `accounts`, `identities`, `device_credentials`, `refresh_tokens`, `signin_attempts`, `entitlements`, `admin_audit_log` |
| Infrastructure | `blobs`, `relays`, `engine_agents`, `engine_jobs` |
| Rooms | `rooms` (settings JSON, revision), `room_members`, `room_seats`, `room_chat_messages` |
| Matches | `matches` (the exact `MatchSetup`, seed, map hash, relay, verification), `match_participants`, `match_team_stats`, `match_artifacts` |
| Ratings | `rating_entities` (an account, or an AI at one sim version), `ratings` (OpenSkill μ/σ per ladder, ordinal generated) |
| Quick match | `queue_tickets` (one waiting ticket per account) |
| Maps | `maps`, `map_versions` (content hash, size, dimensions, team count, preview), `map_likes`, `map_reports` |

Hashes are lowercase hex (`sha256_hex` domain), ids are UUIDs, and enumerations
are text with CHECK constraints so they can grow without type migrations.

## Coordination

- **Job queue.** graphile-worker tables in the same database. Engine jobs use
  the task identifier `engine:<kind>:<simVersionKey>`, so an agent only ever
  receives jobs for its own version. `submitEngineJob()` records the job in
  `engine_jobs` and enqueues it; the agent enqueues its result under
  `platform:engine-job-result`; the worker validates it against the kind's result
  schema and completes the row (a result that breaks the contract is recorded as
  a failure). Deterministic engine failures are reported, other errors retry.
- **Pub/sub.** `PgPubSub` keeps one listening connection per process,
  reconnects with backoff and re-listens; payloads are limited to 8000 bytes, so
  publish identifiers and re-read state. Delivery is at most once; `onReconnect`
  tells subscribers to re-read.
- **Leader lock.** `LeaderElection` holds a session advisory lock; if the leader
  dies Postgres releases the lock and another replica takes over within the
  retry interval.

## Configuration and operation

Each service reads secrets and deployment settings from the environment (or a
`.env` file; see `platform/.env.example`) and instance settings from
`instance.yaml` (see `platform/instance.example.yaml`): name, guest access,
sign-in providers (secrets referenced by environment-variable name), access
policy and queues. Services log structured JSON to stdout, expose health
endpoints where they serve HTTP, and on SIGTERM stop taking work, finish what is
running and close connections within `SHUTDOWN_GRACE_SECONDS`.

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
native jobs, which hold the C++ contract tests.

## Delivery milestones

Each milestone is one or more reviewable pull requests; YOG keeps working until M9.

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
| M9 | Cutover | Delete YOG, IRC and the router role; update docs |

The original plan referred to `src/net/gateway/` for server patterns; that
directory was removed when transport moved to native WSS, and its equivalents now
live in `src/net/NetTransport.cpp`, `src/net/WssTransport.cpp`,
`src/net/ServerControl.cpp` and `deploy/`.
