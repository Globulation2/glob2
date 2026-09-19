# Larger map telemetry studies

For a study on one machine, the native CLI run in parallel is much faster than the framework:
see the end of [show the map, then measure the knobs](tuning-playbook.md#show-the-map-then-measure-the-knobs-then-roll-everything).
Use the framework below when the work spans several hosts or has to survive interruption.

Use the shared [tournament framework](../../../../docs/tournaments.md) for bulk studies
that need several machines, resumable work, per-sample retries or durable diagnostics.
Do not build another SSH runner or subprocess pool. Keep small native CLI probes for
one-map inspection. Both routes use the same production report serializer.

## Prepare and run a bounded matrix

Build the main client and register an immutable executable/data bundle following the
framework guide. Build Linux bundles on Linux; a macOS binary cannot run on the SSH
hosts. Capture `--headless-catalog` from the actual bundle. Require
`map_report_version: 2` and `generation_telemetry_version: 1` for this workflow. Preserve
source revision, dirty source identity and build options; never replace a cached bundle.
Use numeric generator IDs from that catalog. Distributed `params.width` and
`params.height` are **exponents** (7 means 128 tiles), unlike the native map CLI's tile
counts. Record complete settings, seed ranges and the hypothesis before dispatch.

Example `telemetry-study.json` (replace generator 15 with the relevant catalog ID):

```json
{
  "id": "map-telemetry-training-v1",
  "generators": [15],
  "map_seeds": [20001, 20002, 20003, 20004],
  "generator_params": {"width": 7, "height": 7, "teams": 4},
  "grid": {"width": [7, 8], "height": [7, 8], "teams": [2, 4]},
  "candidates": 0,
  "timeout_seconds": 120,
  "outputs": {"map": false}
}
```

The planner includes a baseline plus every grid row; each grid row is a complete
parameter assignment, with omitted controls taking registered defaults. Each sample
has a logical job ID. Set `candidates: 0` to measure the generator's unselected seed
distribution; candidate selection measures a different distribution. For selected
maps, the result retains root and chosen seeds and the **chosen attempt's** telemetry,
not traces of discarded candidates. Use a separate manifest for held-out seeds.

Use absolute worker directories in `hosts.json`, for example:

```json
[
  {"name":"therig.local", "directory":"/home/bradley/glob2-map-workers", "slots":1},
  {"name":"devlaptop.local", "directory":"/home/bradley/glob2-map-workers", "slots":1},
  {"name":"pharaoh-dev-1.local", "directory":"/home/bradley/glob2-map-workers", "slots":1},
  {"name":"pharaoh-dev-2.local", "directory":"/home/bradley/glob2-map-workers", "slots":1},
  {"name":"pharaoh-dev-3.local", "directory":"/home/bradley/glob2-map-workers", "slots":1}
]
```

These paths are examples for the supplied hosts/account; verify available disk and
libraries with `doctor`. Start at one slot and increase from measured RSS/throughput.
`ssh` defaults to `name`; set it explicitly for another account. Local workers use
`"transport":"local"` and a matching platform bundle.

```sh
python3 -m tools.tournaments.generator_stress plan telemetry-study.json --bundle /absolute/bundles/ID --output telemetry-plan.json
python3 -m tools.tournaments submit telemetry-plan.json artifacts/telemetry-training --bundle /absolute/bundles/ID
python3 -m tools.tournaments doctor artifacts/telemetry-training --hosts hosts.json
python3 -m tools.tournaments run artifacts/telemetry-training --hosts hosts.json
python3 -m tools.tournaments status artifacts/telemetry-training
python3 -m tools.tournaments.generator_stress reanalyze artifacts/telemetry-training --seed 19 --draws 1000
```

Run `run` again after interruption; use the experiment's pinned `worker.pyz` if the
Python source has since changed. `pause --drain` stops further buffered starts once
workers receive it; `resume` permits dispatch again. `collect` pulls available work
once. Never delete an unacknowledged spool to recover disk. Invalid requests and valid
generation failures are retained without automatic reruns. Inspect process failures
and missing jobs as well as accepted samples before drawing conclusions.

## Local study integrity and recovery

The same evidence rules apply to a small native-CLI harness. Assert that catalog
discovery found the intended controls and nonempty domains before scheduling work;
an empty discovery result can silently turn a control search into repeated defaults.
A zero process exit is insufficient: require a parsed report, the requested
telemetry, and the metrics needed for the analysis. Missing measurements are an
artifact failure, never zero or an accepted sample.

Write one complete request/result record per job. Resume against the saved ordered
request list or stable job IDs, verifying the full request and immutable build.
Count a rerun once, retain the failed attempt and its reason, and distinguish
infrastructure recovery from a generator retry with a different seed. Before
publishing, check expected versus completed counts and parse every retained record.
Encircled Kingdom's disk-full interruption left one successful command's measurement
file empty; rerunning that exact request was necessary even after the other jobs
resumed successfully.

Budget disk space for late saves and logs as well as initial maps. Compress completed
artifacts without changing active outputs. For a review bundle, include requests,
analysis scripts, build provenance, representative maps/saves and the telemetry
used for conclusions. If filtering noisy logs, retain every record the analysis
uses and say what was omitted. Finish analyses before packaging, then verify the
archive's contents against its manifest so it cannot contain a half-written summary.

## Analyze the returned observations

Every generated result contains `result.map_report`, the complete version-2 native
[map report](../../../../docs/map-generators/REPORT.md). It includes all final-world
measurements and `generation.telemetry` records with sequence, subject and original
JSON types. Service-level failures retain partial telemetry without analyzing the
invalid world; parsing errors/crashes may have no report. Logs and exit categories
remain available. Reports are collected even when map files are disabled.

Offline reanalysis writes `reports/prestige/map-telemetry.json`,
`map-telemetry-records.csv`, `map-metrics.csv`, and `map-telemetry-groups.csv` alongside
the ordinary JSON/CSV/Markdown report. The directory's `prestige` name is the shared
reanalysis policy default; it does not score or adjudicate generation jobs.

- Group by immutable build, generator revision, report version and full requested
  configuration. Keep platforms/build cohorts separate. Check outcome counts, missing
  reports and dropped/invalid telemetry first. Old bundles without telemetry remain
  explicitly missing; their historical results cannot reconstruct it.
- The statistical unit is one accepted logical map job. Retries and late duplicates
  stay in `attempts/`, but never increase the sample size. Numeric telemetry is first
  averaged within each map, then summarized across maps, with seeded map-bootstrap
  mean intervals. Inspect raw records for worst-subject or repeated-repair hypotheses:
  a within-map mean does not answer those questions.
- Fallback and variant frequencies count maps containing the event, not event rows.
  Their denominator is all accepted samples in the group, including failed samples.
  Missing and truncated traces can undercount occurrence; do not interpret absence
  as zero. Inspect pending/unaccepted jobs separately and report coverage.
- `map-metrics.csv` preserves every numeric final-report leaf under its JSON pointer,
  including array indices. Join these map-level rows to raw telemetry on `job`, then
  interpret subjects using the emitting key. A feature index is not automatically a
  team index. Read sentinel and metric definitions before pooling values.
- Compare variants using paired seeds and complete settings; for contrasts, resample
  whole seed blocks across variants, not individual event records. The default
  per-group intervals are descriptive, not paired-effect or multiple-testing claims.
  Predeclare hypotheses, inspect tails and rare variants, then confirm held-out seeds.

Use `tools.tournaments.results.Results` to load records in manifest order without
SQLite and `open_artifact(record, name)` to read verified compressed artifacts. Keep
raw results/build manifests so reports can be regenerated. Retain exact failing and
surprising seeds, then request maps for previews and follow up with AI games and human
play. Internal telemetry explains construction choices; it does not establish that a
map is fun, fair, or playable through late resource growth.

## Balancing tournaments on the same workers

The same coordinator plays the rotation tournaments that tune a generator ([the tuning
playbook](tuning-playbook.md) says what to read from them). A `fairness` experiment names one
generator, a few map seeds, a colony count and one AI; the planner adds every cyclic rotation
of team indices over the starts, so wins by start and wins by team index separate.

```json
{
  "id": "delta-r3-nicowar",
  "generators": [36], "map_seeds": [101, 102, 103, 104, 105, 106], "game_seeds": [1],
  "colonies": 4, "ai": "nicowar", "ticks": 45000, "candidates": 5,
  "generator_params": {"width": 8, "height": 8, "workers": 4},
  "outputs": {"telemetry": ["team-timeline"], "saves": ["final"]},
  "timeout_seconds": 5400,
  "settings": {"prefetch": 0, "heartbeat_seconds": 10}
}
```

```sh
python3 -m tools.tournaments.fairness plan delta.json --bundle /abs/bundles/ID --output delta-plan.json
python3 -m tools.tournaments submit delta-plan.json artifacts/delta-r3-nicowar --bundle /abs/bundles/ID
python3 -m tools.tournaments run artifacts/delta-r3-nicowar --hosts hosts.json
python3 -m tools.tournaments.fairness reanalyze artifacts/delta-r3-nicowar
```

Things the first run teaches the hard way:

- `outputs` applies to the game jobs. `map: true` is a generation output the planner already
  sets; on a game it is a missing artifact, the attempt is recorded as an `artifact_failure`
  with its complete result attached, and the job is retried on another host.
- With the default prefetch of two per slot, the first host to synchronize takes the whole
  queue: a four-core host ends up with nine games while a 32-core host idles. `prefetch: 0`
  and the biggest host first in `hosts.json` spread one wave evenly.
- One worker directory serves one coordinator at a time. To run several tournaments at once,
  give each its own `directory` per host and split the slots between them; the CPUs are
  shared either way, so this only pipelines generation, play and collection.
- Build the bundle on the worker with the oldest glibc; a newer host's binary is refused by
  an older one. A 45,000-tick four-Nicowar game at 256x256 costs about four core-minutes, so
  six maps by four rotations by one AI is one wave on sixty cores.
- Game results in `result.json` carry every team's `history` (units, buildings, prestige, HP,
  attack, defense every 512 ticks) and final counts; the `GLOB2_MEASURE` rows in `stdout.log`
  carry deaths by unit type and cause, hunger bands, blocked units and buildings, and natural
  growth near the team's buildings. `Results.telemetry(record)` streams them typed. Pool per
  start slot over rotations before looking at winners.
