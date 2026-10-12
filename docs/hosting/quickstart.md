# Run an online instance

Install and start the Compose stack. Run these commands from `deploy/` after
cloning the repository. Use a disposable local instance for the first trial.

The local defaults bind to loopback and use a local certificate authority. Before
exposing the service, configure [DNS/TLS](networking.md), [provider sign-in](security.md)
and [backups](backup-restore.md). Generate your own secrets; example values are
placeholders.

## Requirements

- Docker Engine 25 or newer with Compose v2.24 or newer.
- 2 CPU cores and 4 GB of memory for a small instance; engine jobs (map generation,
  verification) are the heaviest load and scale with `engine-agent` replicas.
- Building the images yourself compiles the game: allow 30-60 minutes and about
  10 GB of disk for the build cache. Prebuilt images avoid this (see
  [Images](upgrades.md#images)).
- For a public instance: a DNS name, and inbound TCP 80 and 443 (UDP 443 for HTTP/3).


## Setup from zero

```sh
git clone https://github.com/Globulation2/glob2.git glob2
cd glob2/deploy
cp .env.example .env
cp ../platform/instance.example.yaml instance.yaml
```

1. Edit `.env`. At least:
   - `POSTGRES_PASSWORD`: letters and digits only, e.g. `openssl rand -hex 24`;
   - for a local trial keep the defaults (`https://localhost:8443`); for a public
     instance see [DNS and TLS](networking.md#dns-and-tls).
2. Edit `instance.yaml`: the instance name, sign-in providers (the example enables
   Google, which needs `GOOGLE_CLIENT_SECRET` in `.env`; remove it or see
   [Sign-in providers](security.md#sign-in-providers)), whether guests may play, and the
   quick-match queues.
3. Optionally build the WebAssembly client to serve "Play in browser" at `/play/`:
   `./build-web-client.sh <served-dir>` builds it with the pinned Emscripten
   SDK in a container (`scons target=web release=1 web-package`, see
   [browser/README.md](../../browser/README.md)), writes the Brotli and gzip copies
   (`browser/precompress.py`) and installs the result into `<served-dir>`; set
   `GLOB2_WEB_CLIENT_DIR` in `.env` to that directory. Caddy serves it with the
   cross-origin isolation headers the threaded client needs. Without it `/play/`
   answers 404 and everything else works.
4. Build and start:

   ```sh
   echo "GLOB2_SIM_VERSION=$(python3 sim_version.py)" >> .env   # labels the engine-agent image
   docker compose build
   docker compose up -d --wait
   ```

   On the first start `init` writes a signing key and the relay key into their
   volumes and creates the database schema. `docker compose logs init` shows what
   it did.

5. Open the origin (`https://localhost:8443` locally). For a local instance, trust
   Caddy's local CA in the browser or device you test with:

   ```sh
   docker compose exec caddy cat /data/caddy/pki/authorities/local/root.crt > glob2-local-ca.crt
   ```

6. Make yourself an administrator: sign in once with a provider or a local account
   (guests cannot be administrators), then

   ```sh
   docker compose run --rm --no-deps platform-api platform admin grant "<display name or account id>"
   ```

7. Check the instance: `<public-origin>/api/v1/instance` lists the sim version
   your engine agent serves; a desktop client pointed at the origin can sign in.

`test/deployment/platform_stack_smoke.py` performs these steps in an isolated
project and checks the result; it is a good first test of a new host as well.

[Hosting index](README.md) · [Documentation index](../README.md).
