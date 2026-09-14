# Distributed tournaments

`tools/tournaments` is a Python 3.10+ standard-library package for Linux and macOS.
It executes immutable, explicitly selected binary/data bundles on localhost and
SSH hosts. Workers need Python and the bundle's runtime libraries already installed.
No source builds, dependency installation, inbound coordinator service, rating rule,
or tournament adjudication is part of the worker or engine.

## Start locally

Build the production client (`scons -j4 release=1 server=0`), then prepare a directory
containing `glob2` and `data/`. Include optional symbols and any other runtime files
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
running that bundle's `glob2 --headless-catalog` on its target. Registration hashes
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
Each generation sample is its own job, so a failed sample does not discard a batch.

## Production engine interface

Put the command first. `--headless-catalog` writes schema-version-1 JSON to stdout;
startup diagnostics go to stderr. It enumerates selectable AIs (excluding None),
Cortex and Maxima parameter schemas, generators, controls, revisions, telemetry,
and save/network versions, plus map-report and generation-telemetry schema versions. Structured commands require `--output-dir DIR`; an existing
`result.json` is rejected. Values are separate ordinary arguments, not JSON.

```sh
build/src/glob2 --generate-map --generator 15 --map-seed 42 \
  --param teams=2 --param width=7 --param height=7 \
  --write-map true --rotations 2 --output-dir /tmp/generated
build/src/glob2 --run-game --map-file /tmp/generated/map-r0.map \
  --game-seed 19 --player cortex --player cortex \
  --ai-param 0:swarmWorkerCap=4 --ai-param 1:swarmWorkerCap=7 \
  --ticks 4096 --save initial --save every:512 --save final \
  --telemetry checksums --output-dir /tmp/played
```

Generator options:

| Argument | Meaning/default |
| --- | --- |
| `--generator ID`, `--map-seed N` | Required method and independent uint32 seed |
| `--param key=value` | Repeatable generator controls; defaults and valid ranges come from catalog. Width/height are power-of-two exponents, as in the existing study tool |
| `--candidates N` | 0: explicit single-seed generation; positive: deterministic quality candidate selection, maximum 10000 |
| `--write-map true/false` | false; emits `map-rN.map` when true |
| `--rotations N` | 1; cyclic team reindexings, verified for unchanged geography and rotated starts |
| `--report headroom/terrain` | Headroom study measurements on stdout or terrain/resource grid in terrain.txt |
| `--profile NAME` | Optional isolated profile name; profile files live inside this output directory |

Game options:

| Argument | Meaning/default |
| --- | --- |
| `--map-file PATH` | New game on this exact map |
| `--load-game PATH` | Saved initial state or continuation, mutually exclusive with map/generator input |
| `--generator`, `--map-seed`, `--param`, `--candidates` | Inline generation alternative; embeds generation results and saves the generated map |
| `--game-seed N` | Required uint32 for a new game; forbidden when loading a save |
| `--player AI` | Repeat once per map team, in team order; required for new games |
| `--ai-param P:key=value` | Repeatable, zero-based player overrides; duplicate keys and invalid values rejected |
| `--alliance N` | Repeat once per team, one-based group labels; default separate alliances |
| `--win-condition NAME` | Repeatable replacement for standard conditions: death, allies, prestige, opponents, script |
| `--ticks N` | Absolute tick limit, default 90000; must exceed saved tick |
| `--replay true/false` | false |
| `--save initial/final/every:N` | Repeatable opt-in saves; checkpoints are diagnostics, not automatic recovery |
| `--telemetry NAME` | Repeatable checksums, team-timeline, maxima; default none |
| `--profile NAME` | Optional isolated profile name |

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
state survive independent interruptions. Artifact packaging runs in separate
processes from simulation slots; each host synchronizes independently so one
unreachable host does not stall dispatch to another.

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
| `inputs` | Map `map` or save `save`, or empty for generation. Values are imported artifact records or `{"job":"ID","artifact":"map-r0.map"}` |
| `depends_on` | Job IDs; every input dependency must appear; cycles rejected |
| `seeds` | `{"game":N}` or `{"map":N}`; empty on saved games |
| `config` | Game: players, ticks, ai_params (player-string to key/value object), alliances, winning_conditions. Generator: generator, params, candidates, rotations. Defaults follow CLI |
| `outputs` | replay false; saves/telemetry/reports empty lists; map false; core/stack false; required empty list of relative output paths |
| `limits` | timeout_seconds 3600, memory_mb host default, estimated_seconds 60 (buffer planning only) |
| `labels` | Opaque JSON object preserved unchanged |

Use `python3 -m tools.tournaments input RESULTS FILE` before submission to import
external input; copy the emitted complete artifact record into `inputs`. This
retains compression metadata needed for transparent decoding.

Coordinator settings (omitted fields use these defaults): heartbeat_seconds 15,
lease_seconds 300 (at least two heartbeats), prefetch 2 queued jobs per execution
slot in addition to active games, infrastructure_attempts 5, process_attempts 2,
transfer_slots 4. Optional prefetch_seconds limits estimated buffered dispatch.
Leases use coordinator time; running and queued attempts both renew while connected.
Each dispatch has a new attempt ID and random lease token. A disconnected worker
continues its buffer and spools results. Expired leases can be reassigned; old tokens
cannot revive, replace an accepted result, or count twice. Late attempts are retained.