- `scripts/tournament_starts.py RESULTS` prints that per-start economy for every generator, and with
  `--detail ID --telemetry-key KEY` joins each start to a per-colony draw its generation recorded
  (a facing, a variant), which is how a start-split result is traced to its cause.
- The played map is an artifact of its generation job (`map-r0.map`); `--preview-map` renders
  it and the final save headlessly with `SDL_VIDEODRIVER=dummy`.

### Audit the study as well as the generator

Freeze the binary and source identity before a large sweep. Keep platform and
revision cohorts separate, and distinguish generator rejection from infrastructure
failures such as a full temporary disk. Retain completed rows; do not label an
interrupted study complete or silently combine diagnostic revisions into its totals.
Use native parallel processes for a single machine; SSH orchestration is worthwhile
when distributing work, not as an extra transport layer for each local probe.

Check every registered level against an explicit request plan. Paired control
comparisons require the same seed, dimensions, teams, workers and other settings;
report missing, failed and duplicate endpoints rather than dropping them. Compare
adjacent registered levels, not merely adjacent observed levels. Record all-seed
plateaus and per-seed reversals alongside means: retries can select a new landscape,
so one seed need not be monotone even when the control has a clear aggregate effect.

Count successful size/team/worker combinations, then fill only combinations absent
from the random plan. That coverage is not an exhaustive Cartesian product of all
controls. Preserve per-colony records when judging per-colony guarantees: averaged
worker counts can conceal unequal colonies, and a metric mixing home and neutral
meadows dilutes a control that only enlarges neutral meadows. State these limits when
only aggregated telemetry was retained.
