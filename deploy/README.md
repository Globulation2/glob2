# Deployment files

This directory holds the self-hosted online platform stack: accounts, rooms,
quick match, match relays, engine agents and the web app. The default service images are built
from `Dockerfile`; optional authoring workers use their focused build definitions. The [self-hosting guide](../docs/hosting/README.md)
explains how to run, configure, upgrade and back it up.

| File | Purpose |
| --- | --- |
| `compose.yaml`, `.env.example` | The Compose stack and its settings |
| `Caddyfile` | Public edge: TLS, the web app, `/play/`, the API and relay routes |
| `Dockerfile` | Default images: `platform`, `music-worker`, `skin-render-worker`, `engine-agent`, `relay` and `caddy` |
| `platform-init.sh`, `relay-entrypoint.sh`, `engine-agent-entrypoint.sh` | Container start-up: keys, relay identity, engine checks |
| `sim_version.py` | Prints a source tree's sim version key (engine-agent image label) |
| `update-host.sh` | Upgrades a single-host deployment from this checkout |
| `online-deploy.sh`, `online_remote.py` | Automatic deployment of the official instance from the release mirror (`.github/workflows/deploy-online.yml`): host-side driver and IAP SSH helper ([details](../docs/hosting/upgrades.md#automatic-deployment)) |
| `build-web-client.sh`, `install-web-client.py` | Build the WebAssembly client in a container and install it where Caddy serves `/play/` |
| `source-identity.sh` | Docker build helper: gives the copied source tree a Git identity |
| `provision_tls.py` | Creates an isolated private CA and service certificates. The transport and relay tests use it for local TLS fixtures. |

`.github/workflows/server-image.yml` publishes the default service images for a `server-v*` tag
in the release mirror only ([details](../docs/hosting/upgrades.md#images)).
Deployment script tests: `python3 -m unittest discover -s test/deployment -v`.
Whole-stack smoke test: `test/deployment/platform_stack_smoke.py` (see the
hosting guide).

[Hosting index](../docs/hosting/README.md) · [Documentation index](../docs/README.md).
