# Hosting the secure YOG services

The deployment runs one authoritative lobby, one game router, and a public Caddy
proxy. Both backends implement WSS directly. Caddy terminates public TLS and
uses verified TLS again to each backend; private router registration uses mutual
TLS. No WebSocket-to-TCP gateway is required.

This is a deployment foundation, not a distributed lobby or match-recovery
system. Accounts/maps retain the legacy file storage and password hashing.
That hashing remains an unresolved security limitation for a public service;
transport/container qualification alone does not qualify the complete account
system. Active sessions and games do not survive backend restarts.

## Local deployment

Build the browser assets using `browser/README.md`, and provision isolated TLS
secrets before starting services:

```sh
scons target=web release=1 -j4
python3 browser/package-static.py
python3 deploy/provision_tls.py deploy/secrets
docker compose -f deploy/compose.yaml build lobby web
docker compose -f deploy/compose.yaml up -d --wait
```

Open https://localhost:8443. Caddy creates a local CA for localhost; install that
CA in the testing browser/device trust store. Do not bypass verification in a
production client. Its public CA is separate from the private deployment CA in
`deploy/secrets`. Run `python3 browser/package-static.py` after building the
browser client. Browser assets default to `build/browser-static`, whose verified
gzip sidecars Caddy negotiates for HTML, JavaScript, WebAssembly and data;
`GLOB2_ASSETS` overrides the mounted directory.

The provisioning command refuses to overwrite existing material. The directory
is owner-only; file-backed Compose secrets must be readable by container UID
10001. The private CA key is owner-readable only and is never mounted into a
service. Keep it offline for production issuance. Never commit TLS material;
`deploy/secrets` is ignored and excluded from image builds. Use your cloud secret
manager instead of local files outside Compose.

Only the proxy publishes ports. HTTP redirects public clients to HTTPS; game
listeners never accept plaintext. Private `/register`, `/metrics`, `/livez`, and
`/readyz` routes are denied at the public proxy.

## Public configuration

After configuring DNS, use an environment file, passed with `--env-file` to each
Compose command:

```dotenv
GLOB2_SITE=games.example.org
GLOB2_ORIGIN=https://games.example.org
GLOB2_LOBBY_ENDPOINT=wss://games.example.org/yog
GLOB2_ROUTER_ENDPOINT=wss://games.example.org/router
GLOB2_BIND=0.0.0.0
GLOB2_HTTP_PORT=80
GLOB2_HTTPS_PORT=443
GLOB2_SERVER_IMAGE=ghcr.io/OWNER/REPOSITORY-server:server-vVERSION
GLOB2_PROXY_IMAGE=ghcr.io/OWNER/REPOSITORY-proxy:server-vVERSION
```

Caddy obtains/renews public certificates, with persistent `/data` and `/config`
volumes. Allow incoming TCP 80/443 and public certificate issuance egress at the
proxy. Do not expose lobby 7489, registration 7490, router 7491, or control
7492/7493. Server processes need no public egress in this deployment.

A `server-v*` Git tag triggers `.github/workflows/server-image.yml`, publishing
Linux amd64/arm64 server and proxy images with version and full-commit tags, provenance, and SBOM.
Use the resulting immutable digest for production and record it with the client
revision. The default `glob2-server:development` and `glob2-proxy:development` images are
intended for local builds. All three services run as UID 10001 with a read-only
application filesystem, dropped capabilities, and no privilege escalation.
Caddy writes only its explicit `/data`, `/config`, and temporary volumes; its
namespace permits HTTP/TLS listener ports without a binding capability. Before
upgrading existing root-owned proxy volumes, stop the proxy, back up `/data`
and `/config`, and change their ownership to UID/GID 10001 using an administrative
volume helper. Preserve those permissions when restoring backups. Publishing or qualifying a release still requires repository registry
access and successful deployment/cross-platform checks.

## Native process settings

| Setting | Purpose/default |
| --- | --- |
| `GLOB2_TLS_CERT`, `GLOB2_TLS_KEY`, `GLOB2_TLS_CA` | Required PEM server identity and private deployment CA files |
| `GLOB2_BIND_ADDRESS` | Game-listener address; `0.0.0.0` |
| `GLOB2_LOBBY_PORT`, `GLOB2_REGISTRATION_PORT`, `GLOB2_ROUTER_PORT` | Listener ports; 7489, 7490, 7491 |
| `GLOB2_PUBLIC_LOBBY_ENDPOINT`, `GLOB2_PUBLIC_ROUTER_ENDPOINT` | Complete advertised WSS URLs |
| `GLOB2_REGISTRATION_ENDPOINT` | Private WSS lobby registration URL |
| `GLOB2_ALLOWED_ORIGINS` | Comma-separated exact browser Origins; empty rejects browser Origins |
| `GLOB2_TRUSTED_PROXY_ADDRESSES` | Exact trusted proxy IP addresses; empty ignores forwarding headers |
| `GLOB2_CONNECTION_LIMIT` | Connections per listener, including pending handshakes; 256 |
| `GLOB2_CONTROL_BIND`, `GLOB2_CONTROL_PORT` | Private HTTP control address/port; loopback, 7492 lobby or 7493 router |
| `GLOB2_USER_DATA_DIR` | Absolute writable persistence path; container `/var/lib/glob2` |
| `GLOB2_DRAIN_SECONDS` | Shutdown deadline; 1800 seconds |
| `GLOB2_EXTERNAL_ROUTER` | `1` disables the embedded router in the lobby |

