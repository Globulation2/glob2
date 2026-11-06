# Distributed tournaments

Tournament binaries and worker packages require [CLI 2](cli.md). Register new
bundles when upgrading; drain active attempts before switching worker packages.
Retain historical packages with their original binaries when resuming old runs.
Registration and worker startup probe the static CLI description with a timeout;
unsupported versions fail explicitly. Simulation and artifact schemas are separate.

`tools/tournaments` is a Python 3.10+ standard-library package for Linux and macOS.
It executes immutable, explicitly selected binary/data bundles on localhost and
SSH hosts. Workers need Python and the bundle's runtime libraries already installed.
No source builds, dependency installation, inbound coordinator service, rating rule,
or tournament adjudication is part of the worker or engine.

## Start locally

Build the production client (`scons -j4 release=1 server=0`), then prepare a directory
containing `glob2` and `content/data/`. Include optional symbols and any other runtime files
needed by that build. Bundles must contain regular files, not symlinks. Run from the
repository root:

```sh
mkdir -p /tmp/glob2-supplied
cp build/src/glob2 /tmp/glob2-supplied/glob2
cp -R data /tmp/glob2-supplied/data
python3 -m tools.tournaments bundle /tmp/glob2-supplied /tmp/glob2-bundles \
  --revision "$(git rev-parse HEAD)" --dirty-identity YOUR_SOURCE_DIFF_HASH
```

Use the emitted ID and `/tmp/glob2-bundles/ID` path below. `--revision` identifies
source; `--dirty-identity` identifies uncommitted source (hash a retained source
snapshot or diff, including untracked source). `--options` reads a JSON object of
build flags. `--executable` defaults to `glob2`. For a bundle built on another
platform, supply `--platform platform.json` (for example
`{"os":"linux","arch":"x86_64"}`) and `--capabilities catalog.json`, captured by
running that bundle's `glob2 info catalog --format json` on its target. Registration hashes
every file and manifest field. It never overwrites an installed bundle.

An AI comparison configuration (`comparison.json`):

```json
{
  "id": "example-duels",
  "ais": ["cortex", "nicowar"],
  "formats": ["1v1"],
  "generators": [15],
  "map_seeds": [1001, 1002],
  "game_seeds": [19],
  "ticks": 4096,
  "candidates": 0
}
```

A host list (`hosts.json`); directories must be absolute:

```json
[
  {"name":"localhost", "transport":"local", "directory":"/tmp/glob2-worker", "slots":1}
]
```

```sh
python3 -m tools.tournaments.ai_comparison plan comparison.json --bundle /tmp/glob2-bundles/ID --output planned.json
python3 -m tools.tournaments submit planned.json /tmp/glob2-results --bundle /tmp/glob2-bundles/ID
python3 -m tools.tournaments run /tmp/glob2-results --hosts hosts.json
python3 -m tools.tournaments.ai_comparison reanalyze /tmp/glob2-results
```

Planning validates and expands jobs without starting games. Generation jobs save
verified rotations; dependent games consume the exact committed map artifact.
Rotation export normalizes the save header after loading initializes generated
building call lists. It still requires byte-identical reloads and a full rotation
back to the original, alongside unchanged terrain and colony-state checks.
Each generation sample is its own job, so a failed sample does not discard a batch.

## Production engine interface

