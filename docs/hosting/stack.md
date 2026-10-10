# Deployment stack and permissions

Services, networks, durable volumes and database roles in the Compose deployment.

## The stack

```
             internet ── 80/443 ──► caddy ─┬─ /api /realtime /signin /auth /.well-known /j ─► platform-api ×N ─┐
                                           ├─ /relay/<relay id> (WebSocket) ──────────────► relay ×N ──────────┤ /internal (backend only)
                                           ├─ /play/  WebAssembly client (static)                              │
                                           └─ /       web app (static)                                         ▼
  platform-worker ×1..N ──► postgres 16 ◄── platform-api ◄── /internal/v1/engine ── engine-agent ×N (one image per sim version)
         └──────────────── blob volume ────────────┘          (no database, no blob volume)
```

| Service | Image (Dockerfile target) | Replicas | Role |
| --- | --- | --- | --- |
| `caddy` | `caddy` | 1 | TLS (ACME or local CA), static web app and web client, routing. The only service with published ports. |
| `postgres` | `postgres:16-alpine` | 1 | Structured state, the job queue, pub/sub and the matchmaker's leader lock. |
| `init` | `platform` | one-shot | Creates the first signing key, the relay and engine-agent keys and the database role passwords, creates or updates the [database roles](stack.md#database-roles) as the Postgres superuser, then applies migrations as `glob2_migrator`. Runs before the platform starts on every `up`; the only process that uses the superuser. |
| `platform-api` | `platform` | `GLOB2_API_REPLICAS` (2) | REST, realtime WebSocket, sign-in pages, JWKS, and `/internal` for relays. Stateless. |
| `skin-render-worker` | `skin-render-worker` | 1 | Transparent colony sprite generation using the current client, meshes, Mesa/llvmpipe and Xvfb. |
| `music-worker` | `music-worker` | 1 | Community music inspection, conversion and final media storage. |
| `platform-worker` | `platform` | `GLOB2_WORKER_REPLICAS` (1) | Engine-job results, ratings, matchmaker and schedules (the scheduler runs on one replica at a time). |
| `engine-agent` | `engine-agent` | `GLOB2_ENGINE_AGENT_REPLICAS` (1) | Map generation, validation, previews and match verification with the headless `glob2` binary of one sim version. It runs the engine on uploaded files, so it has no database access and no blob volume: it leases jobs and moves blobs through `platform-api`'s `/internal/v1/engine` with a bearer agent key. |
| `relay` | `relay` | `GLOB2_RELAY_REPLICAS` (1) | Match WebSockets ([relay](../multiplayer/relay.md)). |

Networks:

- `backend` is internal (no route to the internet). Every service is on it; Caddy
  has a fixed address there (`GLOB2_PROXY_ADDRESS`) that relays trust for
  `X-Forwarded-For`. Automatic service addresses come from
  `GLOB2_BACKEND_IP_RANGE`, which must be inside the backend subnet and exclude
  the fixed proxy address so startup order cannot allocate it to another service.
  Changing the address pool requires recreating the backend network: schedule
  downtime, stop the stack with `docker compose down` (without `--volumes`), then
  start it again. Existing data volumes are retained.
- `egress` gives `platform-api` outbound access for sign-in providers (OIDC
  discovery, token exchange, Apple keys).
- `public` carries Caddy's published ports and its ACME traffic.

Volumes (Compose project `glob2-platform`, so named `glob2-platform_<volume>`):

| Volume | Contents | Back up? |
| --- | --- | --- |
| `postgres-data` | The database | Yes, with `pg_dump` ([backup guide](backup-restore.md)) |
| `blobs` | Maps, saves, previews, match records, replays and community music (content-addressed) | Yes |
| `signing-keys` | Ed25519 private keys (`<kid>.pem`) for access tokens and match tickets | Yes, encrypted |
| `relay-secret` | `relay.key`, the bearer key relays use on `/internal` | Yes, encrypted (or regenerate) |
| `engine-agent-secret` | `agent.key`, the bearer key engine agents use on `/internal/v1/engine` | Optional (regenerate: delete it and `up`) |
| `db-migrator-secret`, `db-api-secret`, `db-worker-secret` | `password` of `glob2_migrator`, `glob2_api` and `glob2_worker`, each mounted only by the service that uses it | Optional (regenerate: delete it and `up`) |
| `relay-spool` | Match records a relay has not uploaded yet, one directory per relay id (`relay-1`, `relay-2`, …), and the relays' slot locks | Optional |
| `caddy-data`, `caddy-config` | Certificates, ACME account, local CA | Optional (Caddy re-issues) |

### Database roles

No long-running service connects as a Postgres superuser. `POSTGRES_PASSWORD` is the
superuser's password (the `postgres` image's `glob2`); only `postgres` itself and
`init` see it, and it is blanked in the other services' environment.

| Role | Used by | Privileges |
| --- | --- | --- |
| `glob2` (superuser) | `init` only | Creates the roles below, sets their passwords, hands a database created before roles existed to `glob2_migrator`. |
| `glob2_migrator` | `init` | Owns every table, view, function and type in `public` and `graphile_worker`; runs the platform's and graphile-worker's migrations, then re-applies the grants. Not a superuser. |
| `glob2_api` | `platform-api`, the `platform` CLI | `SELECT`/`INSERT`/`UPDATE`/`DELETE` on the platform tables and the job queue; no DDL, no `TRUNCATE`. The admin audit log is append-only (`scrub_audit_log_account()`, owned by the migrator, is the one change allowed: account deletion). |
| `glob2_worker` | `platform-worker` | As `glob2_api`, minus `identities`, `device_credentials` and `admin_audit_log`; on `refresh_tokens`, `web_sessions` and `auth_flows` only the retention deletes. |
| none | `engine-agent` | Uses `platform-api`'s internal engine API instead. |

`init` writes a random password per role into its own volume (`db-<role>-secret`);
each service mounts only its own and reads it through `DATABASE_PASSWORD_FILE`. To
rotate a role's password, delete its `password` file
(`docker compose run --rm --no-deps init rm /var/lib/glob2/db/api/password`) and run
`docker compose up -d`: `init` writes a new one and updates the role. `glob2-migrate
roles` and `latest` (`packages/db/src/cli.ts`) do the same by hand.

The engine agent's key (`engine-agent-secret`, `agent.key`) lets it announce
itself, lease jobs of its sim version, read only the blobs a job it holds names,
store results and report them. A key written `<agentId>:<key>` in
`ENGINE_AGENT_KEYS` acts only as that agent.

All services except Postgres run as UID/GID 10001 with a read-only root
filesystem, all capabilities dropped and `no-new-privileges`; Postgres runs as its
own UID 70 under the same restrictions. Health checks: `platform-api` `/readyz`
(database reachable and the realtime LISTEN connection up), `relay` `/readyz`
(ticket keys loaded, not draining), Caddy `/livez`, Postgres `pg_isready`.

[Hosting index](README.md) · [Documentation index](../README.md).
