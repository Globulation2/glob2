# Nicowar and Maxima tournament runner

This document describes how to start a new tournament or optimization campaign.
Generated results live under `tournament-results/`, which is intentionally
ignored by Git.

## Build

Build the optimized headless binary from the repository root:

```sh
scons --build=build-tournament release=1 -j4 build-tournament/src/glob2
```

Tournament workers disable replay recording and autosaves so parallel matches
do not contend for shared output files.

## Manual GUI games with crash capture

Launch the optimized GUI through the standard wrapper:

```sh
python3 tools/run_glob2.py
```

Maxima telemetry and crash capture are enabled by default. On macOS the manual
launcher also keeps LLDB attached, allowing it to record the exact backtrace
for a crash caused by interactive play. Use `--no-debugger` if this interferes
with a platform-specific debugger policy. Each run gets a
timestamped directory under `tournament-results/manual-runs/` containing
`output.log` and `run.json`. Pass normal Glob2 arguments after the wrapper
options, using `--` if an argument could be confused with a wrapper option:

```sh
python3 tools/run_glob2.py -- --some-glob2-option
```

The wrapper uses `build/src/glob2`; build that optimized GUI with:

```sh
scons release=1 -j4 build/src/glob2
```

## Maxima versus Original tournament

Run the standard Maxima versus Original Nicowar comparison:

```sh
python3 tools/run_nicowar_tournament.py
```

Useful options:

```sh
python3 tools/run_nicowar_tournament.py --rounds 5 --jobs 8 --seed 12345
python3 tools/run_nicowar_tournament.py --rounds 3 --map G2 --map Isles
python3 tools/run_nicowar_tournament.py --rounds 3 --telemetry
python3 tools/run_nicowar_tournament.py --help
```

Use `--rounds` for evidence-producing runs because it guarantees equal map
coverage and starting-position rotations. Independent map/seed blocks use
mixed 32-bit seeds derived from the reported base seed.

The lower-level engine commands used by the runner are also available:

```sh
./build-tournament/src/glob2 -list-nicowar-tournament-maps
./build-tournament/src/glob2 -nicowar-tournament-match-nox maps/G2.map 12345 0 180000
```

## Shared-vision, independent-agent 2v2 tournament

Use the 2v2 runner for direct comparisons:

```sh
python3 tools/run_nicowar_2v2_tournament.py
python3 tools/run_nicowar_2v2_tournament.py \
  --rounds 3 --jobs 8 \
  --matchup maxima-v-original
```

Teammates receive the normal allied vision used by regular team games and are
allies for non-aggression and victory. Each player still runs a separate AI
instance without shared resources, messaging, strategy state, target selection,
or assigned roles.

The corresponding engine command is:

```sh
./build-tournament/src/glob2 \
  -nicowar-2v2-match-nox maps/G2.map 12345 6 5 0 0 180000
```

The arguments after the seed are AI A, AI B, seat partition, side swap, and the
simulation-step ceiling. AI IDs are Original `5` and Maxima `6`.

## Output and recovery

New runs write to a timestamped directory under `tournament-results/`. Reports
include structured results, CSV summaries, analysis, and captured worker logs.
Telemetry runs also include AI decision snapshots and read-only observer data.

Crash capture is enabled by default for manual games and the FFA and 2v2
tournament runners. An abnormal worker exit creates a directory under the
run's `crashes/` directory. Each bundle contains:

- `crash.json`: command, PID, exit status or signal, platform, and the final
  telemetry events;
- `output.log`: complete stdout and stderr from that process;
- `reproduce.sh`: the exact local command and relevant `GLOB2_*` environment;
- `system.log` and a copied `.ips` report when macOS provides them;
- `backtrace.txt` and a retained core-file link when a core is available.

The runners raise the child core-size limit before launching Glob2. This makes
crash capture independent of the macOS DiagnosticReports quota that can prevent
an `.ips` report from being written. If a headless tournament crash does not
produce a core, the runner automatically retries that deterministic match under
LLDB and stores the transcript as `debugger-rerun.log`; a reproduced failure
also produces `backtrace.txt`. Core files can be large; remove a resolved
bundle when it is no longer needed. Use `--no-crash-capture` only when storage
is constrained. Process PID, signal, failure kind, and bundle path are also
recorded in `matches.csv` and `results.json`, which makes failed games easy to
filter and rerun.

For a bug report, start with `crash.json`, `backtrace.txt` (when present), and
the last telemetry records in `output.log`. Run `reproduce.sh` from the bundle
to verify the failure against the same binary and match seed.

To continue an interrupted run, explicitly point the runner at that run:

```sh
python3 tools/run_nicowar_tournament.py \
  --resume-from tournament-results/<run> --retry-failed-only
```

Do not pass `--resume-from` when starting a clean campaign.

## Maxima strategy contract

Maxima uses the schema-v2 strategy files in `data/maxima/`. Inspect the binary's
authoritative contract before a search:

