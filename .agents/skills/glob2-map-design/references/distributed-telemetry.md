# Larger map telemetry studies

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
