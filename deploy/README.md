# Self-hosting development package

This package runs the existing YOG lobby and router behind a fixed-route
WebSocket gateway and Caddy. It is experimental: the upgraded compatibility
handshake, modern account hashing, invitations, recovery, immutable release
artifacts, and upgrade/rollback qualification remain release gates.

## Local development

Build the browser using the pinned dependency setup in `browser/README.md`, then:

```sh
scons target=web release=1 -j4
docker compose -f deploy/compose.yaml build lobby
docker compose -f deploy/compose.yaml up -d --wait
```

Open http://localhost:8080. The game uses same-origin `/yog` and `/router` routes.
Only the proxy publishes ports. The lobby runs with `GLOB2_EXTERNAL_ROUTER=1`;
ordinary desktop/LAN callers continue to get an embedded router by default.
Stop with `docker compose -f deploy/compose.yaml down`. Named volumes survive.
Do not add `--volumes` unless you intend to erase this deployment's data.

The server image contains the lobby, router, and gateway from the same source
build. `GLOB2_SERVER_IMAGE` selects its image tag; the development default is
`glob2-server:development`. Browser files come from
`build/emscripten/client/release`; `GLOB2_ASSETS` overrides that directory. Keep
server and browser builds from the same revision. Release image publication and
content-addressed assets are still pending; these mutable development defaults
are not a versioned release package.

## HTTPS on a public host

Set these variables in `deploy/.env` (and pass `--env-file deploy/.env` to each
Compose command) after pointing your hostname at the server:

```dotenv
GLOB2_SITE=games.example.org
GLOB2_ORIGIN=https://games.example.org
GLOB2_BIND=0.0.0.0
GLOB2_HTTP_PORT=80
GLOB2_HTTPS_PORT=443
```

Caddy obtains and renews certificates. Its certificate/configuration volumes
persist across recreation. Allow incoming TCP 80/443 and outgoing certificate
issuance traffic. Do not expose internal TCP 7489/7490/7491 or gateway 8080.
Caddy forwards WebSocket upgrades using its [reverse proxy](https://caddyserver.com/docs/caddyfile/directives/reverse_proxy).
The gateway checks the exact browser Origin. No passwords or session credentials
belong in the URL or configuration script. The legacy account registry still
requires the planned password migration before supported internet deployment.

## Data and operation

- `lobby-data` contains `/home/glob2/.glob2`, including `beta4/registry`, account
  metadata, uploaded maps, configuration, and logs.
- `router-data` contains router configuration and logs. Active matches are memory
  only and do not survive a backend restart.
- `caddy-data` and `caddy-config` contain proxy state, including private keys.
- Container health checks test listening sockets/process responsiveness. They do
  not prove match recovery or end-to-end readiness. Room creation is refused when
  no router is available.
- Gateway `/metrics` is reachable only on the private network. Public requests
  for `/metrics` and `/healthz` return 404.

Before backup or replacing server images, stop admitting play operationally and
wait for matches to finish, then stop the services. Archive all named volumes
and record the exact image IDs and browser revision. Copying live registry/map
files is not a qualified consistent backup. Restore into a separate deployment
and verify login and maps before switching traffic. Automated draining, schema
migration, and a tested cross-version rollback command remain outstanding; do
not assume a newer registry can be read by an older server.

## Deployment regression

With the server image and browser output built:

```sh
python3 -m unittest discover -s tests/deployment -v
```

The test creates an isolated Compose project and volumes, uses ephemeral host
ports, and removes only its own project afterwards. It verifies HTTPS with the
local Caddy CA (certificate verification stays enabled), WSS forwarding, private
route denial, registration/login, persistence across container recreation, and
room refusal/recovery after router loss. It does not contact a public YOG server.