Put the command first. `info catalog --format json` writes schema-version-1 JSON to stdout;
startup diagnostics go to stderr. It enumerates selectable AIs (excluding None),
Cortex and Maxima parameter schemas, generators, controls, revisions, telemetry,
and save/network versions, plus map-report and generation-telemetry schema versions.
Its `commands` list names the structured commands this build supports (`game`,
`generate_map`, `verify_match`, `sim_version`, `compose_buildings`), and `sim_version` holds the build's
simulation version (see [verifying a match record](../development/headless-replays.md#verifying-a-match-record)),
so a job runner can probe a binary without passing it flags it may not know.
Game and generation commands require `--output-dir DIR`; an existing
`result.json` is rejected. Building-family composition writes JSON to stdout; see
[portable building families](../features/building-family-packages.md#portable-building-families).
Values are separate ordinary arguments, not JSON.

```sh
build/src/glob2 map study 15 --seed 42 \
  --set teams=2 --set width=7 --set height=7 \
  --write-map --rotations 2 --output-dir /tmp/generated
build/src/glob2 game run --map-file /tmp/generated/map-r0.map.gz \
  --game-seed 19 --player cortex --player cortex \
  --ai-param 0:swarmWorkerCap=4 --ai-param 1:swarmWorkerCap=7 \
  --ticks 4096 --save initial --save every:512 --save final \
  --telemetry checksums --output-dir /tmp/played
```

To exercise a custom profile, append repeatable rule arguments to a new game:

```sh
build/src/glob2 game run --map-file content/maps/SmallForTwo.map.gz \
  --game-seed 19 --player cortex --player maxima \
  --rule noUpgrades=1 --rule noHunger=1 --rule peaceful=1 \
  --ticks 30000 --save initial --save final --write-replay \
  --telemetry checksums --output-dir /tmp/custom-rules
```

For distributed game jobs, put the same settings in `config.rules`, with integer
values such as `{"noUpgrades": 1, "noHunger": 1, "peaceful": 1}`. The adapter emits
rule arguments in name order; the engine validates names and ranges. Effective
settings, including defaults for rules not overridden, appear in `resolved.rules`.
Saved-game jobs retain their original rules and reject overrides. A capped
peaceful conquest-only game can finish without a winner; use crashes, unavailable
orders and stalled useful planning as correctness signals, with win rates as
strategy diagnostics.

Generator options:

| Argument | Meaning/default |
| --- | --- |
| `map study GENERATOR --seed N` | Generator and independent uint32 seed |
| `--set key=value` | Repeatable generator controls; defaults and valid ranges come from catalog. Width/height are power-of-two exponents, as in the existing study tool |
| `--candidates N` | 0: explicit single-seed generation; positive: deterministic quality candidate selection, maximum 10000 |
| `--write-map` | absent by default; emits `map-rN.map.gz` (gzip level 6) when true |
| `--rotations N` | 1; cyclic team reindexings, verified for unchanged geography and rotated starts |
| `--report headroom/terrain` | Headroom study measurements on stdout or terrain/resource grid in terrain.txt |
| `--profile NAME` | Optional isolated profile name; profile files live inside this output directory |

Game options:

| Argument | Meaning/default |
| --- | --- |
| `--map-file PATH` | New game on this exact map |
| `--load-game PATH` | Saved initial state or continuation, mutually exclusive with map/generator input |
| `--generator`, `--map-seed`, `--set`, `--candidates` | Inline generation alternative; embeds generation results and saves the generated map |
| `--game-seed N` | Required uint32 for a new game; forbidden when loading a save |
| `--ai-script player:source.js` | Embedded source for a `javascript` player (zero-based index); new games only |
| `--map-script source.js` | Replace the new game's map script with JavaScript |
| `--player AI` | Repeat once per map team, in team order; required for new games |
| `--ai-param P:key=value` | Repeatable, zero-based player overrides; duplicate keys and invalid values rejected |
| `--alliance N` | Repeat once per team, one-based group labels; default separate alliances |
| `--win-condition NAME` | Repeatable replacement for standard conditions: death, allies, prestige, opponents, script; probability early victory is appended unless explicitly disabled |
| `--win-probability PERMILLE` | New games default to 950 (95%); 0 disables, otherwise 501–1000; saved games retain their conditions |
| `--experiment KEY` | Repeatable [experimental feature](../features/experimental-features.md) baked into a new game, e.g. `guard-area-balancing`; the profile's settings never apply to structured runs; forbidden when loading a save. Listed in `result.json` under `resolved.experiments` |
| `--rule name=value` | Repeatable custom rules, using the names and ranges in [headless rules](../development/headless-replays.md#glob2_test_rules). New games only; effective values are recorded in `resolved.rules`. Tournament game configurations accept the equivalent `rules` object, e.g. `{"noUpgrades": 1, "peaceful": 1}` |
| `--ticks N` | Absolute tick limit, default 90000; must exceed saved tick |
| `--benchmark-warmup N` | Opt in to in-engine CPU counters, excluding the first N simulated ticks; tournament game configuration: `benchmark_warmup_ticks`. Loading a save keeps the absolute `ticks` limit; warmup must leave measured ticks. Whole-process resource usage also includes startup/loading. |
| `--gradient-backend NAME` | Explicit gradient backend (`cpu`, `scalar`, `sse2`, `neon`, `opencl`, `vulkan`, `webgpu`); tournament game configuration: `gradient_backend`. Record the actual backend from the result when comparing compute costs. |
| `--compute-threads auto\|N` | Shared executor participants including the owner; native `auto` prefers eligible physical cores, falling back to logical CPUs then one; browser auto uses logical CPUs capped at eight; explicit N is a positive unsigned integer |
| `--write-replay` | Boolean switch; absent by default; writes `game.replay` when present |
| `--save initial/final/every:N` | Repeatable opt-in saves; checkpoints are diagnostics, not automatic recovery |
| `--telemetry NAME` | Repeatable checksums, team-timeline, maxima, warrior-tactics; default none. See [warrior travel and contact](../ai/telemetry.md#warrior-travel-and-contact) |
| `--profile NAME` | Optional isolated profile name |

GUI autosaving defaults off for tournament/headless runs, independently of the
normal-play preference. Snapshots are produced only when requested through
`--save`; checkpoint frequency is controlled by `every:N`.

Saved games retain settings and execution state; player, alliance, condition,
seed and tuning overrides are forbidden on load. Structured commands isolate
ambient tuning/telemetry/ML environment variables. Numeric Cortex tuning and the
existing Maxima strategy schema are supported; external Cortex ML model selection
is not a player override. Legacy binary invocations keep their environment behavior.
Resolved values are persisted in full and partial GameHeaders, not file paths.
Save version 101 keeps the minimum supported version at 58. New Cortex saves also
retain queued orders, debounce state and settle clocks needed for continuation.
Historic saves cannot recover state their writer never serialized. Network/YOG
protocol is 30; earlier clients are rejected. Replay acceptance retains its existing
floor (99) and rejects future formats; newer header fields are gated by save version.

Results include engine winners (all teams and alliance groups), elimination ticks,
alive states, player/team/start coordinates, resolved AI settings, termination,
standard team statistics/history, and surviving warrior count/HP/attack strength.
History rows are `[units, buildings, prestige, HP, attack, defense]`, sampled by
the existing team-stat clock every 512 ticks; loaded history is retained.
`tick_cap` is unresolved. `result.json` and `artifacts.json` have schema version 1.
The engine manifest lists relative files and sizes; the worker adds transfer and
uncompressed SHA-256 hashes. Progress is tick JSONL (every 256 ticks) or a generation
stage record. Generation results retain requested controls, candidate seed/revision,
quality, colony statistics, and study measurements; detailed reports remain in stdout.
Exit 0 means completed; 2 means invalid request; 3 reports artifact/I/O failure; a valid generation failure uses a
nonzero exit and explicit `generation_failed`. Abrupt exits may leave only logs and
last progress; no result/save/stack is guaranteed after a crash.

## Architecture

```mermaid
flowchart LR
    C[Coordinator SQLite leases] -->|local or outbound SSH RPC| Q[Worker SQLite queue]
    Q --> E[Isolated engine processes]
    E --> S[Compressed upload spool]
    S -->|resumable checksummed chunks| A[Verified coordinator artifacts]
    A --> R[Single accepted result and JSON exports]
    R -->|durable acknowledgement| S
    R --> O[Offline reports]
```

The coordinator owns lease and acceptance decisions. Worker process and queue
state survive independent interruptions. Artifact packaging runs in separate processes from simulation slots. Continuous
`run` has independent bounded lanes for host control/lease renewal, input delivery,
and result collection. Every host has a control lane; bulk transfers cannot block
its heartbeats or wait for another host's transfer slot. Finished-but-uncollected
attempts retain their leases but no longer occupy compute prefetch capacity.
Worker disk/spool limits and a separate result-backlog cap bound storage pressure.
Pipeline lanes share the already-loaded immutable manifest and open the existing
ledger without replaying job initialization or export repair. Dispatch streams
pending rows only until a host is full, so large manifests do not become a
per-heartbeat Python allocation.

## Protocol and manifest fields

`experiment.json` has `schema_version:1`, a filesystem-safe unique `id`, nonempty
`jobs`, optional opaque `labels`, and optional `settings`. Planners also preserve
`kind` and `design`. Submission adds immutable `package_id`, stores `worker.pyz`,
and rejects modifications on resubmission. New settings, job inputs, builds or
source revisions require a new experiment/revision. Multiple explicit build IDs
are allowed within a manifest. The package hash covers its Python modules.
Resume a historical package with `python3 RESULTS/worker.pyz run RESULTS --hosts hosts.json`.

Every job has these fields:

| Field | Meaning/default |
| --- | --- |
| `schema_version`, `id`, `type`, `build` | 1, stable logical identity, game or generate_map, immutable bundle hash |
| `inputs` | Map `map` or save `save`, or empty for generation. Values are imported artifact records or `{"job":"ID","artifact":"map-r0.map.gz"}` (artifact names match the produced file, including its `.gz` suffix) |
| `depends_on` | Job IDs; every input dependency must appear; cycles rejected |
| `seeds` | `{"game":N}` or `{"map":N}`; empty on saved games |
| `config` | Game: players, ticks, ai_params (player-string to key/value object), alliances, winning_conditions. Generator: generator, params, candidates, rotations. Defaults follow CLI |
| `outputs` | replay false; saves/telemetry/reports empty lists; log_mode `full` (or `telemetry`, or `gameplay`/`win-probability` with game team-timeline telemetry); map false; core/stack false; required empty list of relative output paths; result `full` (or `outcome` for compact rating/adjudication fields only) |
| `limits` | timeout_seconds 3600, memory_mb host default, estimated_seconds 60 (buffer planning only) |
| `labels` | Opaque JSON object preserved unchanged |

Use `python3 -m tools.tournaments input RESULTS FILE` before submission to import
external input; copy the emitted complete artifact record into `inputs`. This
retains compression metadata needed for transparent decoding.

Coordinator settings (omitted fields use these defaults): heartbeat_seconds 15,
lease_seconds 300 (at least two heartbeats), prefetch 2 queued jobs per execution
slot in addition to active games, infrastructure_attempts 5, process_attempts 2,
transfer_slots 4 for bounded `--once` synchronization. Continuous `run` uses
input_transfer_slots 8, result_transfer_slots 8, and poll_seconds 1 (no longer than
heartbeat_seconds). Each transfer stage limits both running and queued tasks;
partial transfers rotate among available attempts. Optional prefetch_seconds limits estimated buffered dispatch.
Leases use coordinator time; running and queued attempts both renew while connected.
Each dispatch has a new attempt ID and random lease token. A disconnected worker
continues its buffer and spools results. Expired leases can be reassigned; old tokens
cannot revive, replace an accepted result, or count twice. Late attempts are retained.

Failures distinguish transport_failure, interrupted, disk_pressure, artifact_failure,
invalid_result, crash, timeout, process_failure, invalid_request, generation_failed,
and storage_limit. A storage_limit failure is recorded once and is not automatically
retried; change the recording request or its explicit allowance before retrying.
Infrastructure failures have five attempts total; crashes/timeouts/process failures
have two total, preferring another eligible connected host. Invalid requests and
ordinary generation failures are terminal recorded results. Dependencies requiring
an unavailable generated map become blocked. Automatic retries restart original
input; a user can explicitly create a saved-state job to inspect a checkpoint.

## Experiment storage

Ordinary games retain results without replay or checkpoint output. Keep map seeds,
input identities and frozen bundles so a small, declared set of diagnostic games
can be reproduced. Request initial/final saves or replays for those games; avoid
periodic saves on every game in a broad campaign. A `checksums` request includes
per-object, per-field data on **every tick**, and can grow to many gigabytes for
one large FFA. Games whose absolute tick limit exceeds 4,096 must provide an
explicit positive integer `limits.artifact_bytes` when requesting it. Prefer short
probes loaded near the event of interest. The worker's byte limits still apply.

For model-based Maxima comparisons, this profile retains only the engine's
512-tick win-probability records and structured results:

```json
{
  "outputs": {
    "telemetry": ["team-timeline"],
    "log_mode": "win-probability",
    "result": "full"
  }
}
```

For tactical studies that also need Maxima flags and placement decisions, use
`log_mode: "telemetry"` with the requested `maxima`/`team-timeline` telemetry.
It retains `MAXIMA_TELEMETRY`, `GLOB2_WINPROB`, `GLOB2_TL`, `GLOB2_ECON` and
`GLOB2_TIMELINE` records, while omitting unrelated console/performance chatter.
For delivery and labour studies, `log_mode: "gameplay"` keeps `GLOB2_MEASURE`,
`GLOB2_MEASURE_HISTORY`, win-probability and team-state records, omitting AI,
performance and Maxima records. This avoids retaining high-volume AI samples and histories
when they are outside the study. Saved games may still emit substantial gameplay
history; calibrate allowances over a full measurement window before dispatch.
Probability-only capture omits delivery and labour measurements. Jobs that need
those measurements can set `labels.require_gameplay_telemetry: true`; validation
requires game `team-timeline` telemetry with full or gameplay capture, and
collection rejects a completed game without a retained final measurement record.
Endpoint analysis must still verify team coverage and observation boundaries.
Requested CSV outputs are unaffected.

`log_mode` controls stdout retention only; stderr remains diagnostic. The engine
still emits its ordinary timeline internally. Both console streams are compressed
while the game runs, with no full raw-log temporary copy. Stored `stdout.log`
remains readable through `Results.open_artifact`; its raw hash and byte count
refer to the retained content. Execution records report received/retained bytes,
mode, truncation and capture errors. Full console capture remains available with
`log_mode: "full"`. Hitting a log limit is a failed measurement, never a successful
shortened timeline.

Workers enforce an aggregate attempt allowance and a hard child file-size limit;
job `limits.artifact_bytes` and `limits.log_bytes` may lower the host ceilings.
These limits also cover saves, replays, core files and detailed traces. After
packing publishes verified content and its durable record, source output copies
are removed; acknowledgement still controls removal of the authoritative spool.
A host stops admitting work at its spool budget. Supervisors also watch live
attempt/spool usage and the full disk reserve. Aggregate monitoring can overshoot
briefly between polls; per-file and retained-log ceilings remain hard bounds.

Coordinator settings default to `artifact_budget_bytes: 10737418240` (10 GiB) for
artifacts plus transfer state, and `disk_reserve_bytes: 2147483648` (2 GiB).
Concurrent collectors reserve an artifact's remaining transfer size before
receiving it. Quota pressure defers collection and new dispatch; uncollected
worker evidence remains intact. `status` reports used, reserved, budget and free
bytes. Record larger campaign allowances explicitly in the experiment manifest
when the intended evidence requires them. Frozen build caches are accounted for
separately by the worker cache budget; derived report exports are not included in
the coordinator artifact-transfer allowance.

These protections apply to the shared tournament runner. Standalone shell/Python
loops must use the same storage bounds rather than bypassing them. The managed
SDK/cache pruner does not clean experimental evidence; see
[development storage](../development/development-storage.md).

## Hosts and operational commands

Each host has `name`, `directory`, optional `transport` (ssh default, or local),
`ssh` (alias defaults to name), `python` (python3 remotely, current interpreter locally),
`transport_timeout_seconds` (30), and worker fields:

| Field | Default |
| --- | --- |
| slots | logical CPUs minus one, minimum one; one process per slot |
| collect_slots | 2 separate artifact packaging processes |
| result_backlog_limit | 128 executed/packing/done attempts before admission stops |
| input_slots | 2 concurrent input deliveries per host (coordinator-side) |
| result_slots | 2 concurrent result collections per host (coordinator-side) |
| builds | empty: every platform-compatible, capable build |
| disk_reserve_bytes | 5 GiB |
| spool_budget_bytes | 10 GiB |
| attempt_budget_bytes | 512 MiB, including inputs, logs and child-written outputs |
| log_budget_bytes | 64 MiB of retained decoded bytes per console stream |
| cache_budget_bytes | 20 GiB |
| memory_mb | null (optional process address-space limit) |
| cpu_ids | absent: inherit the host CPU affinity; otherwise a nonempty list of caller-owned logical CPU IDs |
| participants_per_game | absent: use every CPU in each allocated share; optional positive owner-inclusive count |

With `cpu_ids`, specify `slots` explicitly. The coordinator partitions those CPU
IDs into disjoint shares and freezes each attempt's share and compute-participant
count. The worker validates the plan, pins the dedicated job supervisor before
launch, and passes the existing `--compute-threads` flag to game jobs. An explicit
participant count cannot exceed the smallest share. Completed game results must
report the assigned resolved and actual counts; silent serial fallback is rejected.
Map jobs inherit the share without a compute-thread override. Explicit affinity
requires Linux-style `sched_getaffinity`/`sched_setaffinity` support.

For one host, `run --cpu-ids 2,3,6,7 --jobs 2 --compute-participants 1` supplies the
same fields. `collect` and `doctor` accept the same options; repeat the frozen
budget on later calls or store it in host JSON. Use per-host JSON for multiple
hosts. The budget is caller-supplied;
the coordinator checks overlap among known local hosts but cannot infer that two
SSH aliases name the same machine or reserve CPUs from unrelated processes.
Allocation receipts persist in the results database. CPU plans cannot change
mid-cohort. With experiment `settings.measured=true`, retries retain their
original host and slot; ordinary retries may select another allocation.
A share is reserved while its job is queued or running. This bounds outstanding
work without another scheduling queue, but the next job waits for a coordinator
poll and transfer after the share is released. For very short jobs that gap can
reduce throughput; explicit allocation is not a universal speedup claim.

For hosts without explicit CPU allocations, worker `slots`, `collect_slots`,
backlog and storage limits reload on every daemon tick; reconfiguration does not
require restarting the daemon. Packaging compresses
into private attempt staging in parallel. A short shared lock publishes objects and
the record atomically with respect to acknowledgement cleanup, preserving shared
artifacts needed by other attempts. `collect_slots` therefore controls actual
compression concurrency. Packing records retain queue and compression durations.
`reports/pipeline.json` records active control/input/result lane counts and the latest
worker queue/storage snapshots; snapshots are observations, not promises that every
configured slot is busy.

Each lane uses its own persistent SSH/Python RPC stream, with bounded messages and
a timeout covering request writes and response reads. Broken streams are discarded;
retry/reconnect uses the existing idempotent operations and lease rules, never an
unconditional replay of an unknown mutation. Old experiments retain their original
worker package; resume those with their preserved `worker.pyz`.

Use explicit conservative slots initially. Inspect peak RSS, process seconds and
throughput before increasing concurrency. Generation, execution, packaging and
transport are separate bounded activities. Stop admitting jobs at reserve/spool
limits; watchdogs kill outside simulation ticks and classify the attempt. Active
jobs can overshoot spool estimates; leave ample reserve for their outputs. Worker
cache cleanup is explicit and will refuse while work is unacknowledged.

SSH must support noninteractive `ssh -o BatchMode=yes HOST python3 --version`.
Use normal SSH keys/config and host-key verification. Package uploads, immutable
bundle installation, process start, job delivery, and chunk retrieval all use
outbound SSH sessions. No port or service is opened on the coordinator.

```json
[
  {"name":"worker-1.example","directory":"/srv/glob2-workers","slots":1},
  {"name":"worker-2.example","directory":"/srv/glob2-workers","slots":1},
  {"name":"worker-3.example","directory":"/srv/glob2-workers","slots":1},
  {"name":"worker-4.example","directory":"/srv/glob2-workers","slots":1},
  {"name":"worker-5.example","directory":"/srv/glob2-workers","slots":1}
]
```

| Command | Effect |
| --- | --- |
| plan MANIFEST [--output FILE] | Validate fully expanded manifest |
| submit MANIFEST RESULTS --bundle DIR (repeatable) | Snapshot bundles, jobs, package and database |
| run RESULTS --hosts FILE [--once] | Reconnect/start missing workers; dispatch, renew, collect until terminal; once performs one bounded synchronization |
| status RESULTS | Local job/attempt counts, host queues/slots/spool/free disk, throughput/resources, errors and failure categories |
| pause RESULTS | Stop new dispatch; already buffered jobs may start |
| pause RESULTS --drain | Also stop starting buffered jobs once workers receive the control |
| resume RESULTS | Resume paused/draining work |
| cancel RESULTS | Durable cancellation; no further result acceptance for cancelled jobs |
| retry RESULTS [--job ID] | Reset exhausted failed jobs' retry budgets, preserving attempts |
| collect RESULTS --hosts FILE | One collection/control pass, no new dispatch |
| doctor RESULTS --hosts FILE | Deploy/check workers and report platform, storage, queue and daemon status |
| diagnose RESULTS JOB --output MANIFEST | New one-job manifest with retained dependency artifacts, expanded saves/telemetry/core/stack |
| cleanup RESULTS --reports/--transfers | Remove regenerable reports or inactive transfer staging |
| cleanup RESULTS --worker-hosts FILE --objects/--bundles | Remove idle worker input caches and/or installed bundles |
| audit --hosts FILE [--stale-hours N] | Discover every worker install found under each host's home directory, not just the ones named in FILE; read-only |
| reap --hosts FILE --host NAME --directory DIR [--confirm] | Stop one stale install's daemon by exact PID; never deletes files; omit --confirm for a dry run |

`audit` searches up to six directory levels below each host's home for worker
installs. It reads their persisted state across package versions. An install is
stale when its daemon is dead, or its queue has no running/queued jobs and no
recent database or WAL activity. Unreadable queue counts do not establish idleness.

`reap` defaults to a dry run. With `--confirm`, it checks that the recorded PID
still names a daemon for the selected worker root before sending SIGTERM. This
reduces stale-PID risk; the check and signal are not atomic. It leaves files and
running game supervisors intact. Only reap installs whose work should stop.

A `configure` RPC updates `host.json`; the daemon reads it on every tick, so
ordinary slot/build/storage-budget changes take effect without restarting the
worker. A frozen CPU plan cannot change while work is live or within the same
coordinator cohort.

Continue `run` or invoke `collect` after changing controls so connected workers
receive them. Disconnected workers may finish before learning cancellation; those
results are retained as unaccepted attempts. Cancellation never revokes results
accepted earlier. A cancelled experiment cannot resume; create a new one. A paused
continuous runner remains available for resume. Only one coordinator run holds the
experiment lock, while status/control commands can operate concurrently.

If a coordinator dies, rerun with the same results directory/package. SQLite WAL
commits are authoritative and missing JSON exports are repaired. Workers have their
own SQLite queue and independent attempt supervisors: daemon death does not kill
an ongoing game. A killed supervisor/game is identified and rerun from original
input. Five-minute disconnected work may execute twice; acceptance is exactly once.

## Artifacts and retention

Portable results contain experiment.json, builds/, jobs/, results/, failures/,
attempts/, artifacts/, reports/, state.sqlite and worker.pyz (plus transfer staging).
Committed JSON and small summaries are directly readable without SQLite. Large
logs/saves/traces use deterministic standard gzip. Artifacts are content addressed;
streaming readers verify transported hash/size; input extraction also verifies decoded hash/size. One-MiB chunks have
checksums and durable resumable offsets; verified files are atomically published.
A result commits only after every declared artifact is verified. A worker deletes
its upload spool only after durable coordinator acknowledgement, including on
repeated acknowledgements after interruption. Failed attempts retain configuration,
build/host/package, status/signal, timing/resources, progress, logs, partial requested
outputs, and missing-artifact declarations. Optional installed coredumpctl and gdb
or lldb provide best-effort diagnostics with explicit availability reasons.

No automatic central retention deletes raw observations. Keep the complete results
directory for review/reanalysis. Explicit cleanup removes only derivative staging,
reports or idle worker caches; delete an archived experiment directory yourself when
its retention period ends. Never manually remove unacknowledged worker spools.
Exact trace replication is optional in tournaments; transport hashes cannot detect
plausible computation errors from faulty RAM. pharaoh-dev-1 is a normal pilot host.

## Experiment designs and reanalysis

All experiment modules (`ai_comparison`, `fairness`, `generator_stress`, `ablations`) support `plan CONFIG --bundle DIR --output FILE`,
`submit CONFIG --bundle DIR --output RESULTS`, and
`reanalyze RESULTS [--policy win_probability|prestige|survivor_draw|military] [--seed 1] [--draws 1000]
[--k 32] [--output DIR]`. Reports are JSON, CSV and Markdown; no rerun is required.

Shared design fields: id; builds (all supplied by default); map_build (first build);
generators [15]; map_seeds [1001]; game_seeds [1]; generator_params {}; candidates 5
for reusable maps; ticks 90000; timeout_seconds 3600; generation_timeout_seconds
1800; outputs {}; settings {}; labels {}; win_probability_permille 950 (95%; use 0 to disable). Build
cohorts use common generated maps
unless generator variation itself is the experiment. Each build needs an eligible
host. Generator defaults/ranges are always discoverable in its pinned catalog.

Set `randomize_parameters: true` to sample map controls from that pinned catalog.
`parameter_domain` defaults to `search`; use `legal` for extreme-value compatibility
studies. Width, height, colony count and starting workers remain at their defaults
or explicit settings. Explicit `generator_params` and generator ablation overrides
take precedence over sampled values. Search sampling requires `searchValues` in
the bundle catalog; older bundles fail explicitly rather than silently sampling
legal ranges. Explicit `grid`, `sample` and `one_parameter` lists remain unrestricted.
Stress sweeps inherit `generator_params`; explicitly swept keys override those settings.

AI comparison, fairness and ablation planners draw a reproducible parameter set
from each map seed and reuse the map across players, rotations and build cohorts.
Generator stress adds `samples` catalog draws per generator/build using `sample_seed`,
alongside its baseline and explicit sweeps. Each sampled request is retained;
unsupported combinations remain reported failures rather than being filtered out.

* `ai_comparison`: ais defaults to all active selectable implementations. The standard
  Elo cohort defaults to 1v1 on generated 128x128 maps (`width` and `height` are
  both exponent 7), which keeps ratings directly comparable while minimizing CPU.
  It records only each game's result by default; request `outputs.telemetry`
  explicitly when a study needs per-tick AI or team history. Incidental maps,
  logs and reports are omitted for successful game jobs with no requested outputs;
  failed jobs and map-generation jobs retain their diagnostic artifacts.
  For rating-only runs, set `outputs.result` to `outcome`. The retained payload
  includes termination, winners, team/player identity, survival, prestige,
  military totals, raw probability summaries and the versioned log integral
  needed by adjusted trajectory scoring, while omitting
  per-tick histories and other bulky result details. Installed bundles are fully
  hashed before atomic publication and then treated as immutable; workers do not
  rehash the complete bundle before every queued game.
  `formats` and `generator_params` can explicitly request another study design. Duels
  pair every AI; 2v2 defaults to homogeneous
  pairs and accepts explicit two-player `rosters`; four-colony FFA balances AI
  participation through combinations and cyclic player orders. Map rotations and
  player-order rotations balance team indices and starts. Identical logical jobs
  are deduplicated, not counted as independent evidence.

  An exhaustive `ai_comparison` sweep is a cross product of every dimension --
  AIs x formats x generators x map seeds x sizes -- which reaches into the
  hundreds of thousands of games for a broad generator sweep. Set `sample_games`
  (an integer count) to draw a bounded random sample instead: each sample game
  independently draws its AI matchup and generator; the standard format and size
  remain 1v1 and 128x128 unless the configuration overrides them. Games are submitted
  as a single inline-generation job (`--generator`/`--map-seed` embedded directly
  in `game run`, no separate `generate_map` dependency). `sample_seed` (default
  1) makes the draw reproducible; `sizes` is a list of `generator_params`-shaped
  dicts to choose from per sample (defaulting to one 128x128 size, or to an
  explicit `generator_params` value); `generators` still
  restricts the pool as in the exhaustive design. By default, **all playable
  generators** in the supplied build are included; editor-only generators are
  excluded. Catalog controls are not a compatibility guarantee: preflight the
  chosen size and colony count against the engine, publish exclusions, and use
  `generator_overrides` for documented generator-specific settings (for example,
  `{"52": {"slant": 0}}` for Rice Terraces at 128x128). Never silently replace a
  failed generator with another one. A single-generator cohort is a specialized experiment and must not
  be presented as general AI strength.

  Sampled studies can set `ffa_colonies: [4, 8, 16]` to draw the FFA colony
  count independently (integers 2–16; the default remains four). Duels and 2v2
  retain two and four colonies. Set `seat_sampling: "with_replacement"` for
  independent AI draws, allowing repeated AI implementations even when the pool
  contains enough distinct entries. The default `"distinct"` samples without
  replacement when possible and falls back to replacement for smaller pools.
  In 2v2, the two team AIs are sampled, preserving homogeneous pairs. These two
  controls require `sample_games` and cannot be combined with `balanced_duels`.
  Explicit `ffa_colonies` requires an `ffa` entry in `formats`. Counts above four
  need the same generator/size preflight as any other nonstandard cohort.

  New-game designs may provide `experiments: ["farm-areas"]`; each requested
  key must exist in every pinned build's `experiments` catalog. The job adapter
  forwards one `--experiment KEY` per entry. Saved games retain their embedded
  experiment set and reject overrides, including an explicit empty list.

  Set `balanced_duels: true` for a balanced 1v1 sample: `sample_games` must be
  even, with one map size and at least two AIs. Each block plays the same map and
  game seed twice, swapping the AI starting sides. Generator quotas differ by at
  most two games, as do total AI-pair quotas. Block order and build assignment are
  shuffled reproducibly. For 10,000 games across 66 generators and eight AIs,
  this means 150 or 152 games per generator and 356 or 358 games per AI pairing.
  Bootstrap uncertainty must resample whole paired blocks. Build IDs repeated
  in `builds` retain their weighting for heterogeneous worker capacity.
  `ais`/`formats`/`ticks`/`candidates` are shared with the exhaustive path and mean
  the same thing. This is the *only* sampling path for `ai_comparison` -- do not
  add a second, separate script that reimplements job construction outside this
  Planner; `reanalyze` and every other analysis entry point already work on
  either design unchanged, since both produce the same job/label shape.
* `fairness`: colonies 4, ai nicowar. Reuses each identical map across every team
  rotation. Generator 15 supplies symmetric controls. Preserves the legacy tested
  multinomial/Fisher methods, unbiased squared-bias estimator, sampling floor,
  bootstrap, BH and Holm corrections. Reports per-map unfairness separately from
  pooled generator-start and engine-team bias, including decisive-only results.
* `generator_stress`: explicit `grid` (Cartesian parameter lists), `sample`
  (parameter choice lists), samples 32, sample_seed 1, baseline generator_params {}.
  Defaults candidates 0 and timeout_seconds 60. Every seed/configuration is retained;
  validation, generation failure, crash and watchdog timeout are separate outcomes.
  Map output is opt-in. Reports timing and map-statistic distributions.
* `ablations`: target ai or generator; players [cortex,nicowar]; player 0;
  baseline_ai_params {}; one_parameter maps names to lists; grid for factorials;
  sample/samples/sample_seed for seeded random variants; optional disjoint
  held_out_map_seeds. Every variant shares seeds/rotations, and AI variants share
  exact map artifacts. format defaults to 1v1 for two players, otherwise ffa;
  alliances is optional. Paired effects, raw pairs, intervals, configurations and
  failure rates are exported. No automatic best-variant selection occurs.

Engine-declared winners are authoritative under every policy. The default
`win_probability` policy ranks unresolved survivors at the tick cap by their final
model probability; exact ties stay tied. Eliminated colonies still rank by
elimination tick. Allies share one probability, counted once in 2v2. Alternatives
are `prestige` (prestige, population, finished buildings), `survivor_draw` (tie all
survivors), and `military` (warrior attack, HP, count). Historical results without
probability statistics require an explicit legacy policy; missing predictions are
never silently replaced with economic scores. Reports record policy name/version,
raw termination, and engine/adjudicated status.
Standard 1v1 ratings fit all observed outcomes at once with the Bradley–Terry
logistic model: each game has equal weight, ties score 0.5, and input/completion
order has no effect. Ratings are centred on 1500 with 400 points per tenfold odds;
there is no K factor or regularization. Disconnected or undefeated groups have no
finite maximum-likelihood fit and are reported as unavailable, not assigned a
fabricated finite score. Formats/builds remain separate unless pooling is explicit.

Duel intervals resample complete swapped-side pairs within each generator, retaining
its quota. Incomplete pairs contribute to the point fit, but not uncertainty; both
counts and unavailable intervals are explicit. These are sampling intervals for
the chosen generator mix and adjudication policy, not a guarantee of unbiased
strength on every map. `tools/tournaments/duel_ratings.py` is the standard-library
implementation, also usable to refit retained outcomes without running games.
Historical sequential Elo/uncertainty remain separately labelled diagnostics;
non-duel formats retain K=32 pairwise updates. Ablation intervals resample paired
effects.

Legacy entry points remain: `ai-benchmark.sh` delegates to this package (its legacy
capped-game summary remains a draw); `cortex-knob-search.py` invokes that benchmark;
`map_fairness_tournament.py` and `map_generator_study.py` use shared local execution
and the main binary. Their existing statistical/report formats remain available.
The old study executable remains a regression wrapper around production code.
Structured games assign AI players directly in team order; the legacy random-game
driver also inserted a passive local player and polled team 0 last. Treat migrated
benchmark games as a new cohort when comparing historical results; that order can
affect play even with the same map and seed.

## Extend and verify

A minimal experiment uses `from tools.tournaments.model import job, grid,
seeded_samples, rotations`, constructs jobs, and submits once via
`Coordinator.submit(directory, manifest, bundles)`. Consumers use
`Results(directory)` (manifest-ordered committed results), `Results.attempts()` and
`Results.open_artifact(record, path)` for transparent gzip reads. Inspect
`tools/tournaments/results.py` for the exact streaming interface.

To add a job type, implement an adapter with `validate(job)`,
`command(job,bundle,attempt_dir,inputs)` and `collect(job,attempt_dir)`; register it
in `jobs.py` with `register_job_type("name", Adapter())`. Its immutable bundle must
advertise that capability. Include its module in the package and import it during
registration. Worker packaging automatically includes package-level Python modules;
changing them creates a new protocol package identity. The experiment script owns
planning/statistics, not SSH, process pools, retries or transfers.

```sh
python3 test/test_tournaments.py
python3 test/test_tournament_pipeline.py
python3 test/test_map_fairness_tournament.py
scons -j4 release=1 server=0 engine-tests
python3 test/run_tests.py --filter 'TournamentCompatibility/*'
python3 test/tournament_cli_integration.py --output artifacts/tournament-cli
```

Hosted native verification runs these with the production Linux binary and retains
CLI evidence when selected by the coverage policy. The
opt-in `test/tournament_reliability_pilot.py --help` describes the localhost/five-host
pilot; it intentionally kills only its own processes and simulates connection loss
by withholding coordinator contact, without rebooting hosts or changing networking.
Engine compatibility requires actual identical initial states/seeds/orders and
per-tick checksums across platforms; passing transport tests or builds is insufficient.
Keep measured coverage, command output and other run-specific evidence in the
ignored `artifacts/` workspace or in pull-request attachments.

## Resource-claim experiment preparation

`tools/resource_claim_patterns.py` is temporary offline experiment tooling. It
does not enable claims, run games, or select a shipping policy. Keep its generated
catalogs under `artifacts/`; remove the experimental generator and its tests when
the final claim policy is selected. Its default small grid is a preparation
smoke check, not the full requested search:

```sh
python3 tools/resource_claim_patterns.py --output artifacts/claim-pattern-pilot \
  --radii 0 1 2 3 4 6 8 --strides 1 2 3 4 --origins unit resource midpoint interleaved \
  --directions 0 1 2 3 4 5 6 7 --headers
```

For full grids, the radius and positive sparsity scales are
`0,1,2,3,4,6,8,12,16,24,32,48,64,96,128,192,256`; omit zero for sparsity.
Split large grids into explicit catalogs by family/radius rather than removing
the default 256 MiB payload and 100,000-variant bounds. A failed limit leaves
`summary.json` marked incomplete. Existing output directories are never replaced.
Each JSONL manifest entry records its full parameter choice and SHA-256 array
identity; equivalent arrays share one packed binary and optional C++ header.
Headers contain separate signed 16-bit X/Y and byte origin arrays. Directions are
the eight compass octants starting at positive X; cones cover 90 degrees, and
perpendicular cones include both sides. All shapes are clipped to a Euclidean
disk. Every pattern starts with the unit's zero-offset probe. Resource/midpoint
origins follow it, and interleaving checks all three origins per offset. Reverse
and shuffle variants preserve that initial probe. Random-block sparsity samples
one offset from each consecutive block of the same dense ordering. Geometry is
cached across origin and sparsity variants; seeds and enumeration order do not
consume or alter the simulation RNG.

A pilot cohort can use the normal `ai_comparison plan` path with
`sample_games: 32`, an explicit farming-AI pool, `formats: ["ffa"]`,
`ffa_colonies: [4,8,16]`, `seat_sampling: "with_replacement"`, and
`sizes: [{"width":8,"height":8},{"width":9,"height":9}]` (map exponents).
Preflight generators for those sizes/counts, freeze the resulting requests, and
use `win_probability_permille: 0` only for the deliberately late-game cohort.
Retain the usual 950 threshold in the separately labeled representative cohort.
Add requested claim telemetry only after the instrumented engine advertises it;
a planner manifest alone does not produce claim-policy measurements. Use the
shared storage/CPU allocation controls above and estimate campaign volume from
the pilot before launching broad runs.

The temporary integrated planner freezes a 32-game pilot (16 broad, 16 farming
stress) or a 1,000-game round (500 each) using those same sampling APIs:

```sh
python3 -m tools.tournaments.resource_claims plan --bundle BUNDLE --games 32 \
  --seed 1 --output artifacts/claim-pilot.json
python3 -m tools.tournaments.resource_claims analyze RESULTS \
  --output artifacts/claim-pilot-analysis.json
```

Planning does not dispatch jobs. For the next policy cohort, use the conditional
pool/selector in `tools.tournaments.resource_claims`: draw complete tuples with
replacement from each frozen seeded family, preflight them, and retain the first
valid tuples until the requested 16/8/8 (or 500/250/250) counts are met. The
attempt cap is ten times the target; insufficient valid draws fail selection.
Keep rejected tuples and reasons. Missing preflights and infrastructure failures
block selection instead of being counted as invalid map draws. Report realized
valid distributions and rejection rates; proposal weights do not describe the
conditioned accepted distribution. Freeze identical valid inputs across policies.
Map-size repairs in initial calibration are diagnostics only, not policy data.

Preflight every sampled generator/count/size;
retain failed requests and apply explicit `--generator-overrides` where needed.
Broad games draw native AIs, formats and 128/256/512 square sizes independently;
stress games draw Nicowar/Econo/Maxima/Cortex/Cabino from the pinned catalog,
4/8/16 colonies and 256/512 square sizes. Half the stress games explicitly enable
`farm-areas` for pooled farms; the other half retain protected farming with no
experiments enabled. Thus the pilot has 16 broad, eight protected and eight
pooled games; a full round has 500/250/250. Within stress, the twelve
farm-mode/size/count strata each have design probability 1/12; only the farm-mode
counts are fixed, while size/counts are sampled. Broad and
stress use 950 and zero early-victory thresholds, respectively. The manifest
freezes sampling, stopping rules, limits and 50/50 cohort weights; reports retain
cohorts separately. The resource-claim job adapter requires a complete compressed
trace whose limits and completed counts match the engine's result.

`tools/resource_claim_offline.py` provides the next-stage offline kernel. It
writes exact packed arrays into bounded shards (64 MiB by default) with a
SQLite deduplication/alias index, a 2 GiB total allowance and a free-space reserve.
Incomplete or modified catalogs cannot be scored. Relative directional aliases
select precomputed views using each frozen event's nearest octant. The scorer
resolves published destinations and their geometric ranking once, computes both policies with vectorized
tails and a bounded shared-prefix cache, and emits per-game aggregates rather
than per-event full-grid outcomes. The immutable geometric plane uses four bytes
per map tile and counts toward the event-batch memory allowance. Oracle path
labels never participate in this ranking. Its source/cohort-pinned splits use whole
game IDs. Pure fixture tests cover all 240 pilot profiles against an independent
scalar reference; native frozen-event parity and capture are separate gates,
not established by those Python tests. Full-grid CPU/space budgets must be
measured before choosing a deep capture rate. The kernel selects no policy.

Offline full-grid reduction groups a game's frozen events by nearest bearing,
scores each distinct physical array once for that group, then maps results into
logical alias indices. Compact integer/float game matrices feed global sums and
valid-game counts; unobserved means remain unavailable and no bootstrap interval
is invented from those global sums. Retain per-event data only for a later
shortlist re-score on held-out games/features. `deep_budget` must pass before any
deep-capture dispatch: it reserves the actual catalog bytes, a worst-case frozen
input allowance (including all candidate rows), aggregate output, and free-space
reserve. Compression is not assumed. These allowances are separate from ordinary
tournament telemetry and must be included in the overall campaign allocation.

Reversible discovery screening is available through the NumPy-enabled development
environment:

```sh
python -m tools.resource_claim_screen RESULTS CATALOG SPLIT.json OUTPUT
```

Use a new output directory and one frozen live-policy configuration. The tool
never opens held-out event payloads. Global cuts require at least 16 usable,
uncensored discovery games and at least five in **each planned gameplay cohort**
from the split (broad, protected farming and pooled farming in the initial plan).
Missing cohorts are reported explicitly; broad-only evidence cannot eliminate
global farm candidates. Global cuts also require at least one selected event at
or after tick 60,000 with locally sparse or fully claimed legal goals in each
planned farming cohort:
protected farming must concern food (material 1 in the frozen registry), and
pooled farming must actually use pooled mechanics. Growing requested-material
stock on own-team forbidden cells is direct evidence of configured regrowth
potential, not guaranteed habitat success or AI intent. Legacy traces only count
nearby forbidden paint; growing food beside that paint is an explicitly labeled
adjacency proxy, since the painted cells may contain wood or be empty. Reversible
pilot allocation may use this proxy with factor/niche backstops and eliminated
audits retained; it cannot qualify final protected-food scenario coverage. A
known zero direct count overrides the weaker proxy. The final reservoir ordinal
is joined to its ordinary trace features; selection is never changed after
seeing the event. Family labels alone do not establish realized farm coverage. The preregistered
local criterion is a toroidal Euclidean radius-16 disk around the nearest source:
at most four legal goals is locally sparse; more than four legal goals with zero
available goals is a separate fully claimed regime. This is an explicit screening
criterion, not a universal ecological definition. Record distinct disk area and
legal/available counts for radii 16 and 32 around the unit, nearest source and
wrapped midpoint; retain global counts as context. Remote dense plots must not
hide local scarcity. For pooled farming, a late event also qualifies when the
nearest frozen candidate is an eligible access in a component with zero
available physical sources and the ordinary row confirms pooled mechanics.
Other nearby components may still have stock; their density does not negate
exhaustion of the selected pool. Report that component’s raw, legal and available
counts separately from the radius-16 context.
The report retains material, reason, pooling, tick and legal-density regimes;
this minimal screening safeguard does not replace final scenario coverage. The first pass retains compact global aggregates. A
second pass verifies bounded witnesses: failure/reachability outcomes must match,
the eliminated candidate must never beat its witness in selected-access travel
or probe count on any observed game, and its total probes must be at least 25% higher. Preserve
search-family/radius/sparsity/origin/direction representatives, observed niche
winners, and a deterministic 10% audit sample of eliminated candidates. This is
reversible compute allocation, not policy promotion or a final confidence claim.
Reopen candidates after claim enforcement changes the game distribution.

For bounded pilot reuse, `--score-cache DIRECTORY` stores immutable per-game
score matrices under a 2 GiB cache allowance and 8 GiB free-space reserve. Entries
bind the complete catalog identity, scorer and decoder sources, frozen event
bytes, exact decoded inputs, alias-view bytes and provenance, with a matrix
checksum. Malformed alias views are rejected. Legacy cache entries require an
explicit provenance upgrade or recomputation; they are never silently reused. Provisional discovery precomputation can fill
this cache before coverage gates pass; no elimination follows from precomputation.
Both screening passes reuse exact entries. Reserve cache and retained captures
inside the campaign budget. This is a small-pilot optimization, not permission
to retain full-grid matrices for a thousand games; large rounds still stream
compact aggregates and re-score shortlists.

The optional `tools.resource_claim_physical_screen` API prepares a stronger
proposal; it does not apply an allocation or dispatch games. It tests one
preordered witness per candidate using a one-sided 95% paired game bootstrap,
stratified by the frozen family mix. The proposed bound permits at most 10% more
mean selected-access travel in exchange for at least 25% fewer raw probes.
Fallback, unreachable-access and missing-label status must match on every game;
unknown candidate costs prevent a cut. A complete, uncensored frozen event whose
exhaustive oracle proves there is no reachable, legally harvestable unclaimed
alternative may be excluded from travel comparisons using
`prove_no_reachable_alternative`. The proposal binds that proof to the record,
manifest and event identities, retains every game's failure and probe checks,
and separately requires at least 16 travel-applicable games and five per planned
family. No missing cost becomes zero. Each family must also satisfy the travel
bound in its observed applicable-game mean. These are exploratory allocation bounds, not simultaneous
winner confidence intervals. Frozen factor and observed-niche representatives
remain protected. An array is removable only when no surviving alias or policy
uses it in any direction, and a deterministic 10% sample of removable physical
arrays remains for drift audits. Its probe-entry reduction is explicitly a work
projection; CPU savings require a separate measured comparison. Applying this
stronger proposal requires the agreed screening decision and preserves the
original report, frozen cohort and unopened holdout.

The screening report measures full versus retained-plus-audit offline kernels on
the same frozen event before a larger rollout. These timings do not establish
shipping policy costs. Dense/sparse screening bins use legal pickup goals from
frozen candidate eligibility, with pooled availability resolved per component;
raw stock density remains a separate feature. Protected crops can make raw stock
density high while legal gradient goals are sparse. Missing goal or exposure
information is unavailable, never zero.

Temporary deep capture uses `resource_claims_deep_context` with source revision,
bundle, cohort and game identities and requires capability
`resource_claims_deep_version: 1`. It forwards `--resource-claims-deep-context`;
require `resource-claims-deep/manifest.json` in retained outputs. One independent
minimum-hash event is retained per game among sampled contested published-XY
events, with priority chosen before admission limits. Existing oracle results
supply the binary; no additional Dijkstra runs. Missing fields/XY are population
exclusions, while dropped eligible events censor the reservoir. The collector
rejects censored or incomplete captures and checks final priority and SHA256.
The binary has a 10 MiB ceiling, its replacement directory a 24 MiB ceiling,
and an 8 GiB free-space reserve. A completed zero-eligible game has no binary;
it remains missing policy evidence. Native roundtrip parity with the 240 pilot
outcomes must pass on real captured events before full-grid scoring. The portable
`claim_capture` validator ships inside the worker archive and uses only the Python
standard library; offline NumPy helpers are loaded separately. Verify collection
from the actual archive with `python -I -S`, outside the checkout, so missing
package files cannot be hidden by contributor imports.

For stratified diagnostic acquisition, explicitly add
`resource_claims_deep_stratified: true` alongside the context. This requires
`resource_claims_deep_stratified_version: 2` and forwards
`--resource-claims-deep-stratified`. Context alone retains the V1 behavior.
V2 keeps one independent minimum per each of 16 disjoint bins: before tick 60000,
food/fruit/other by movement/pickup; later, fruit/other by phase and food by
pooled/protected-growing-proxy/other-or-unverified by phase. Pooled evidence takes
precedence; the protected proxy means nearby forbidden requested-material stock
with configured regrowth potential, not demonstrated growth or AI intent.
Classification and population counts precede admission limits. Stale-goal pool
interpretation remains explicitly unknown.

V2 manifests retain every bin, including empty and censored bins, and bind each
unchanged V1 binary record to its V2 stratum. The strict reader rejects any
censored bin, inconsistent counts, missing minima or unexpected files. Reserve
160 MiB final deep storage plus 24 MiB serialized replacement space, separately
from ordinary traces and logs. Pending encoded payload is bounded by eight
10 MiB records. A separate 128 MiB admission reservation bounds accounted input
payload (vector capacities and serialized metadata); it is not an RSS limit.
Allocator/JSON/map-node overhead, shared registries, retained snapshot pools and
oracle scratch remain separate and require measured memory calibration. A
reservation underestimate fails the capture rather than silently exceeding its
accounted allowance.

The optional V3 `resource_claims_deep_top_four: true` adds
`--resource-claims-deep-top-four` and requires both stratified capture and
`resource_claims_deep_topk_version: 3`. It retains the four smallest distinct
priority/event-ordinal pairs per bin, or all events when fewer than four are
eligible. The default remains the byte-compatible V1/V2 path. Sampling frequency
and the eligible population do not change; invalid or missing destination fields
remain outside this capture stream. No additional oracle search is needed, but
more retained events cause additional exposure analysis, encoding and disk writes.

V3 reserves 640 MiB for 64 final records plus 24 MiB serialized replacement space;
the 128 MiB input and 80 MiB pending encoded payload allowances remain unchanged.
Only small ranking metadata stays in the reservoir. An evicted pending result
cannot overwrite a newer selected event. The separate strict V3 reader requires
exactly `min(4, N_s)` current records in every uncensored bin. Each has nominal
weight `N_s / min(4, N_s)` under the independent uniform-priority model; ordinal
tie-breaking is deterministic and observed priority ties are reported. Admission
loss, missing writes or corrupt records cannot be repaired by dividing by the
number of surviving records. Freeze larger per-attempt storage allowances before
running V3; existing cohorts retain their original limits and readers.

For uncensored bins, eligible-count expansion describes the ordinary sampled
population under the independent hash-priority model; it does not correct unknown
admission exclusions. Aggregate within game, then by independent parent world.
Policy arms and multiple bins from the same world are dependent observations.
The native disk writer requires the same 8 GiB free reserve; browser MEMFS tests
of ordinary observation do not establish browser deep-file capture coverage.

New-game claim cohorts explicitly freeze `material_destinations: true` and require
the bundle's `material_destinations_version: 1` capability. This temporary mode
forwards `--material-destinations` and cannot record replay output. Saved-state
jobs retain their embedded mode and reject an override. Reports distinguish
published XY decisions from missing XY or scalar projection; scalar data is
throughput calibration only, never policy-selection evidence. Hold the metadata
mode equal when comparing shadow diagnostics on versus off.

Later active-policy cohorts use `resource_claims_active: "prototype-stationary-v1"`
with material destinations enabled and a `resource_claims_probe` sampling object
in place of `resource_claims_shadow`. Pinned catalogs must advertise the active
policy and probe capability. Active policy overrides are forbidden on saves;
saved probes retain their authoritative mode. Collectors verify the canonical
`resolved.material_destinations_enabled` and `resolved.active_claim_policy`
fields, result enforcement, and each event's enforcement/policy. New nullable
schema-level enforcement is resolved per event; historical shadow traces remain
supported. Different enforcement modes are never silently pooled in analysis.

For configurable live trials, freeze the complete canonical JSON object in
`resource_claims_policy` instead of `resource_claims_active`. It forwards
`--resource-claims-policy-json`, requires material destinations and the pinned
`resource_claims_policy_config_version` capability, and rejects saved-state or
replay overrides. Obtain the canonical fields and compiled pattern asset digests
from the executable/catalog; do not rely on implicit defaults. The standard-library
`claim_policy` validator checks the full configuration and pinned
`resource_claims_policy_patterns` inventory. Collectors attest the exact canonical
configuration and SHA256 in results and diagnostic events. Different configuration
digests are analyzed separately. Configurability does not select or approve a
shipping policy.

Policy source version 1 retains its original canonical fields and digest. Source
version 2 additionally identifies bounded failed-route recovery: four rejected
accesses with a 128-tick soft expiry, at most four recovery route attempts per
64-tick window, a 16-tick backoff, and first-available recovery scanning from a retained
cursor. These fixed parameters are explicit canonical fields, not additional
free sweep factors. A capability version 2 build accepts both policy versions;
version 1 builds cannot run version 2 configurations. Compare versions as
distinct interventions rather than combining their measurements. Successful
periodic refreshes before any failure remain unrestricted so cadence factors
retain their meaning. Reaching a resource within pickup range needs no route
admission and bypasses soft failed-route exclusions.

The result's `gradient_execution.destination_published_bytes` and
`destination_pending_reserved_bytes` describe current destination allocations
and immutable pending reservations when metadata is enabled. Reading them does
not publish or join background work. They exclude retained snapshots, scratch
space and allocator overhead; use whole-process RSS measurements for the memory
budget and paired release-build CPU measurements for runtime overhead.

Plan live trials separately from execution with `tools.tournaments.resource_claim_live`:

```sh
python -m tools.tournaments.resource_claim_live factors CONTROL.json FACTORS.json --patterns SHORTLIST.json
python -m tools.tournaments.resource_claim_live interactions CONTROL.json BINARY_FACTORS.json INTERACTIONS.json
python -m tools.tournaments.resource_claim_live plan COHORT.json SPLIT.json BUNDLE.json CONFIG_BATCH.json PLAN.json --additional-reserved-bytes BYTES
```

`CONTROL.json` is a complete canonical configuration. The optional shortlist is
an array of catalog `{name, digest}` assets; config batches contain a `configs`
array of `{label, policy}` entries. Factor generation declares independent and
coupled destination/path refresh intervals, fallback, pending movement, route
target, primary-gradient use, selection rule and shortlisted patterns. It does
not queue their Cartesian product or launch games. First eliminate clearly poor
routing/refresh candidates using separately labeled short movement fixtures and
frozen event paths; then supply a small explicit whole-game config batch.

Experimental policy source version 4 requires the explicit boolean
`direct_resource_probes`, together with the source 3 adjacent-pickup boolean and
existing recovery fields. Versions 1–3 reject this new key and retain their
canonical identities. A version 4 executable accepts those older configurations;
older executables reject source 4. The prototype first tests the published XY
destination at each pattern entry. First-available selection checks the entry's
own resource after the XY candidate is rejected; closest-geometric selection
considers both candidates at every entry, with deduplication. Raw-enabled source
4 uses a paired recovery cursor (`2 * entry + phase`), bounded by twice the
pattern length, so a failed XY target can resume at the same relative pattern entry's raw
resource. Movement or a changed nearest-resource bearing can change that entry's
absolute map coordinate; this does not preserve a stationary world-space scan.
Older sources and raw-disabled source 4 retain the original cursor bound.
The existing route-attempt limits are unchanged. This is a diagnostic ablation, not an adopted
policy. Existing offline pattern scores remain XY-only and must not be presented
as measurements of the hybrid selector. The additive `raw_resource_probes`
footer covers raw-enabled full-probe selections only; zero counters in raw-off or
movement-only runs mean unobserved work. `raw_best_candidate_improvements` counts
provisional winners, while `raw_final_selections` counts a raw winner only after
reservation commit succeeds (including a successful unchanged held claim).

Experimental policy source version 5 uses explicitly typed `mixed-v1` entries,
with `pattern_frame` set to `mapaxes` or `nearest-d4-v1`. Each entry encodes its
initial unit/nearest-resource origin and its gradient/direct check separately;
legacy five-byte probe arrays cannot be reinterpreted as this format. The pinned
`resource_claims_policy_mixed_patterns` catalog binds name, format, frame and
asset digest. Source 5 requires the adjacent-pickup boolean and recovery fields,
rejects `direct_resource_probes`, and initially accepts only `first_available`.
The digest includes the format/frame prefix and ordered little-endian signed
coordinates plus tag bytes. Versions 1–4 retain their existing canonical
identities. The illustrative registry entry is plumbing for evaluation, not an
adopted policy or evidence of a gameplay improvement.

Policy source version 3 adds the explicit boolean `opportunistic_adjacent_pickup`;
it retains the version 2 recovery contract. When enabled, a worker can exchange a
distant reservation for available material within pickup range. An already
reachable held reservation takes priority, and a failed exchange preserves the
old claim. Other workers' reservations remain protected. Versions 1 and 2 retain
sticky pickup behavior and their original canonical identities. Version 3 requires
bundle policy capability 3; batches testing this option require the transport
movement audit on every arm. This is an experimental variant, not a selected
default or a demonstrated throughput improvement.

Three optional keys apply to every source version and are omitted from the
canonical JSON at their defaults, so existing policies keep their digests (the
Python `canonical_policy` applies the same rule). `refresh_switch_margin_permille`
(integer 0–900, default 0) adds hysteresis to destination refreshes: while a claim
is held, another access is eligible only when its toroidal Chebyshev distance `d`
satisfies `1000*d < (1000-margin)*d_held`; the held access stays eligible, and when
the margin rejects every alternative the claim and route are kept (a
`goal_refresh_same`). `terrain_component_prefilter` (boolean, default false) skips
an access when no cell of its 3×3 stance shares the worker's terrain-only connected
component (8-neighbour, toroidal, per movement class, ignoring units, buildings and
resources). The labelling is derived per map, rebuilt when the terrain revision
changes and never saved. `claim_oversubscription_permille` (integer 1000–100000 or
`"unlimited"`, default 1000) relaxes the capacity gate: a source admits another
same-material claimant only while its existing claimant count stays below
`floor(spendable*permille/1000)`, where `spendable` is the stock above the shared
seed floor. 1000 is the exact one-claimant-per-unit rule; `"unlimited"` admits any
number while at least one unit is spendable. Exclusive sources, seed floors and
infinite yields behave as before, and selection, acquisition, stock-decrease
revalidation and the overcommit telemetry all use the scaled limit. Surplus
holders that arrive after the stock is gone fail the harvest and release
(`resource_depleted`). Builds advertise all three in `resource_claims_policy_options`.

Claim overhead experiments. These change only opt-in values, so every existing
policy keeps its meaning and digest. Builds advertise each one by name in
`resource_claims_policy_options` (a new value of an existing key by
`<key>_<value>`), and the Python contract refuses a policy whose build does not.
`pending_step: "gradient"` (advertised as `pending_step_gradient`): while the
held claim's route is computing or awaiting route admission, the worker takes
the ordinary natural-material gradient step for the claimed material, the same
step claims-off movement takes (demand, movement outcome and reactive refresh
included), instead of a zero-displacement wait. The published route then rejoins
through the existing reanchor window or is repaired from the new position. With
no gradient step available the worker waits as before. `"redecide"` is not
implemented.
`max_claim_distance_factor_permille` (integer 1000–100000, default 0 = off)
rejects, in both selection loops and before any claim resolution, a candidate
access whose toroidal Chebyshev distance `d` from the worker satisfies
`1000*d > permille*d_nearest + 1000*4`, where `d_nearest` is the distance to the
nearest material destination recorded at the worker's cell by the destination
plane (the plane's `x/y` at the worker) and 4 tiles is a fixed slack for nearby
stock. A decision with no recorded nearest destination is not capped. A held
claim that the cap rejects on refresh is kept, like any failed refresh.
`traffic_sidestep` (boolean, default false): when the next route cell is blocked
by traffic (`routePassable` with frozen units false, geometry intact), the worker
steps instead to a free legal neighbour that is on, or one step from, a later
route cell within the 8-cell reanchor window (furthest such cell first, then a
fixed neighbour order); the next decision rejoins the route at that cell. With no
such neighbour it waits as before. `traffic_wait_limit` (integer 0–64, default 0
= unlimited) counts consecutive traffic waits per route; on reaching the limit
it repairs the route from the current cell, planning around the units sampled at
that moment (frozen-unit occupancy) rather than releasing the claim: a repair
costs one bounded search and keeps the reservation, whereas a release would also
discard the claim, reselect and route again. If the repair finds no path the
ordinary route-failure release applies. The counter is saved and checksummed only
for policies that set the limit.
`fallback: "gradient"` (advertised as `fallback_gradient`): a decision that
finds no claim (including one restricted by route-admission cooldown) runs the
ordinary claims-off material movement for that decision, keeping the job; it
neither idles nor releases, except where claims-off movement itself abandons an
unreachable material. Failure accounting and the next decision's selection
attempt are unchanged. Arriving next to stock, the worker reserves it through
the ordinary pickup check.
`claim_gradient_demand` (boolean, default false): each decision pursuing a held
claim (not yet adjacent, route not failed) registers movement demand for the
claimed material's natural gradient field. When the route is published, the
decision also records the same movement evidence `pathfindMaterial` records
(moved, at-goal, goal-valid) and requests the field refresh on an invalid goal,
so reactive refresh sees claim workers as it sees gradient followers. Decisions
whose route is still computing record demand only.
`claim_trigger` (`"always"` by default, or `"contention"`) with
`contention_min_workers` (integer 2 to 16, default 3) and `contention_radius`
(integer 0 to 4, default 1): with `"contention"`, a fetching worker without a
claim takes the ordinary claims-off material step and pickup for that decision
unless its ordinary target is crowded. A worker's target is its claimed access
while it holds a claim, otherwise its valid ordinary `targetX`/`targetY` (the
target projected by its previous ordinary decision); a worker with no valid
target is never crowded. The decision is crowded when at least
`contention_min_workers` same-team workers fetching the same material (going to
or harvesting at a resource, claimed or not, itself included) target cells within
toroidal Chebyshev `contention_radius` of its target; the worker then takes the
claim path with every other configured option. A claimed worker stays in claim
mode until the claim is released or harvested. The counts read a per-team target
index rebuilt from unit state as each team starts its unit step, so it is derived
and never saved. The two parameters are rejected unless the trigger is
`"contention"`. Live probe summaries report `live.contention_counts`
(`ordinary_decisions`, `contention_tests`, `contention_triggers`,
`claim_mode_decisions`; version `live.contention_version`).

Live probe summaries count every `released` event by reason in
`live.release_reason_counts` (names in `live.release_reason_names`, version
`live.release_reason_version`; the counts sum to the `released` event count), and
released journey rows carry `release_reason`. `live.selection_filter_counts`
reports accesses skipped by the terrain prefilter or the refresh margin and
refreshes kept by the margin. Older traces without these fields remain valid.

Interaction confirmation accepts three to five binary scalar factors and records
the half-fraction generator and aliasing. It is a diagnostic design, not proof
that aliased interactions are absent. Whole-game plans preserve every selected
original tuple, seed, seat, stopping rule and discovery identity across new
build/job IDs. Discovery is the default; held-out games require explicit
`--stage confirmation --partition holdout`. They never repair or resample a tuple
based on a policy result. New captures require an attested bundle with compiled
pattern digests. Deep capture defaults off; ordinary bounded probes and whole-game metrics remain
enabled. Confirmation stages can opt in with `--deep-capture --round-id NAME`.
The pinned bundle must advertise deep capture. Contexts use the new source/bundle
and a per-policy/round cohort identity while preserving original game IDs and
discovery roles. Each policy gets its own frozen split in `design.deep_splits`;
extract that split and analyze one policy cohort at a time. Context identities
remain stable across small config batches. The deep manifest becomes a required
artifact and remains inside the existing attempt/campaign caps.

Planning defaults to 256 jobs, a 32 GiB campaign and an 8 GiB free-space reserve.
It reserves every attempt's artifact/log cap plus an equal transfer/spool copy
and the explicitly supplied other reservations. Automatic retries are disabled;
retrying retained failures needs a new storage review. A batch that exceeds a bound is
rejected in full; select fewer policies, not fewer paired games. Existing large
trace allowances may permit only a few policies per batch. Submission remains a
separate operation after native validation and resource review; generated plans
do not authorize dispatch or select a shipping policy.

Active-probe bundle capability version 2 adds linked outcome rows; planners
accept versions 1 and 2 and reject unknown versions. Version 1 bundles retain
the previous trace contract. Temporary full-probe traces may advertise `decision_outcome_version: 1`.
Their separate `decision_outcome` rows record the actual immediate selection,
held-claim change, route result and applied fallback. Join them to sampled
`decision` rows by ordinal; either row may arrive first. Collectors verify owner,
preselection task, tick, material, phase and nearest-access identity, reject
duplicate links, and bound retained links by the frozen sample cap and a
one-million-row reader ceiling (exceeding it fails explicitly). These rows
do not increase the decision `submitted` or `completed` counts.

The optional outcome summary keeps fixed material/phase counters separate from
sampled observations. Counter categories overlap: a failed refresh can retain the
old claim, so `no_candidate` and `held_same` can both increase.
`cooldown_wait` is independent of whether selection was attempted: the selector
can still examine adjacent pickups during a cooldown. Unlinked active
decisions remain explicitly unavailable (including direct diagnostic calls);
legacy/shadow decisions are unobserved by design, not evidence of failed active
selection. A selected claim is captured before a subsequent route failure can
release it. `no_candidate` means the selector or pickup preparation returned false; it
does not establish global resource absence (recovery filters or unavailable
destination metadata can also prevent selection). A `pending` route result can
mean a blocked cached next step as well as unfinished route work; it does not
by itself measure CPU waiting. Immediate selection and route results do not establish eventual
harvest, delivery or feeding causality. Older traces without this schema retain
their existing report shape.

Live diagnostic traces may also advertise `live_journey_version: 1` and an
ordered event-name table. Bounded journey rows describe committed movement for
sampled acquisitions, with a static acquisition-time baseline to the held pickup
access. Journeys are separate from ordinary decision counts. Readers validate
unique owner/task identities, admission/phase counters, partial terminals and
nullable baseline costs; collectors bind each journey's policy digest to the
resolved executable configuration even when there are no decision rows.
Timeout/end-of-segment costs describe partial movement, not completed-route
regret. Workload-dependent admission exclusions censor sampling. Aggregate by
game; sampled units are not independent experimental replicates. Movement totals
cover held claims only and cannot establish total pursuit travel for disabled or
shadow behavior.

Full decision probes may advertise `blocker_census_version: 1`. Ordinary decision
rows then include an authoritative claimant census for the selected physical
source and, when applicable, its farm component. Counts distinguish the
requester, other claimants, potentially conflicting reservations and same-team
versus other-team owners. Other teams are not necessarily hostile. Infinite
sharing and exclusive deposit consumption have different conflict rules; a
seed-reserve shortage need not involve another claimant.
Nearest-access reservability uses owner-aware component availability and is
reported separately from static reachability. A rejected raw donor does not
prove that the same pooled pickup access requires an alternative.

Fixed histograms cover all potentially conflicting owners before the bounded
owner examples are selected. Example exclusions are explicit. Distances are
current toroidal geometric distances to each claimant's own source and pickup
access, rather than path distances. Observed acquisition ages are exact;
inherited ages are labeled lower bounds, with unavailable ages separate. These
measure ledger ownership independently of journey duration, which can continue
through harvesting after release. Direct and pooled groups overlap and must not
be added together. Shadow or disabled ledgers report the authoritative census
as unavailable. Path distance and current route state are not inferred from it.
The census remains in the ordinary trace; join deep captures by event ordinal.

New captures may additionally advertise `movement_audit_version: 1`. Their summary
counts committed neighboring grid moves and weighted entry costs for every live
transport-capable unit, partitioned into medical travel, claimed or unclaimed
resource pursuit, return to the job, idle movement and other activity. Stationary
decisions contribute no move; non-neighbor updates are explicitly excluded.
Unclaimed resource pursuit combines natural-resource and supplier travel; its
source kind is unavailable. Segment start/end and coverage availability are
explicit, with an exclusive end taken from the engine's final publication. An
unspecified start or unpublished end remains null. These totals do not reconstruct
travel before loading or establish wasted movement. Older captures lack this
audit and remain unavailable, never zero. Compare these totals alongside delivery,
labour and shortage metrics before evaluating roam or job-release fallbacks.

For classic-versus-claims travel comparisons, use the explicit
`resource_claims_movement_only` job observer (CLI
`--resource-claims-movement-only`). It requires bundle capability
`resource_claims_movement_only_version: 1`, the same bounded observer configuration
and compressed artifact as the decision probe, and full results. Sampling bounds
remain recorded but decision sampling and journey baselines are disabled. The
schema attests `movement_only: true` and `decision_sampling_enabled: false`;
collectors reject decision/journey rows or unknown segment coverage. This mode
neither enables a shadow ledger nor changes the loaded/new game's claim policy.
It excludes shadow, decision and deep observers. Results report actual claim
enforcement, including `ledger_mode: "disabled"` for a classic baseline.

Use the separate bounded gameplay analyzer for exact team-timeline metrics:

```sh
python -m tools.tournaments.resource_claim_gameplay RESULTS --build-design DESIGN.json --output INVENTORY.json
python -m tools.tournaments.resource_claim_gameplay RESULTS --design DESIGN.json --baseline POLICY_DIGEST --output REPORT.json
```

Freeze the design before comparing resource outcomes. It records paired initial
states, policy identity, seats and a common observed tick window across all
planned policies. Missing variants or timeline boundaries remain unavailable;
early-ended matches are not extrapolated. These team-level measurements are
separate from diagnostic journey samples and preserve the discovery/holdout
partition.

The pilot header has 240 logical pattern profiles with precomputed views for all
eight nearest-resource bearings. Cone/ray direction offsets are relative to that
bearing. Runtime selection uses wrapped integer deltas: a component at least
twice the other selects a cardinal direction, otherwise a diagonal. No runtime
rotation or rounding changes the arrays. Regenerate the temporary header with
`tools/resource_claim_patterns.py --pilot-header OUTPUT`; it refuses to replace
an existing output. This pilot omits most large radii and is not policy-selection
evidence for the full requested grid.

Analysis reports source-choice lower-bound regret separately from the cost to
the pattern's selected pickup access; these differ for pooled farms. The
selected-access prototype must use `mean_access_regret` and `mean_access_ratio`
as its travel proxies, alongside access-unreachable and fallback rates. Source
`mean_regret`/`mean_ratio` are component-best lower bounds, appropriate only for a
route that can finish at any access in the component. The diagnostic evaluator
chooses the smallest available stock index, while live harvesting chooses
ripest-first; identical availability/access/path labels do not establish identical
physical stock selection. Exact costs
label heuristic decisions but never choose their candidates. An unreachable
selection is distinct from falling back without selecting a resource. Missing
oracles, zero-cost optima, failed/incomplete games and sampling exclusions remain
explicit. Summaries weight per-game means equally, with deterministic bootstrap
intervals over games (unavailable below five games), rather than pretending
individual unit decisions are independent observations. Analysis of 16 or more
games uses the NumPy development dependency already pinned in
`requirements-dev.txt`. It reuses multinomial game-resampling weights across
policies with matching coverage, with a 64 MiB cache bound. Reports identify the
tool package and analysis environment; no dependency is added to the game.

`mean_probes` counts the raw entries each pattern examines. In contrast,
`new_destination_reads` counts marginal misses in a cache shared across the
entire event; it depends on which patterns ran earlier. It describes telemetry
amortization, not a pattern's standalone runtime cost. Shared event CPU also
cannot rank individual patterns. Compare native policy timing later in isolated
runs, retaining identical inputs and metadata mode.

Static route samples compare a direct route to the source component against its
component optimum, and a route through the selected probe against that access's
optimum. Reports retain unreachable, budget-limited and contradiction failures
without assigning them zero regret. Cached per-route CPU labels are not summed;
the event-level `static_route_elapsed_ns` is the measured aggregate. Readers also
accept the historical names `static_route_cpu_ns` and `oracle_ns` as elapsed
timings. New traces identify `worker-steady-clock-elapsed` and use
`oracle_elapsed_ns`. These values include scheduling delays and are not thread
CPU measurements; missing timing coverage is reported separately. These frozen
comparisons do not establish live refresh cadence, pending movement or fallback
effects.

A complete trace footer establishes framing, not unbiased sampling. Reports mark
sample-cap or queue-cap exclusions as censored; unknown exclusion counts also
remain censored. Scenario coverage counts uncensored games separately and blocks
policy selection when any measured game was censored. Simply dropping those
games does not correct contention-correlated missingness. Inspect first/last
sample ticks and retained late events during the pilot, then tune `sample_every`
and sample/output budgets before freezing a full round. The initial 512-sample
cap is a calibration setting, not an approved full-campaign sampling design.
An alternative weighting design requires explicit justification; none is applied
automatically.

The post-pilot coverage audit requires five distinct games and 100 sampled
events per required scenario, using actual material, stock, farm, yield and late
game features. Missing strata produce supplemental allocation requests. Ordinary
cherry, orange and prune trees retain their deposit but have finite fruit stock
that regenerates; they require explicit fruit and finite-persistent coverage.
Trees grow/spread and their wood harvest destroys the deposit. Ordinary rocks
yield infinite stone and have no finite-capacity contention by design; verify their routing
and delivery behavior separately, rather than demanding impossible contested
samples. Protected-farm classification is explicitly an observational proxy;
controlled fixture intent and gameplay review remain necessary.

Freeze supplemental snapshots for uncovered sparse/dense, protected/pooled,
exhausted-farm, algae, ordinary-fruit, finite-persistent, compound or destructive cases. Supply a
JSON list of `{ "path": "SAVE", "strata": ["water_algae"], "start_tick": 150000,
"window_ticks": 30000 }` records to `resource_claims supplement --bundle BUNDLE
--fixtures FILE --artifacts RESULTS/artifacts --output MANIFEST`. It retains
embedded AIs, rules and RNG state, freezes content-addressed inputs, rejects
identical states counted twice, and caps input capture at 256 MiB per save and
2 GiB total with an 8 GiB reserve. Distinct seeds/states must be prepared before
planning; repeating one save is not additional independent evidence. Scenario
labels alone never satisfy the coverage audit. Supplemental sampling rates differ
from the representative campaign, so analyze those frozen cohorts separately.

## Map-generation telemetry

Structured generation results now embed `map_report`, the complete native
[version-2 report](../map-generators/report-format.md), including every final-map measurement
and the bounded, ordered internal trace. Collection is automatic, independent of
map-file output. Failures from the generation service retain its failure report;
argument errors and crashes may only have diagnostics. Root/chosen seeds remain
explicit, and candidate searches do not expose discarded attempts' traces.

`generator_stress reanalyze` additionally writes `map-telemetry.json`, raw
`map-telemetry-records.csv`, `map-metrics.csv` and grouped summaries. Numeric
telemetry uses equal map weight after averaging repeated observations within each
map. Fallback/choice frequencies count maps. Missing/truncated traces are explicit;
retries and duplicates never count as additional samples. Seeded bootstrap intervals
resample maps within configuration/build/revision groups. For paired contrasts,
resample complete seed blocks across variants in your analysis script.

See the [map-design bulk workflow](../../.agents/skills/glob2-map-design/references/distributed-telemetry.md)
for complete commands, host configuration, dimensional units and statistical limits.
Native `map generate NAME --report-file FILE` uses tile dimensions; the structured
`map study GENERATOR --output-dir DIR` interface uses exponent dimensions as documented
above. Both use the same production report serializer.

## Whole-game probability scores

**Use the adjusted win-probability trajectory score as the standard continuous
AI effectiveness score alongside wins, losses and draws**, including custom
assessments. Its identity is `log_time_outcome_v1`, with log curvature `c=2`.
It replaces the previous linear AUC divided by observed game duration.
Standard reanalysis, experiment entry points and pooled leaderboards use it.
This is a trajectory/utility score from 0 to 1, not a calibrated win probability.

Let `H` be the absolute game tick limit, `T` its observed end, and `p(t)` the
piecewise-linear retained model curve. The normalized clock is
`g(t) = ln(1 + 2*t/H) / ln(3)`. Equivalently, `tau=H/2`; changing the log base
has no effect after normalization. The first half of the horizon receives
about 63.1% of the weight. Each game is weighted equally in aggregate reports.

The score is `integral_0^T p(t) dg(t) + u * (1 - g(T))`:

- On an `engine_end`, `u=1` for every declared winner, including shared winners,
  and `u=0` for a loss. Terminal utility is a scoring convention; it does not
  replace the recorded probability samples with invented 100% predictions.
- On a `win_probability` early stop, apply the same 1/0 credit using its declared
  predicted winners/losers. Reports label the source `probability_stop`, separately
  from actual `engine` endings. The default threshold remains 950 permille.
- At a `tick_cap`, `T=H`, so there is no terminal tail. The usual final-probability
  adjudication still determines W/L/D and ratings; it does not invent a score tail.
- An alliance shares one trace and is counted once. Never add allied probabilities.
  A winning alliance receives one full group credit, not a sum over its members.

Raw engine results still contain `win_probability_history` (`[tick, permille]`)
and the original `win_probability` summary (`model`, `final_model`, `permille`,
`start_tick`, `end_tick`, `area_twice_permille_ticks`, `mean`). The latter remains
linear/trapezoidal: area divided by 2000 gives probability × ticks; dividing by
observed duration gives the **legacy** mean. Neither field is the new score.
The fixed model identity fingerprints the coefficients and feature transforms.

Workers with `outputs.result: outcome` calculate a per-team
`win_probability_log_integral` before discarding the large curve. This sufficient
statistic retains metric/coefficient, resolved horizon, model, coverage, weighted
area and availability. Final group credit is derived from retained outcomes on
analysis. Compact results therefore score identically to full results without
verbose logs. Old full results with complete histories can be rescored. Old
compact records containing only linear area cannot recover the log integral;
they are unavailable for the new metric, with an explicit reason. Never treat
missing or partial history as zero, reconstruct it from the final probability,
or silently fall back to the old mean.

Analysis report schema 2 exports `probability_scores` in JSON/CSV and Markdown.
The aggregate `mean` is now the adjusted score, identified by `metric`,
`coefficient` and `horizon_ticks`. Groups retain format, build, variant, subject
or opponent role, model and stopping threshold; different horizons are never
pooled. Each entry includes W/L/D, scored/total games, unavailable reasons,
`outcome_credit_sources`, cap/early-stop counts and deterministic 95% seed-block
intervals. `legacy_linear_mean`, `legacy_linear_scored_games` and
`legacy_linear_mean_area_probability_ticks` preserve the old diagnostic.

Paired ablations export `paired_effects` with metric `log_time_outcome_v1`:
candidate minus baseline for the configured subject, raw differences and
seed-block intervals that keep correlated rotations together. Pairing requires
matching format, build, score definition, horizon, model and stopping rule.
Elo and placements remain outcome-based; changing this continuous score does
not rewrite wins, adjudication policies or previously published reports.

Custom scripts should use the shared reader rather than reimplementing the score:

```python
from tools.tournaments.probability import group_score

side = group_score(
    [t for t in result["teams"] if t["alliance"] == alliance],
    result,
    config=job["config"],
)
if not side["available"]:
    raise ValueError(side["unavailable_reason"])
score = side["score"]  # 0..1, higher is better
```

The resolved `tick_limit` takes precedence over the job request; compact records
retain that horizon in their integral. For a standalone full result, pass its
resolved metadata or the game job configuration. The configured horizon is fixed,
not inferred from how quickly a game ended. Zero elapsed time, missing opening or terminal coverage, inconsistent alliance
traces, or mixed model identities make the adjusted score unavailable. Missing terminal outcomes also remain explicit.
Cap adjudication can still use a fresh final prediction when history is partial.

Use matched models, horizons, stopping rules, maps and opponents. A probability
stop credited as a win is still the same model's prediction, not independent
confirmation of victory; disable early victory when testing natural conversion
or validating that model. Retain actual outcomes, completion times and held-out
maps alongside this score. Choosing `c=2` fixes a weighting preference; it does
not make every actual win rank above every unresolved game.

## Ending decided games early

Standard experiment planners enable the
[win probability](../ai/architecture/win-probability-model.md) winning condition at
95% (`"win_probability_permille": 950`), so strongly decided end games stop sooner.
Structured `game run` also defaults to 950; `--win-probability 0` disables it.
The engine still waits until tick 5,120 and evaluates every 512 ticks. Set
`"win_probability_permille": 0` to disable it, or specify another threshold.
The planner freezes this choice in every game job, so its identity and retained
design distinguish cohorts. An explicit `rules.winProbabilityPermille` also sets
the threshold; conflicting explicit settings are rejected. Low-level new-game jobs that
bypass the planners also use the CLI default of 950 unless they specify the rule.

Calibration and its limits are described in the model guide. Evaluate the model
against the AI and game rules in the cohort you intend to measure. The threshold accepts 0 (off) or 501–1000.
For validation or refitting of the probability model itself, disable probability
victory and use independent labels, such as the historical `prestige` cap policy.
The fitting helpers reject probability-called games and `win_probability` labels
to prevent training or evaluating the model against its own predictions.
Experiment `rules` also pass through the shared `--rule` interface. Games it ended report a `termination`
of `win_probability` rather than `engine_end`, and `observations()` carries the
raw termination through, so analysis can pool, exclude or compare them but can
never mistake the model's opinion for a win the rules declared. Do not compare
ratings gathered with the condition on against ratings gathered without it.

`tools/tournaments_ai_leaderboard.py` pools compatible directories explicitly and
uses the same batch duel estimator and paired-block intervals. It rejects duplicate
job IDs; keep AI source/settings and map distributions comparable yourself. Other
formats retain their earlier sequential Elo and penalized finishing-order fits.
For another batch, choose a new sample seed, audit map-seed/job-ID overlap, retain
both side-swapped games, and refit the concatenated outcomes. Never average the
batch ratings or restart from the old rounded UI scores. Bootstrap draws that omit competitors or have no finite
fit report unavailable uncertainty rather than silently biasing the intervals.

## Gameplay, AI and performance telemetry

Add `"outputs":{"telemetry":["team-timeline"]}` to an experiment configuration
(or pass `--telemetry team-timeline` to `game run`). This existing option now
exports the legacy timeline **and** all gameplay measurements, per-player AI
schemas/current/history/final values, and engine performance samples/final totals.
Collection remains automatic; export remains opt-in. No extra worker service,
transport option or result-schema migration is needed. The catalog advertises
`gameplay_telemetry_version`, `ai_telemetry_version`, and
`performance_telemetry_version` (gameplay 2, AI 1, performance 2). The typed
reader also covers the per-team `GLOB2_ECON`/`GLOB2_TL` timeline and the
`GLOB2_WINPROB` trace as a `team_state` family; unlike the gameplay measurements
these come straight off `TeamStat`, which is what makes them usable as model
inputs for something the simulation itself reads. Old bundles can still run and their
missing new record families are reported as unavailable.

Workers already retain `stdout.log`, compress it, checksum it and transfer it with
the other artifacts. These records stay in that artifact instead of inflating the
coordinator's SQLite/result payloads. The normal `reanalyze` commands now also
write `game-telemetry.jsonl` (one typed record per log line) and
`game-telemetry-values.csv` (one field per row). The main report's `game_telemetry`
section indexes counts, missing families, missing final records and parse errors
per game. Existing map telemetry, ratings and adjudication are unchanged. Reports
use accepted attempts only; diagnostic retries remain accessible separately.

The streaming API uses the same verified, offline artifact reader:

```python
from tools.tournaments.results import Results
source = Results("/tmp/glob2-results")
for game in source:
    if game["job"]["type"] == "game":
        for row in source.telemetry(game):
            if row.get("record") == "GLOB2_AI_FINAL":
                print(row["job_id"], row["values"])
```

Rows retain job, attempt, build and host identity, source line, record kind and
all named values. Integers remain exact Python/JSON integers, including uint64
counters; CSV adds a value type, so readers must not coerce integer columns
through floating point. `na` becomes JSON null / CSV `unavailable`, never zero.
Quoted names/descriptions and per-field `@tick` timestamps survive decoding.
Unknown AI fields and future AI/performance record kinds are retained. Malformed
rows are explicit JSONL errors with the raw line; artifact corruption is an error,
not an empty trace. Export reads one log line at a time without retaining complete
match histories in memory.

Join AI series using job/attempt + team/player/implementation/generation, not
team alone. Schemas are deduplicated within a match by AI implementation/schema;
a player's schema may therefore have been emitted under another player's identity.
Loaded games retain gameplay/AI coverage and history. History records repeat
previously sampled ticks at final export: choose sample or history records for a
curve, rather than summing both. Final records are cumulative counters or current
stocks, not extra events. The JSONL retains the original ordering and duplicates
so downstream tools can make that choice explicitly.

Performance session IDs are process-local: qualify them with job/attempt/host/build.
Do not pool different hosts, modes, budgets or builds blindly. Sample records are
interval measurements; final records cover the session. Hot-scope estimates,
observed totals, inclusive times and unavailable self times remain distinct.
Population standard deviations require sample counts and a parallel-variance
merge, not averaging standard deviations. A headless run has no render budget
and cannot measure interactive frame quality. Structured final saves are included
before the performance final record. Ambient build-label overrides are isolated like the existing headless tuning
environment. Performance collection is mandatory; the retired disable variable has
no effect. Version 2 adds wall-time distributions, permanent executor
wait/dependency summaries and renderer/GPU records to the same stream. GPU raw results
overlap interval/final aggregates; select one population when analyzing them.

See [gameplay metric definitions](../ai/gameplay-statistics.md),
[AI schema/extension contract](../ai/telemetry.md), and
[performance scope definitions](../development/performance-telemetry.md).
Existing exported telemetry logs need no conversion.


## Pipeline throughput benchmark

`test/tournament_pipeline_benchmark.py` runs identical seeded Maxima/Nicowar games
through a selected checkout's worker package, retaining the full experiment and
`benchmark.json`. Use the same bundle, map, seeds, slots and idle host for before/after
runs. Bundle/input warm-up is excluded from timing; final artifact collection is
included. The report separates occupied execution-slot time from actual engine CPU
seconds, and includes game results plus raw replay/save hashes for comparison.

```sh
python3 test/tournament_pipeline_benchmark.py --package-root /path/to/checkout \
  --bundle /path/to/bundle --map /path/to/map-r0.map \
  --output /path/to/new-results --jobs 24 --slots 4 --ticks 20000
```

Optional `--hosts hosts.json` supports SSH hosts with dedicated worker directories;
the supplied bundle must match those hosts. Do not benchmark two variants concurrently
on the same CPU resources. Short jobs emphasize scheduling overhead; report the map,
ticks, workload size, platform, and all commands rather than extrapolating one result
to every tournament.


Periodic gradient propagation shares the session's compute executor and uses an
eight-tick publication delay by default. `--compute-threads auto|N` selects the
whole pool; one participant is the serial control with identical simulation
behavior. `--gradient-delay D` selects 1–16 ticks for experiments before work is
pending. A loaded game preserves its saved delay; worker count may change without
altering decisions. Save and replay exports retain pending fields and deadlines.
See [performance experiments](../development/performance-telemetry.md) for the
benchmark procedure and interpretation of CPU and wall time.


New generated maps can carry normalized custom building frames with
`--building-catalog CATALOG_JSON --building-artwork BUNDLE_G2BA`. Use the canonical
snapshot produced by `assets compose-buildings --format json` as the catalog file. The artwork bundle
is verified against that catalog and embedded in the generated map (format 145).
Families selected in the graphical picker do not affect headless generation.

Custom JavaScript generator jobs may use a namespaced string `generator` ID. Attach
the frozen portable package as an input named `generator-package` (or numbered
`generator-package-*` inputs for several packages). The adapter supplies them through
`--generator-package`; package artifacts therefore participate in job identity and
travel with the request. See [generator authoring](../map-generators/javascript.md).

## Related guides

See [AI evaluation](../ai/README.md), [map-generator verification](../map-generators/verification.md), and the [tools index](README.md).

## Related guides

See [AI evaluation](../ai/README.md), [map-generator verification](../map-generators/verification.md), and the [tools index](README.md).
