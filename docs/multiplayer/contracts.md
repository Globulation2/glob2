# Platform contracts and coordination

Reference for JSON contracts, stored state and cross-replica coordination. Read the [architecture](architecture.md) first.

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
_semantic_ checks (`matchSetupProblems()`) and a document is valid only if it
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
| `seats[i]` (numbered 0..k-1) | `BasePlayer` _i_: `human` → `P_IP` on every client and in the verifier (as networked games do today, so heavy checksums agree), `ai` → `P_AI + id`; `name`, `team`; `setNumberOfPlayers(k)` |
| `seats[i].aiConfig` | `setAIConfig(i, …)` |
| `teams[t].alliance` (teams listed 0..n-1, n = map team count) | `setAllyTeamNumber(t, alliance + 1)` |
| `rules.prestigeVictory`, `rules.suddenDeathMinutes` | prestige and sudden-death winning conditions (minutes × 60 × 30 ticks) |
| `rules.mapDiscovered`, `rules.allyTeamsFixed` | `setMapDiscovered`, `setAllyTeamsFixed` |
| economy and combat rules | the setter of the same name (`setResourceScarcityLevel`, …) |
| `rules.aiOrderDelay` (optional integer 0–8, new matches use 8; absent means 0) | `setAIOrderDelay`; one delay for every AI controller in the match |
| `rules.buildingGradientDelay` (optional integer 1–8; absent means 8) | `setBuildingGradientDelay`; ticks between a building walking-field capture and its publication |
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
| Maps | `maps` (owner, visibility, moderation, counters, latest version), `map_versions` (content hash, size, dimensions, team count, preview, validation), `map_likes`, `map_reports`, `map_downloads`; see [Map catalog](engine-agents.md#map-catalog) |

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
  through `platform-api` ([Engine agents](engine-agents.md#engine-agents)). Its report is enqueued
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

[Multiplayer index](README.md) · [Documentation index](../README.md).
