# Deploy music workers

Processing community music and enabling optional CPU music generation.

## Music worker

The default Compose stack includes `music-worker` (Dockerfile target
`music-worker`, optional pinned `GLOB2_MUSIC_IMAGE`). It uses the worker database
role and blob volume on the internal backend network. FFmpeg runs only in this
service, independently of engine agents and sim versions. The image includes
FFmpeg, Python and the existing mastering dependencies. Website builds also build
the small WASM preview decoder with the game's pinned Opus dependencies.

Default limits are one conversion per worker, two CPUs, 12 GiB memory, 6 GiB
private temporary storage, 64 processes and a 30-minute processing deadline.
These allow optional mastering of a 15-minute stereo trio. Inputs are at most
512 MiB per mood and 8 MiB per cover (4096 pixels per side before thumbnailing).
Only direct media containers are accepted; decoder network protocols and
playlist/concat inputs are disabled. The API admits one buffered source upload
per replica at a time, so allow memory headroom for a 512 MiB request and HTTP
buffer copies. Each registered creator may create six releases/day, keep three
active uploads and submit 24 files/hour. Bulk downloads contain at most ten sets
and fit the game's 64 MiB archive limit.

The worker's container health check checks a fresh heartbeat after a database
round trip. Inspect queue/status totals and stored byte counts at
`GET /api/v1/admin/music-status`, or use the Music administration tab. Monitor
worker health, conversion failures, oldest pending jobs and blob-volume capacity.
A cancelled job stops its decoder group and removes temporary PCM. Retries remove
interrupted attempt directories for that release before starting again. The
platform scheduler expires drafts after 24 hours without activity and collects
unreferenced source files older than 24 hours. Resumed drafts retain all their
referenced inputs, including files uploaded before their last activity.
Transient storage/database failures preserve inputs and retry up to three times;
technical failures and exhausted retries record failure before deleting sources.
Successful conversion retains only final outputs.

Back up music with the existing PostgreSQL dump and `blobs` volume backup: both
metadata/references and media are necessary. Converted audio, artwork, waveform
summaries and ZIPs are protected by `music_assets` references during blob GC.
Withdrawn/hidden releases retain immutable output for administration but public
media routes deny access. Original uploads are temporary and cannot be recovered
after normal cleanup; creators should retain their source files. On restoration,
expired in-flight uploads should be re-uploaded if their sources are absent.

### CPU AI Music Studio

Music Studio is optional and disabled by default. Set `musicStudio.enabled: true`
in the instance configuration and configure `textModel`, `pipelineVersion:
music-v1`, `providerCallsPerDay`, `maxCalls`, `maxOutputTokens`, `maxTotalTokens`
and `timeoutSeconds`. The example configuration caps each generation at 12 model
calls, 250,000 total tokens, 16,000 output tokens per call and 30 minutes. Token
allowance includes a conservative UTF-8-byte input reservation before dispatch.
Per-request model, call and token budgets remain snapshotted; the daily provider
ceiling uses the lower of that snapshot and the current worker configuration, so
lowering the operator limit on restart also constrains queued and recovered work.
Each request can render at most three candidates. `maxSourceBytes` (at most 128 KiB)
and `maxOutputBytes` (at most 128 MiB total artifacts) can lower the hard output
ceilings. Chat requires available music
credit but consumes none.

Configure `MUSIC_OPENAI_API_KEY` for the dedicated worker, install the security
profiles below, then start it with
`docker compose --profile ai-music up -d --build ai-music-worker`. This image
preinstalls pinned CC0 samples, sfizz, Surge XT and Python audio tools. The current
Surge distribution makes this worker Linux amd64 only. There is no GPU or model
server. It runs one request at a time with two CPUs, a 12 GiB container ceiling,
a 6 GiB scratch tmpfs and process/time limits. Numerical libraries use one thread.
Scale workers only after measuring memory use and model cost for your workload.

The host must support unprivileged Linux user namespaces and Bubblewrap inside
the container. Startup executes the isolated Python runtime, FFmpeg, FFprobe,
sfizz and Surge XT, and refuses to start when isolation or their dependencies are
unavailable. The image resolves system library alternatives within `/usr` so
the encoder does not require access to `/etc`. The worker uses the scoped profiles in
`deploy/security/`: install its AppArmor profile before starting it:

```sh
sudo install -m 644 deploy/security/glob2-ai-music.apparmor /etc/apparmor.d/glob2-ai-music
sudo apparmor_parser -r /etc/apparmor.d/glob2-ai-music
```

The seccomp profile is based on Moby profiles revision
`2ceae35d351c156cb5a8efc0fdc4a08cf94569d8`, with namespace creation and
mount operations allowed for Bubblewrap. The AppArmor profile retains Docker's
process, kernel and filesystem protections while allowing private mounts and
the namespace-limit writes used by `--disable-userns`. Docker's system-path masks
are removed for this worker so the kernel permits its private proc mount;
AppArmor still denies sensitive proc/sys access. The worker runs without outer
capabilities, as UID 10001, with no new privileges and a read-only root. Recipes
run in a fresh user/mount/PID/network namespace with all capabilities dropped
and further user namespaces disabled. Keep seccomp and, on AppArmor hosts,
AppArmor enabled. Hosts without AppArmor can explicitly set
`GLOB2_AI_MUSIC_APPARMOR_PROFILE=unconfined`; their renderer must still pass the
same namespace probe. Custom profile locations/names use
`GLOB2_AI_MUSIC_SECCOMP_PROFILE` and `GLOB2_AI_MUSIC_APPARMOR_PROFILE`.
Startup also requires a cgroup v2 ancestor with `memory.max` at most 12 GiB.
This bounds the entire worker and its children; per-process address-space limits
alone cannot constrain a recipe that forks. A bare-host worker or integration
test therefore needs a bounded systemd service/scope (for example,
`systemd-run --user --scope -p MemoryMax=12G ...`), alongside the scratch tmpfs.
Missing or unlimited cgroup accounting fails closed.
Do not replace this with unrestricted execution
or mount a container-engine socket. Recipes get read-only pipeline/assets and a
private writable job directory, with no network, database, blob store or provider
credentials. Trusted rendering/validation runs in a separate fresh namespace.

Keep `musicStudio.salesEnabled: false` until both palettes have passed real-model
cost, failure/recovery and human-listening qualification. Configure dedicated
music credit packs, `MUSIC_STRIPE_SECRET_KEY`, `MUSIC_STRIPE_WEBHOOK_SECRET`, and
the webhook `/api/v1/music-studio/stripe` before enabling sales. Payment-return
URLs never grant credits. Model usage, candidate reports and source revisions
are retained in the request journal; monitor failures, uncertain requests,
reserved credits, worker health and daily provider capacity. Reconcile uncertain
requests through the administrator endpoint documented in the architecture guide.

[Hosting index](README.md) · [Documentation index](../README.md).
