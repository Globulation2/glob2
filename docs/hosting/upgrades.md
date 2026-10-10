# Upgrade and drain services

Upgrade images, retain simulation versions and drain active matches safely.

## Upgrades

Before every upgrade, take a [backup](backup-restore.md#backups-and-restore) and read the release
notes for migrations and sim-version changes. `deploy/update-host.sh`
takes the database dump and a copy of the served web client itself.

Run the manual Compose upgrade from `deploy/`:

```sh
git pull                       # or set new GLOB2_*_IMAGE digests in .env
docker compose build           # skip with prebuilt images: docker compose pull
docker compose up -d --wait
```

From the repository root, `deploy/update-host.sh <env-file> [git-ref]` does all of this on a single host in
one command, in an order that never leaves a half-upgraded instance:

1. **Backup.** A `pg_dump` and an archive of `GLOB2_WEB_CLIENT_DIR` go to
   `GLOB2_BACKUP_DIR/<UTC time>/` with the running revision; the newest
   `GLOB2_BACKUP_KEEP` are kept.
2. **Build**, while the old stack keeps serving: the WebAssembly client (with
   `deploy/build-web-client.sh`; Emscripten runs in a container, so the host needs
   only Docker) into the build tree, and every image with the checkout's sim
   version. The running images are first tagged `:previous`. A failed build
   changes nothing that runs.
3. **Swap:** `up --wait --force-recreate` (migrations run first, in `init`).
   Containers are recreated to attach them to any replaced networks. Profiles of optional
   services already running on the host are included in the build and swap, so
   they cannot be left behind when Compose replaces a network. A network address
   pool change still requires the downtime described in [network configuration](stack.md#the-stack).
4. **Web client last:** only once the new stack is healthy is the new client
   installed at `/play/`, so browsers never load a client newer than the platform
   and engine agents behind it. The `index.html`, `studio.html` and `generator-studio.html` entries
   are installed with their available precompressed copies. Localized launchers
   install `i18n.js` and every supported translation catalog before the HTML;
   incomplete localization inputs are rejected before changing the served client.

If the new stack does not become healthy, the script starts the `:previous`
images of the previous revision again, recreating containers to restore network
attachments, and leaves the web client as it was. The
previous revision is the one the last successful run recorded in
`GLOB2_DEPLOYED_REVISION_FILE`, not the checkout's `HEAD`, so checking out the new
ref before running the script (to run its newest version) is safe. Before the first
recorded deployment the script assumes `HEAD` runs; on an instance deployed by
hand, write the running commit to that file first. It
does not restore the database: migrations are forward-only and must keep the
previous release working (expand, then contract in a later release), so the
previous release normally runs on the new schema. When it does not, restore the
dump the script took; it prints the `pg_restore` command.

Migration numbers: parallel branches reserve ranges (a branch adds its files after
the highest number on the integration branch and leaves a gap for the others), so
files are applied in number order on a fresh database. Kysely's migrator refuses a
database that has a later migration applied but not an earlier one, so deploy
branches that add migrations in number order.

`up` runs `init` first, which updates the database roles and applies new migrations
(forward only, each in a transaction) before the new API and worker start. `platform-api` and
`platform-worker` replicas are replaced together: realtime clients reconnect after
a few seconds. Caddy keeps WebSockets open across its own configuration reloads.

### Upgrading to database roles

Run the Compose commands below from `deploy/`; invoke `deploy/update-host.sh`
from the repository root instead.

Instances deployed before the [database roles](stack.md#database-roles) have every object
owned by the superuser and every service connected as it. The upgrade is the usual
`up` (or `deploy/update-host.sh`) with the new `compose.yaml`; nothing in `.env`
changes:

1. Take a [backup](backup-restore.md#backups-and-restore).
2. `docker compose up -d --wait` (or `update-host.sh`). `init` generates the role
   passwords and the agent key, creates the roles, hands every table, view,
   sequence, function, type and the `graphile_worker` schema to `glob2_migrator`
   (idempotent, a few seconds), applies migrations 0010-0013 as `glob2_migrator`,
   then starts the services with their own roles. Migration 0010 moves queued
   engine jobs from graphile-worker to the `engine_jobs` lease queue: queued
   verify-match jobs run again (recovering matches whose verdict was lost), and
   queued map jobs older than an hour are closed as failed.
3. Check: `docker compose exec -T postgres psql -U glob2 -d glob2 -c "SELECT usename, application_name FROM pg_stat_activity WHERE datname = 'glob2'"`
   shows `glob2_api` and `glob2_worker` and no `glob2` sessions apart from your own,
   and `platform_stack_smoke.py --attach` passes.

Rolling back to a release without roles needs the superuser connection back:
restore the previous `compose.yaml` and its images; the services connect as `glob2`
again (a superuser can use objects `glob2_migrator` owns), and the extra columns
and tables of 0010-0013 are ignored by older code except that engine agents of the
older release take jobs from graphile-worker, where newer releases no longer put
them, so roll back the API and worker together with the agents.

### Draining relays

Run this procedure from `deploy/`.

A plain `up -d` with a new relay image replaces the relays at once and drains each
for up to `GLOB2_RELAY_DRAIN_SECONDS` while new matches have no relay. Replace them
without downtime instead:

```sh
old=$(docker compose ps -q relay)
docker compose up -d --no-recreate --scale relay=$((2 * ${GLOB2_RELAY_REPLICAS:-1})) relay   # new relays start, register
docker stop --timeout 1860 $old   # each old relay drains: no new matches, running matches finish
docker rm $old
docker compose up -d relay     # back to GLOB2_RELAY_REPLICAS, all on the new image
```

While draining, a relay reports `draining` in its heartbeat so the platform stops
allocating matches to it, and still accepts reconnects to its running matches.
Records not yet uploaded stay in `relay-spool` under the relay's id; the next relay
that starts with that id, or that finds the directory unclaimed, re-sends them.

### Sim versions and engine agents

A sim version is `VERSION_MINOR`, `NET_PROTOCOL_VERSION` and a hash of `SIM_REVISION`
(`src/game/SimRevision.h`, bumped with every simulation change) and the data files that
affect the simulation (`python3 deploy/sim_version.py` prints it for a source tree; a
tree from before `SIM_REVISION` keeps its earlier key). Players are only matched with the same version, and only an engine agent of
that version can generate maps for or verify their games.

An upgrade that changes the sim version therefore needs care:

- New clients get matches only once an agent of their version runs. Deploy the new
  `engine-agent` image before or with the client release.
- Clients still on the old version get `update_required` unless an agent of the old
  version keeps running, and finished matches of the old version are verified only
  by an old-version agent. Keep one running until those matches are verified and
  old clients have updated.

The file and build paths in this section are relative to the repository root.
Run the worktree and image-build commands from that root.

To serve an additional (older) version, add a second agent service in
`deploy/compose.override.yaml`, which Compose merges automatically:

```yaml
services:
  engine-agent-125-48:
    extends: { file: compose.yaml, service: engine-agent }
    image: ghcr.io/<owner>/<repository>-engine-agent:simver-125-48-<data hash>
    build: !reset null
    deploy: { replicas: 1 }
```

Released engine-agent images are tagged `simver-<sim version>`. To build one for an
older release yourself, build the `engine-agent` target of the current Dockerfile
(the current agent wrapper) with that release's source tree as the engine source:

```sh
git worktree add ../glob2-0.9.25 v0.9.25
docker buildx build -f deploy/Dockerfile --target engine-agent \
  --build-context engine-source=../glob2-0.9.25 \
  --build-arg GLOB2_SIM_VERSION=$(python3 deploy/sim_version.py ../glob2-0.9.25) \
  -t glob2-engine-agent:simver-$(python3 deploy/sim_version.py ../glob2-0.9.25) --load .
```

The engine stage builds the pinned SDL3 family with `scons/sdl3_dependencies.py`
from the engine source, so this works for releases from the SDL3 migration on. An
SDL2-era release (before #487) has no such helper; build its agent from that
release's own Dockerfile and dependencies instead.

The verifier must compute byte-identical games to the release's clients, so build
it with the compiler and flags of that release where they differ, and check it
against a recorded game ([replay verification](../development/headless-replays.md))
before trusting its verdicts.

Each image knows its own version (from its binary and data), registers it, and
takes only that version's jobs; `GET /api/v1/instance` lists every version with a
live agent. Remove the service when the version is retired.

The dedicated `skin-render-worker` service uses the worker database role and
shared blob store, with one render process, two CPU threads, a 3 GiB container
limit and a six-minute shutdown grace period. The display entrypoint starts Xvfb
and executes Node as the primary process, so termination reaches the worker's
graceful shutdown handlers directly. The current engine and its meshes ship
together; this service is independent of match simulation-version agents.
Its capability probe verifies the pinned runtime WebP encoder before registering
a content-derived render revision, and startup
queues existing enabled skin versions and presets for that revision. Publication
continues while artwork is pending or failed. Watch `Skin sprites ready` and
`Skin sprite generation failed` logs for duration, compressed size, retries and
failures. Three attempts and a five-minute process timeout bound each job.
Ready derivatives commit atomically and the API's normal appearance refresh
makes them available to active matches; matches retain the first ready bundle
they received. Deploy the updated API, migration, web interface and renderer
worker together. Source versions and their derivative references remain in blob
retention, including history for disabled skins; download endpoints honor
moderation immediately.

### Images

The Dockerfile targets are `platform` (API, worker and CLI), `music-worker`, `skin-render-worker`, `engine-agent`,
`relay` and `caddy` (Caddy with the built web app). A `server-v*` tag pushed by
the owner to the release mirror `genixpro/glob2-release` runs
`.github/workflows/server-image.yml` (it skips every job in any other repository; a
fork or self-hoster builds images with `deploy/compose.yaml` instead), which
publishes all of them for linux/amd64 and linux/arm64 to
`ghcr.io/genixpro/glob2-release-<target>`, tagged with the Git tag and commit; engine
agents are also tagged `simver-<sim version>` and labelled
`org.glob2.sim-version`. Pin digests in production:

```dotenv
GLOB2_PLATFORM_IMAGE=ghcr.io/<owner>/<repository>-platform@sha256:…
GLOB2_MUSIC_IMAGE=ghcr.io/<owner>/<repository>-music-worker@sha256:…
GLOB2_SKIN_RENDER_IMAGE=ghcr.io/<owner>/<repository>-skin-render-worker@sha256:…
GLOB2_ENGINE_AGENT_IMAGE=ghcr.io/<owner>/<repository>-engine-agent@sha256:…
GLOB2_RELAY_IMAGE=ghcr.io/<owner>/<repository>-relay@sha256:…
GLOB2_CADDY_IMAGE=ghcr.io/<owner>/<repository>-caddy@sha256:…
```

### Automatic deployment

The official instance, `https://app.glob2online.com` (VM `glob2-staging` in project
`pharaoh-418820`, zone `northamerica-northeast2-a`), is deployed by
`.github/workflows/deploy-online.yml`. Like the store release workflows it is
defined here but runs only in the owner's release mirror `genixpro/glob2-release`:
every job is skipped in any other repository, for any other actor, and off the
mirror's `master`. Mirror a reviewed commit there to make a new version of the
workflow take effect.

**What it deploys.** The host can only check out public commits, so:

- A push to the mirror's `master` (when `AUTO_DEPLOY_ONLINE=true`) and a dispatch with
  `ref` = `master` deploy the newest public `master` commit that the mirror's
  `master` contains (their merge base).
- A dispatch with another `ref` deploys that public branch, tag or full commit id.
  It must be on public `master` unless `allow_unmerged` is ticked (for example to
  return to a commit of an integration branch).

The `preflight` job writes the commit, its source and the trigger to the run
summary.

**How it runs.** The `deploy` job uses the `online-production` environment
(restricted to `master`; add required reviewers there to approve each deploy). It
authenticates to Google Cloud without keys through Workload Identity Federation,
adds a three-hour SSH key for the deploy user to the VM's instance metadata,
connects through an IAP TCP tunnel and pipes `deploy/online-deploy.sh` to the host.
That script refuses to start while another `update-host.sh` runs (a process check
plus a lock), records the running revision if there is no record yet, checks out
the commit, and starts `deploy/update-host.sh` detached under `nohup`, with its log
and exit status in `/opt/glob2/deploys/gh-<run id>-<attempt>/`. The job then polls
once a minute, runs the attached smoke test (`platform_stack_smoke.py --attach`,
with `GLOB2_ONLINE_WEBSITE` as `--website`), writes the result, the deployed and
previous revisions, the rollback status, the sim version and the backup directory
to the run summary, and removes its SSH key. Cancelling the run, or the run timing
out, does not stop the deploy on the host; check it there with
`sh /opt/glob2/src/deploy/online-deploy.sh status /opt/glob2/config/staging.env gh-<run>-<attempt>`.

One deploy runs at a time (concurrency group `deploy-online`). A run in progress
is never cancelled; a newer request replaces one that is still waiting. The job waits on a GitHub-hosted runner for the whole deployment. Record runner
usage and deployment duration from the run rather than relying on an old estimate.

**Trigger, enable and disable.**

```sh
gh workflow run deploy-online.yml -R genixpro/glob2-release -f ref=master
gh variable set AUTO_DEPLOY_ONLINE -R genixpro/glob2-release -b true   # deploy every mirrored master
gh variable set AUTO_DEPLOY_ONLINE -R genixpro/glob2-release -b false  # back to manual only
```

Without `AUTO_DEPLOY_ONLINE` = `true`, pushes run nothing. An automatic deploy is
skipped when the deployed revision already contains the commit (the same commit, or
a mirror that is behind what was deployed by hand); a dispatch always deploys.

**Roll back.** `update-host.sh` already returns to the previous release when the
new stack does not become healthy (the summary's rollback row says
`rolled-back`, `failed` or `unchanged`). To go back after a successful deploy,
dispatch again with `ref` = the previous revision from the summary (tick
`allow_unmerged` if it is not on public `master`). Database restores stay manual;
see [Upgrades](upgrades.md#upgrades).

**Repository variables** in the mirror: `GLOB2_ONLINE_WIF_PROVIDER`,
`GLOB2_ONLINE_SERVICE_ACCOUNT`, `GLOB2_ONLINE_PROJECT`, `GLOB2_ONLINE_ZONE`,
`GLOB2_ONLINE_INSTANCE`, `GLOB2_ONLINE_SSH_USER` (the host user that owns
`/opt/glob2` and is in the `docker` group), `GLOB2_ONLINE_ENV_FILE`
(`/opt/glob2/config/staging.env`), optionally `GLOB2_ONLINE_WEBSITE`
(`https://glob2online.com`) and `AUTO_DEPLOY_ONLINE`. None of them is a secret.

**Google Cloud identity.** Each piece, and why it exists:

| Resource | Scope | Why |
| --- | --- | --- |
| Provider `glob2-online-deploy` in pool `github-actions` | the pool | Accepts only GitHub OIDC tokens whose repository is `genixpro/glob2-release` (and its id), actor id the owner's, ref `refs/heads/master`, event `workflow_dispatch` or `push`, environment `online-production`, a GitHub-hosted runner and workflow `deploy-online.yml@refs/heads/master`. It maps `attribute.online_deploy_repository_id`, which no other provider in the pool sets. |
| Service account `glob2-online-deployer` |  | The identity of the workflow; no keys. |
| `roles/iam.workloadIdentityUser` on the service account for `principalSet://…/github-actions/attribute.online_deploy_repository_id/1397722696` | the service account | Lets tokens from that provider act as it. |
| Custom role `glob2OnlineDeployInstance` (`compute.instances.get`, `compute.instances.setMetadata`) | the VM only | Read the VM and add or remove the short-lived SSH key in its own metadata. Nothing project-wide: `gcloud compute ssh` is not used because it also reads and tries to write project metadata. |
| `roles/iap.tunnelResourceAccessor` | the VM's IAP tunnel resource only | Open the IAP TCP tunnel to port 22. |
| Firewall rule `glob2-staging-iap-ssh`: tcp:22 from `35.235.240.0/20` to tag `glob2-staging` | the network | IAP's forwarding range, so SSH keeps working through IAP if the open `default-allow-ssh` rule is ever removed. |

Setting instance metadata is root-equivalent on the VM (as is the deploy user's
`docker` group), so the account is as powerful as a person deploying by hand, but
only on that VM. If Compute Engine asks for `iam.serviceAccounts.actAs` on the
VM's service account when setting metadata, grant `roles/iam.serviceAccountUser`
on that service account only (better: move the VM to the dedicated
`glob2-staging-host` account first). The commands that set this up:

```sh
P=pharaoh-418820 Z=northamerica-northeast2-a SA=glob2-online-deployer@pharaoh-418820.iam.gserviceaccount.com
POOL=projects/485653453075/locations/global/workloadIdentityPools/github-actions
gcloud iam service-accounts create glob2-online-deployer --project $P
gcloud iam workload-identity-pools providers create-oidc glob2-online-deploy --project $P \
    --location global --workload-identity-pool github-actions \
    --issuer-uri https://token.actions.githubusercontent.com \
    --attribute-mapping google.subject=assertion.sub,attribute.online_deploy_repository_id=assertion.repository_id \
    --attribute-condition "assertion.repository_id == '1397722696' && assertion.repository == 'genixpro/glob2-release' && assertion.actor_id == '6193625' && (assertion.event_name == 'workflow_dispatch' || assertion.event_name == 'push') && assertion.environment == 'online-production' && assertion.runner_environment == 'github-hosted' && assertion.ref == 'refs/heads/master' && assertion.workflow_ref == 'genixpro/glob2-release/.github/workflows/deploy-online.yml@refs/heads/master'"
gcloud iam service-accounts add-iam-policy-binding $SA --project $P --role roles/iam.workloadIdentityUser \
    --member principalSet://iam.googleapis.com/$POOL/attribute.online_deploy_repository_id/1397722696
gcloud iam roles create glob2OnlineDeployInstance --project $P \
    --permissions compute.instances.get,compute.instances.setMetadata
gcloud compute instances add-iam-policy-binding glob2-staging --zone $Z --project $P \
    --member serviceAccount:$SA --role projects/$P/roles/glob2OnlineDeployInstance
# gcloud has no command for a single instance's IAP tunnel policy, so call the
# API. This replaces the instance's tunnel policy; read it first with
# :getIamPolicy and add to it if it already has bindings.
curl -sS -X POST -H "Authorization: Bearer $(gcloud auth print-access-token)" \
    -H "Content-Type: application/json" \
    -d "{\"policy\":{\"bindings\":[{\"role\":\"roles/iap.tunnelResourceAccessor\",\"members\":[\"serviceAccount:$SA\"]}]}}" \
    "https://iap.googleapis.com/v1/projects/$P/iap_tunnel/zones/$Z/instances/glob2-staging:setIamPolicy"
gcloud compute firewall-rules create glob2-staging-iap-ssh --project $P --network default \
    --source-ranges 35.235.240.0/20 --allow tcp:22 --target-tags glob2-staging
```

To revoke the pipeline, delete the provider (or disable it with
`gcloud iam workload-identity-pools providers update-oidc … --disabled`).

[Hosting index](README.md) · [Documentation index](../README.md).

## CLI 2 coordinated rollout

Ship the [CLI 2 binary and its consumers](../tools/cli.md#migrating-from-cli-1)
in the same release: engine-agent and skin-render-worker images, native clients,
browser launchers, deployment probes and tournament worker packages. Drain old
workers before switching images; keep their binaries with any in-flight jobs until
those jobs finish. Updated workers probe `glob2 help --format json` with a bounded
timeout and require `schema_version: 1` and `cli_version: 2`. A rejected version is
a deployment mismatch; rebuild the matching image rather than rewriting arguments.
Rollback these components together. The simulation identity, saved games, catalog
and job result schemas remain independent of the CLI version. This rollout does
not require deleting production records, blobs, accounts or completed jobs.