Compose assigns the proxy a fixed private IP and trusts forwarding headers only
from that peer. Caddy replaces X-Forwarded-For with its verified client address;
publicly supplied forwarding headers cannot spoof IP bans. Override
`GLOB2_BACKEND_SUBNET` and `GLOB2_PROXY_ADDRESS` together if the private subnet
conflicts with your host. When Caddy sits behind a cloud load balancer, set
`GLOB2_EDGE_TRUSTED_PROXIES` to that balancer's managed source CIDRs; Caddy's
strict forwarding-chain policy then derives the original client address. Never
trust arbitrary public CIDRs. See [Caddy's trusted proxy policy](https://caddyserver.com/docs/caddyfile/options#trusted-proxies).
Other deployments must configure exact trusted proxy addresses explicitly; native direct clients cannot set their own peer identity.

Public clients can omit Origin. Supplied browser Origins must be allowlisted;
HTTPS Origins are required except explicit localhost/127.0.0.1 HTTP development
Origins. Router registration accepts only certificates issued by the private
CA and the matching network protocol. One registered router is admitted for this
milestone; horizontal router routing and autoscaling remain future work.

The container runs as UID/GID 10001, with a read-only application filesystem,
private writable data volumes, a bounded temporary filesystem, dropped
capabilities, and no privilege escalation. Lobby state has an exclusive OS lock
acquired before registry/map loading. Use one lobby replica and a volume whose
filesystem supports exclusive locks; shared-volume replicas are unsupported.

## Health and shutdown

Private HTTP `/livez` reports process-loop responsiveness. `/readyz` reports
initialized dependencies and admission readiness: a lobby requires a registered
router, and a router requires its lobby connection. `/metrics` exports readiness,
draining state, connections, and games. These HTTP management endpoints stay on
a private network; they are not multiplayer data channels.

Compose probes lobby liveness to avoid a dependency cycle while starting the
router, then probes router readiness. Cloud routing should use `/readyz` to
control admission. Loss of a router makes the lobby unready and refuses new
rooms. Existing sessions are not recovered automatically.

SIGTERM/SIGINT makes the service unready and stops admitting connections and new
games. Existing matches can finish; persistent registry, player metadata, maps,
and logs are flushed before lobby exit. After the deadline, remaining sessions
are interrupted and the process logs that limit. Configure the orchestrator's
termination grace longer than the drain deadline; Compose defaults to 31 minutes.
During upgrades, drain the lobby before stopping its router. Router shutdown
withdraws its registration while allowing its current connections to finish.

Keep the public proxy running until both backends have exited. For a coordinated
stop or upgrade with the default 30-minute deadline, run these commands in order:

```sh
docker compose -f deploy/compose.yaml stop --timeout 1860 lobby
docker compose -f deploy/compose.yaml stop --timeout 1860 router
docker compose -f deploy/compose.yaml stop web
# Replace images/secrets or take the consistent backup, then restart:
docker compose -f deploy/compose.yaml up -d
```

Increase the stop timeout when increasing `GLOB2_DRAIN_SECONDS`. A whole-stack
`docker compose down` or simultaneous service termination is an interrupting
shutdown: the proxy may close active WebSockets before backend draining finishes.
Cloud rolling upgrades must retain the proxy/load-balancer connections until the
backend drain completes, then replace that proxy instance. Backend drain tests
do not qualify simultaneous proxy shutdown as preserving matches.

A cloud load balancer must support WebSocket upgrades and long-lived connections.
Use TLS passthrough to Caddy, or a balancer that verifies its TLS upstream's
certificate chain and DNS identity. Re-encryption without certificate validation
does not meet the transport trust requirement. Keep the readiness/metrics route
private, with only the load balancer/orchestrator allowed to reach it.
Set its idle timeout above the 30-second WebSocket keepalive interval. Existing
connections stay attached to their backend until closed; readiness alone cannot
migrate them. Scale-to-zero, multiple lobby replicas, and arbitrary router
replication are unsupported in this milestone.

## Persistence, backup, certificates, and cutover

`lobby-data` contains the existing registry, metadata, uploaded maps, and logs
under `/var/lib/glob2`; `router-data` contains router configuration/logs. Caddy
volumes contain public certificate keys and configuration. `compose down` retains
volumes; `--volumes` erases them. When upgrading the older development package,
copy its `/home/glob2/.glob2` contents into the new lobby data volume while both
deployments are stopped. Preserve ownership for UID 10001.

For a consistent backup, mark the lobby draining, wait for it to exit, stop the
router, and archive the data volumes and exact image/client identifiers. Copying
live registry/map files is not a consistent backup. Restore into an isolated
stack, verify registration/login, map transfer, and a match, then switch traffic.
Preserve volume ownership as UID/GID 10001 when restoring. Tools such as
`docker cp` can assign root ownership to restored files; correct ownership before
starting the lobby and verify that its private readiness endpoint succeeds.
Transport changes do not migrate the existing account/map formats.

Private service certificates expire after 90 days. Issue replacements into a new
secret directory using the original offline CA (or your production PKI), verify
SANs for service DNS names and both serverAuth/clientAuth usages, then drain and
recreate services. To rotate a CA, first deploy a trust bundle containing both
roots, replace service identities, then retire the old root. The provisioning
helper creates a fresh isolated CA; it does not perform in-place rotation.

Network protocol 49 needs a coordinated client/server cutover. Do not fall back
to raw TCP or deploy mixed protocol versions. Keep an isolated backup for
rollback; stop/drain before replacing images. The default public YOG hostname
must pass certificate, login, map-transfer, and cross-play qualification before
release; local tests do not establish that external service is ready.

## Deployment regression

```sh
python3 -m unittest discover -s tests/deployment -v
```

The test uses an isolated Compose project, secrets, and disposable volumes. It
verifies TLS with the local Caddy CA, native WSS forwarding, private-route denial,
registration/login, persistence across recreation, and room refusal/recovery
after router loss. Review artifacts belong under ignored `artifacts/`, not in
committed documentation.
