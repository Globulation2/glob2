# Deployment configuration

Environment settings and instance configuration reference. See [security](security.md) for sign-in providers and key rotation.

## Configuration

Everything is configured in `deploy/.env` (Compose variables, and the environment of
`platform-api`) and `deploy/instance.yaml`. `.env.example` lists every variable with
its default.

| Variable | Default | Meaning |
| --- | --- | --- |
| `GLOB2_DOMAIN` | `localhost` | Site name Caddy serves and certifies |
| `GLOB2_ADDITIONAL_DOMAIN` | unset | Optional second served hostname during cutover; does not change the canonical origin |
| `GLOB2_WEBSITE_URL`, `GLOB2_DOWNLOAD_URL` | unset | Web app links to a separately hosted public website and its download page (build-time) |
| `GLOB2_PUBLIC_ORIGIN` | `https://localhost:8443` | Origin clients use; also the relays' URL base |
| `GLOB2_BIND`, `GLOB2_HTTP_PORT`, `GLOB2_HTTPS_PORT` | `127.0.0.1`, `8080`, `8443` | Published address and ports |
| `GLOB2_TLS_MODE` | `auto` | `auto` or `internal` |
| `GLOB2_EDGE_TRUSTED_PROXIES` | `127.0.0.1/32` | Load balancer ranges Caddy trusts for client addresses |
| `POSTGRES_PASSWORD` | required | Password of the Postgres superuser (letters and digits); only `postgres` and `init` use it, the services have their own [roles](stack.md#database-roles) |
| `GLOB2_HSTS_MAX_AGE` | `31536000` | `Strict-Transport-Security` max-age Caddy sends (`0`: off) |
| `GLOB2_API_REPLICAS`, `GLOB2_WORKER_REPLICAS`, `GLOB2_ENGINE_AGENT_REPLICAS`, `GLOB2_RELAY_REPLICAS` | `2`, `1`, `1`, `1` | Replica counts |
| `ENGINE_CONCURRENCY`, `GLOB2_ENGINE_SCRATCH_SIZE` | `1`, `1g` | Jobs per agent, and its scratch tmpfs |
| `ENGINE_SET_VALIDATION` | `0` | Enable set publication checks after the Linux isolation probe succeeds ([rollout](content-validation.md#terrain-and-resource-set-validation-rollout)) |
| `ENGINE_SET_LIBRARY_PATH` | unset | Optional trusted shared-library directories for a custom validator build; add to the engine-agent environment in a Compose override |
| `WARM_MAPS_PER_ENTRY`, `WARM_MAPS_MAX_PER_ENTRY` | `2`, `8` | Pre-generated quick-match maps per map pool entry (0: off), and the ceiling the pool rises to while an entry is busy |
| `GLOB2_BACKUP_DIR`, `GLOB2_BACKUP_KEEP` | `backups/` beside the env file's directory, `5` | Where `deploy/update-host.sh` keeps its pre-upgrade backups, and how many |
| `GLOB2_BACKUP_BUCKET` | unset | Cloud Storage bucket of the [scheduled backups](backup-restore.md#scheduled-backups) |
| `GLOB2_DEPLOYED_REVISION_FILE` | `deployed-revision` beside the env file's directory | Where `deploy/update-host.sh` records the revision of each successful deployment, its rollback target |
| `GLOB2_RELAY_REGION` | `default` | Region these relays report |
| `GLOB2_RELAY_MAX_MATCHES` | `200` | Matches per relay |
| `GLOB2_RELAY_DRAIN_SECONDS`, `GLOB2_RELAY_STOP_GRACE` | `1800`, `31m` | Longest relay drain, and Compose's stop timeout (keep it longer) |
| `UPLOAD_MAX_BYTES`, `RECORD_MAX_BYTES` | 64 MiB, 64 MiB | Largest map/save upload, and largest match record |
| `RELAY_KEYS` | unset | Extra relay keys, `<relayId>:<key>` comma-separated (relays on other hosts, rotation) |
| `JWT_ACTIVE_KID` | unset | Signing key id, needed while several keys exist ([rotation](security.md#signing-keys-and-rotation)) |
| `LOG_LEVEL` | `info` | Platform log level |
| `GLOB2_WEB_CLIENT_DIR` | `./web-client` | Built WebAssembly client for `/play/` |
| `GLOB2_INSTANCE_CONFIG` | `./instance.yaml` | Instance settings file |
| `GLOB2_*_IMAGE` | local `:development` tags | Images to run ([Images](upgrades.md#images)) |
| `GLOB2_SIM_VERSION` | unset | Label for locally built engine-agent images; the build fails if it does not match the source |
| `GLOB2_BACKEND_SUBNET`, `GLOB2_BACKEND_IP_RANGE`, `GLOB2_PROXY_ADDRESS` | `172.30.89.0/24`, `172.30.89.128/25`, `172.30.89.10` | Backend subnet, automatic allocation pool, fixed proxy address; change together for a custom subnet, keeping the proxy outside the pool |
| provider secrets |  | Named in `instance.yaml`, e.g. `GOOGLE_CLIENT_SECRET` |

`instance.yaml` holds the settings players see: name, guests, sign-in providers,
local accounts, rate limits, the access policy and quick-match queues. Its format
is documented in [`platform/instance.example.yaml`](../../platform/instance.example.yaml)
and [ratings and matchmaking](../multiplayer/ratings-and-matchmaking.md). Restart
`platform-api` and `platform-worker` after editing it:
`docker compose up -d --force-recreate platform-api platform-worker`.

[Hosting index](README.md) · [Documentation index](../README.md).

## Disable unfinished paid features for a free launch

Set `enabled: false` and `salesEnabled: false` for `hiveMind`, `mapStudio`,
`musicStudio`, `terrainStudio`, `buildingStudio`, `aiStudio`, `generatorStudio`
and `skinDesigner`. Keep other required fields in existing configured blocks.
The example instance config already disables these services. An absent
`skinDesigner` block disables its designer and sales.

The API advertises enabled tools through `/api/v1/instance.features`. The web
app hides their navigation and authoring links and refuses to mount disabled
workspaces opened through direct URLs. Backend guards reject new studio edits,
generation, publication through the studio, skin drafting/design changes and
checkout, so hidden UI alone is not the enforcement boundary. Ordinary accounts,
multiplayer, catalogs, manual community publishing and existing skin equipment
remain available. Reads and deletion are not blocked by the new guard; existing
route authorization still applies. Account data export remains available.

Before changing a live instance, stop new sales, drain active creation/Commander
work and reconcile pending provider requests and credit reservations. Back up
the database and retain webhook credentials and payment reconciliation services
for earlier purchases. Disabled sales do not erase balances, entitlements,
projects or checkpoints. Creation worker shutdown is an operational step;
disabling API routes does not stop already queued worker jobs. Restart the API
with the reviewed config and smoke-test its advertised features and direct
requests before advertising the free launch.
