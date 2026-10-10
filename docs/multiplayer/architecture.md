# Online platform architecture

The online platform connects clients, match relays and engine agents. Start here for component responsibilities, trust boundaries and simulation versions.

## Components

```
 clients (desktop / mobile / browser)
   │ HTTPS + realtime WebSocket (JSON)          │ match WebSocket (binary turns)
   ▼                                            ▼
 platform-api (TypeScript, stateless, N)  ◄──► relay (C++, N, version-agnostic)
   │ Postgres: state, job queue, LISTEN/NOTIFY       │ uploads the match record
   │ blob store: durable fs volume             │
   ▼                                                  │
 platform-worker (TypeScript): result intake, scheduler, matchmaker, ratings
 engine-agent (TypeScript + glob2 headless, one image per sim version):
   generate-map · validate-map · render-preview · verify-match · validate-ai · validate-buildings
 Caddy: TLS, static web client and web app, /api, /realtime, /relay
```

| Part | Code | Role |
| --- | --- | --- |
| `platform-api` | `platform/apps/api` | Public REST (`/api/v1`), realtime WebSocket (`/realtime`), browser sign-in pages (`/signin`, `/auth/<provider>/…`), JWKS (`/.well-known/jwks.json`), internal endpoints for relays and agents (`/internal`), health (`/healthz`, `/readyz`). Stateless; run any number of replicas. |
| `platform-worker` | `platform/apps/worker` | Applies engine-job results (recording verify-match verdicts and history, applying ratings, completing map jobs); runs the scheduler (maintenance, matchmaker, rating sweep, warm map pool, relay sweep) on the one replica holding the leader lock. The process code only: the domain logic it runs is in `@glob2/play`. |
| `engine-agent` | `platform/apps/engine-agent` | Runs engine jobs for exactly one sim version with its glob2 binary; see [Engine agents](engine-agents.md#engine-agents). |
| web app | `platform/apps/web` | Home, leaderboards, player and match pages, map catalog, moderation (React + Vite); see [match history and the web app](history-and-web.md). Sign-in and invite pages are rendered by `platform-api`. |
| relay | `src/relay/` | Clock and turn sequencing for matches; trusts only signed tickets. |
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
- **Postgres owns structured service state.** It provides the job queue
  (graphile-worker), cross-replica pub/sub (LISTEN/NOTIFY) and leader election
  (advisory locks). There is no Redis, so a self-hosted instance runs one
  database, a durable blob volume, secret volumes and the services above.
- **Every action that admits play goes through `AccessPolicy`.** It is the only
  hook for restricting admission; see [access policy](contracts.md#accesspolicy).


## Simulation versions

A sim version is the triple `VERSION_MINOR` + `NET_PROTOCOL_VERSION` (both in
`src/app/Version.h`) + the build's simulation hash (SHA-256 over `SIM_REVISION` from
`src/game/SimRevision.h` and the data files that affect simulation, computed by the
engine and by `deploy/sim_version.py`). Two builds with the same sim version must
produce identical games; every simulation change bumps `SIM_REVISION`
([turn protocol](turn-engine.md#simulation-version)).

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

[Multiplayer index](README.md) · [Documentation index](../README.md).
