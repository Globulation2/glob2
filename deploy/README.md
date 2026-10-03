# Deployment files

This directory holds the self-hosted online platform stack: accounts, rooms,
quick match, match relays, engine agents and the web app. Everything is built
from one `Dockerfile`. The [self-hosting guide](../docs/hosting/README.md)
explains how to run, configure, upgrade and back it up.

| File | Purpose |
| --- | --- |
| `compose.yaml`, `.env.example` | The Compose stack and its settings |
| `Caddyfile` | Public edge: TLS, the web app, `/play/`, the API and relay routes |
| `Dockerfile` | Images `platform`, `engine-agent`, `relay` and `caddy` (see its header) |
| `platform-init.sh`, `relay-entrypoint.sh`, `engine-agent-entrypoint.sh` | Container start-up: keys, relay identity, engine checks |
| `sim_version.py` | Prints a source tree's sim version key (engine-agent image label) |
| `update-host.sh` | Upgrades a single-host deployment from this checkout |
| `online-deploy.sh`, `online_remote.py` | Automatic deployment of the official instance from the release mirror (`.github/workflows/deploy-online.yml`): host-side driver and IAP SSH helper ([details](../docs/hosting/README.md#automatic-deployment)) |
| `build-web-client.sh`, `install-web-client.py` | Build the WebAssembly client in a container and install it where Caddy serves `/play/` |
| `source-identity.sh` | Docker build helper: gives the copied source tree a Git identity |
| `provision_tls.py` | Creates an isolated private CA and service certificates. The transport and relay tests use it for local TLS fixtures. |

`.github/workflows/server-image.yml` publishes the four images for a `server-v*` tag.
Deployment script tests: `python3 -m unittest discover -s tests/deployment -v`.
Whole-stack smoke test: `tests/deployment/platform_stack_smoke.py` (see the
hosting guide).

The legacy YOG lobby and router stack (`compose.legacy.yaml`, `Caddyfile.legacy`,
the `server` and `proxy` images) was removed at the M9 cutover. There is no data
migration from it: the online platform starts with fresh accounts, ratings and
history.
