# Content validation rollout

Enable building, JavaScript AI and terrain/resource validation only after their release gates pass.

## Building-family library rollout

Apply migrations through `0050_building_library.sql` before updating the API and
worker. Migration 0049 adds private account drafts; 0050 adds published families,
immutable archives, social activity and the `validate-buildings` engine job kind.
Rebuild and deploy the engine-agent image from the matching engine source, then
deploy the website. The agent enables this job kind only when its binary's
`info catalog --format json` advertises `compose_buildings`; a recent heartbeat must also
advertise the same stock catalog hash used for publication.

Publication is unavailable until a compatible agent is present. Existing agents
continue serving their advertised kinds and simulation versions. Pending releases
become downloadable after native validation succeeds; failed jobs can be retried
by publishing the same saved draft again. A published release pins its archive,
stock catalog and simulation version, so a new engine does not silently replace
old validation. Keep agents for simulation versions the instance still supports.

Format 145 maps, saves and replays include custom building artwork alongside their catalog.
The terrain/resource asset bundle retains its format-144 loading gate.
Network protocol 62 prevents mixed header layouts in one match. The save-support
floor remains 58. Authors share generated maps through the existing map library;
server-side room generation does not read a player's locally installed families.
See [building catalogs](../features/building-authoring.md#online-library-and-installed-families)
for quotas, package limits, moderation and the author workflow.


## JavaScript AI validation rollout

Apply database migrations through `0040_ai_library.sql`, then deploy the engine
agent and platform worker before enabling publishing on the website. A working
`validate-ai` agent heartbeat is required for upload acceptance. The agent performs
an isolated startup probe; failure disables this capability and logs the reason.
Other engine capabilities remain available.

The engine-agent image includes Bubblewrap. AI jobs require Linux user, PID, mount
and network namespaces, no inherited credentials, read-only engine/data/fixtures,
and a dedicated scratch tmpfs of at most 4 GiB (Compose defaults to 1 GiB). Each
engine invocation has a 120-second wall/CPU bound, 2 GiB address-space bound, 64 MiB
file bound and bounded captured output. Temporary internal storage is also bounded.
Keep container memory/PID limits and the network namespace boundary in place. An
installation whose container security policy disallows nested user namespaces
must configure an appropriate isolated worker host; there is no unsandboxed fallback.
Do not disable host protections or run the engine agent privileged to bypass a
failed probe. `ENGINE_AI_LIBRARY_PATH`, if needed for a custom build, mounts only a
trusted read-only shared-library directory inside validation jobs.

Validation progress uses the held engine-job lease and is checked against the job's
source hash, simulation version and suite. Interrupted jobs retain their source and
retry through the existing engine queue. Published validation history remains bound
to its original engine; the scheduler requests new evidence as agents for new
versions appear. A newly deployed engine must pass checks before native clients can
install a release. These checks establish compatibility, not a security certificate.


## Terrain and resource set validation rollout

Apply migrations through `0048_sets.sql` and deploy the platform API, worker, web
workspace and a matching engine agent that supports file format 144 or later and
advertises the `validate_set` command. Install the matching WebAssembly client,
including `/play/set-preview.html`, to enable workspace previews. Browser previews
are temporary local checks; publication requires server validation.

Install the engine's scoped AppArmor profile before recreating its container:

```sh
sudo install -m 644 deploy/security/glob2-engine.apparmor /etc/apparmor.d/glob2-engine
sudo apparmor_parser -r /etc/apparmor.d/glob2-engine
```

Compose uses the shared `deploy/security/ai-music-seccomp.json` namespace policy
and `glob2-engine` AppArmor profile. These permit Bubblewrap's private mounts and
namespace creation while retaining container confinement, dropped outer
capabilities, a read-only root and no new privileges. System-path masks are
removed so Bubblewrap can mount its private proc filesystem; AppArmor still
denies sensitive proc/sys access. Custom profile paths and names use
`GLOB2_ENGINE_SECCOMP_PROFILE` and `GLOB2_ENGINE_APPARMOR_PROFILE`. On hosts without
AppArmor, explicitly set `GLOB2_ENGINE_APPARMOR_PROFILE=unconfined`; seccomp and
the same isolated startup probe remain required.

Set `ENGINE_SET_VALIDATION=1` in `.env`, then recreate the engine agent:

```sh
docker compose up -d --force-recreate engine-agent
docker compose logs --tail=100 engine-agent
```

An engine agent with asset-capable file format 144 or later must pass its Linux
Bubblewrap probe, including rendering a small set, before starting. This applies
even while `ENGINE_SET_VALIDATION=0`: map uploads, previews and match verification
can contain bundled PNGs and use the same isolated launcher. The publishing flag
additionally enables `validate-set` in its heartbeat. Verify the rollout by saving a
small draft in the workspace, running checks and confirming a passing result and
preview before publishing it. A failed isolation probe stops that modern agent
from starting; it cannot advertise unsafe map jobs. The workspace preserves drafts and says no validator is available
until a capable agent returns. Keep publication disabled until the probe and an
actual draft check pass.

Validation requires user, PID, mount and network namespaces and a dedicated
scratch tmpfs of at most 4 GiB (Compose defaults to 1 GiB). Standalone set checks have a 120-second
wall/CPU bound, 2 GiB address-space bound and 64 MiB per-file bound. There is no
unsandboxed fallback. Use an isolated worker host if the container security policy
prevents nested namespaces; do not run the agent privileged to bypass the probe.
For custom dynamically linked builds, supply `ENGINE_SET_LIBRARY_PATH` as trusted,
read-only directories in the engine-agent environment. The validator also discovers
the installed binary's `lib/glob2` directory.

To pause new publication checks, restore `ENGINE_SET_VALIDATION=0` and recreate the
agent. Existing releases and self-contained shared maps retain their assets and
credits. An old queued check whose agent no longer advertises this capability is
failed by the stale-job sweep after six hours, allowing the author to retry when a
validator returns. A package is limited to 16 MiB; each set retains at most 100
drafts and 50 immutable releases. See
[set architecture](../multiplayer/content-libraries.md#terrain-and-resource-set-library)
for ownership, visibility and blob retention.

[Hosting index](README.md) · [Documentation index](../README.md).