```sh
./build-tournament/src/glob2 --dump-maxima-schema
./build-tournament/src/glob2 --dump-maxima-strategy --maxima-format 2v2
```

The 2v2 runner accepts isolated per-process overrides:

```sh
python3 tools/run_nicowar_2v2_tournament.py \
  --matchup maxima-v-original --map FourSquares1 \
  --maxima-overrides \
  'military.campaign_population_base=70,scoring.target_switch_margin=60'
```

## Maxima optimization

Create the pinned optimizer environment once:

```sh
python3 -m venv .venv-optimizer
.venv-optimizer/bin/python -m pip install -r requirements-optimizer.txt
```

Run the paired 2v2 optimizer:

```sh
.venv-optimizer/bin/python tools/optimize_maxima.py \
  --trials 24 --jobs-per-host 4
```

Run the balanced multi-format portfolio optimizer:

```sh
.venv-optimizer/bin/python tools/optimize_maxima_portfolio.py \
  --bohb --bohb-eta 3 --parameter-stage tactical-core --same-maps \
  --trials 204 --startup-trials 24 --bohb-random-fraction 0.34 \
  --search-rounds 4 --replication-rounds 4 --audit-rounds 4
```

Each clean campaign starts from the compiled strategy as trial zero. Omit
`--resume-from` and `--warm-start-from` so no previous observations enter the
sampler. The portfolio experiment performs broad search, fresh-seed replication,
and an untouched-map audit at a common tick horizon.

Remote match workers require an identical checkout and optimized binary. Supply
workers explicitly for the hosts available to the campaign:

```sh
.venv-optimizer/bin/python tools/optimize_maxima_portfolio.py \
  --worker local:8 \
  --worker <host>:<slots>
```

The continuous scheduler lets faster hosts pull more games. Every worker in a
campaign must use the same source revision and native optimized build.

## Stopped September 3 tactical campaign and manual recovery

The `maxima-tactical-core-14-optuna-bohb-r1` campaign was stopped before any
candidate was promoted. At shutdown it had completed 5 BOHB brackets, 31
configurations, 41 rung evaluations, 5,432 games, and 7 full-budget candidates,
with no unresolved match failures.

The shaped objective was internally consistent: recomputing it from match
results agreed to within `5e-9`, and its correlation with the formal portfolio
score was approximately `0.99`. Lower-rung ordering was directionally useful
but not promotion-grade (`49 -> 147` Spearman `0.77` over 6 shared candidates;
the higher-rung samples were too small for a reliable estimate).

The run did not justify promoting its apparent leader. Trial 12 improved the
search objective from `0.34446` to `0.36306`, but did so largely with inert
extremes such as campaign thresholds or cooldowns beyond the 120,000-step match
horizon. On the exact 440-game comparison schedule, the current Maxima baseline
also substantially trailed the previously selected strategy (`0.33854` versus
`0.50438` formal portfolio score), especially in duel, FFA3, and 2v2.

Two interpretation cautions apply:

- The `tactical-core` search included explorer fruit-clearing controls. Those
  controls are not worker economic-raiding controls; the `raiding.*` block was
  fixed throughout this campaign.
- Telemetry contained only three tactical missions in the inspected small-run
  sample, and only one was a raid. Most snapshots were `expand` or `recover`,
  while early colonies often attempted two or three schools. That is enough to
  diagnose an over-eager economy, but not enough to estimate raid parameters.

The manual recovery therefore restores only a previously replicated economic
profile and two small tactical corrections. It deliberately leaves campaign
timings, warrior allocation, reconnaissance, and every `raiding.*` value alone:

| Key | Recovered value |
| --- | ---: |
| `economy.inn_population_divisor` | 36 |
| `economy.food_headroom_warning` | 40 |
| `economy.food_headroom_critical` | 23 |
| `economy.swarm_population_divisor` | 27 |
| `economy.school_population_min` | 50 |
| `economy.second_school_utility_min` | 72 |
| `economy.racetrack_population_min` | 69 |
| `economy.growth_site_utility_mid` | 69 |
| `economy.growth_site_utility_high` | 100 |
| `construction.population_mid` | 8 |
| `construction.population_high` | 53 |
| `military.defense_reserve_floor` | 17 |
| `scoring.target_warrior_weight` | 0 |

Future worker-raiding tuning should use a separate, bounded parameter stage and
enough telemetry-confirmed raid engagements to distinguish target selection
from mission launch, withdrawal, and economy failures.

## Analysis and telemetry

Standard FFA and 2v2 reports generate exploratory analysis automatically. To
combine selected new runs:

```sh
python3 tools/analyze_nicowar_tournament.py \
  tournament-results/<ffa-run> \
  tournament-results/<2v2-run> \
  --output-dir tournament-results/<analysis-run>
```

Pass `--telemetry` to capture Maxima state snapshots and immediate strategy
events. The tournament observer records omniscient state for analysis but never
exposes it to an AI. Treat correlations and strategy effects as hypotheses;
confirm changes with paired common-seed comparisons and fresh-seed validation.