Failures distinguish transport_failure, interrupted, disk_pressure, artifact_failure,
invalid_result, crash, timeout, process_failure, invalid_request, generation_failed.
Infrastructure failures have five attempts total; crashes/timeouts/process failures
have two total, preferring another eligible connected host. Invalid requests and
ordinary generation failures are terminal recorded results. Dependencies requiring
an unavailable generated map become blocked. Automatic retries restart original
input; a user can explicitly create a saved-state job to inspect a checkpoint.

## Hosts and operational commands

Each host has `name`, `directory`, optional `transport` (ssh default, or local),
`ssh` (alias defaults to name), `python` (python3 remotely, current interpreter locally),
`transport_timeout_seconds` (30), and worker fields:

| Field | Default |
| --- | --- |
| slots | logical CPUs minus one, minimum one; one process per slot |
| collect_slots | 1 separate artifact packaging process |
| builds | empty: every platform-compatible, capable build |
| disk_reserve_bytes | 1 GiB |
| spool_budget_bytes | 10 GiB |
| cache_budget_bytes | 20 GiB |
| memory_mb | null (optional process address-space limit) |

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
  {"name":"therig.local","directory":"/home/bradley/glob2-workers","slots":1},
  {"name":"devlaptop.local","directory":"/home/bradley/glob2-workers","slots":1},
  {"name":"pharaoh-dev-1.local","directory":"/home/bradley/glob2-workers","slots":1},
  {"name":"pharaoh-dev-2.local","directory":"/home/bradley/glob2-workers","slots":1},
  {"name":"pharaoh-dev-3.local","directory":"/home/bradley/glob2-workers","slots":1}
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

All four modules support `plan CONFIG --bundle DIR --output FILE`,
`submit CONFIG --bundle DIR --output RESULTS`, and
`reanalyze RESULTS [--policy prestige|survivor_draw|military] [--seed 1] [--draws 1000]
[--k 32] [--output DIR]`. Reports are JSON, CSV and Markdown; no rerun is required.

Shared design fields: id; builds (all supplied by default); map_build (first build);
generators [15]; map_seeds [1001]; game_seeds [1]; generator_params {}; candidates 5
for reusable maps; ticks 90000; timeout_seconds 3600; generation_timeout_seconds
1800; outputs {}; settings {}; labels {}. Build cohorts use common generated maps
unless generator variation itself is the experiment. Each build needs an eligible
host. Generator defaults/ranges are always discoverable in its pinned catalog.

* `ai_comparison`: ais defaults to all active selectable implementations;
  formats defaults to 1v1, 2v2, ffa. Duels pair every AI; 2v2 defaults to homogeneous
  pairs and accepts explicit two-player `rosters`; four-colony FFA balances AI
  participation through combinations and cyclic player orders. Map rotations and
  player-order rotations balance team indices and starts. Identical logical jobs
  are deduplicated, not counted as independent evidence.
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

Engine-declared winners are authoritative under every policy. Capped games rank
survivors by prestige, unit count, then finished buildings; exact ties stay tied.
Alternatives draw all survivors or rank warrior attack strength, HP, count.
Eliminated colonies rank by elimination tick. 2v2 aggregates surviving roster
statistics. Reports record policy name/version/options and engine/adjudicated status.
Elo starts at 1500, K=32; manifest order determines updates, never completion order.
2v2 rosters are competitors. FFA updates all pairwise scores simultaneously,
normalizing by opponent count. Formats and build cohorts have separate ratings.
Seeded uncertainty resamples complete map/seed blocks, preserving within-block
manifest order; incomplete blocks contribute observations but not block uncertainty.
Ablation intervals resample paired effects. Sparse/empty estimates are explicit nulls.

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
python3 test/test_map_fairness_tournament.py
scons -j4 release=1 server=0 tournament-compatibility-test
build/src/TournamentCompatibilityTest
python3 test/tournament_cli_integration.py --output artifacts/tournament-cli
```

CI runs these with the production Linux binary and retains CLI evidence. The
opt-in `test/tournament_reliability_pilot.py --help` describes the localhost/five-host
pilot; it intentionally kills only its own processes and simulates connection loss
by withholding coordinator contact, without rebooting hosts or changing networking.
Engine compatibility requires actual identical initial states/seeds/orders and
per-tick checksums across platforms; passing transport tests or builds is insufficient.
See the retained [validation record](tournaments-validation.md) for measured coverage,
commands, evidence and remaining limits.

## Map-generation telemetry

Structured generation results now embed `map_report`, the complete native
[version-2 report](map-generators/REPORT.md), including every final-map measurement
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

See the [map-design bulk workflow](../.agents/skills/glob2-map-design/references/distributed-telemetry.md)
for complete commands, host configuration, dimensional units and statistical limits.
Native `--generate-map NAME --json FILE` uses tile dimensions; the structured
`--generate-map --output-dir DIR` interface uses exponent dimensions as documented
above. Both use the same production report serializer.
