# Maxima reconnaissance calibration

The calibration follows the information chain: discovery → visible observations →
remembered contacts → force estimates → attack gates → candidate ranking → flag
execution → damage and economic disruption. Errors at one stage should not be
compensated by tuning a different stage without measuring the tradeoff.

## Opt-in paired diagnostics

`--run-game --telemetry maxima-recon` enables Maxima's usual diagnostics plus
decision, staffing and offensive-outcome samples. Legacy entry points can use `GLOB2_MAXIMA_TELEMETRY=1`
plus `GLOB2_MAXIMA_RECON_AUDIT=1`. Ordinary `maxima` telemetry does not enable the
extra world scans. These records are separate from the indexed AI telemetry
schema and its constant-time capture contract.

- `recon_audit`: one observer/enemy pair at each approximately 1,000-AI-tick
  diagnostic boundary. Records fog-visible counts, remembered warrior peak and
  age, building knowledge, exploration, and the AI's current estimates, alongside
  exact living enemy counts and health-weighted warrior attack power. Power is
  `max(1, realAttackStrength * ATTACK_SPEED * hp / maxHP)` per warrior, matching
  Maxima's tactical threat units. This is a combat proxy, not an outcome model;
  it excludes explorer attacks, towers, reinforcement travel and food supply.
- `target_candidate`: viable siege and raid candidates with score, coordinates,
  enemy, route distance, and tower/opponent or worker/defender score inputs.
  Raids also record how many credited workers currently lie inside the actual
  raid flag radius. The `fog` and `oracle` views are separate.
- `target_choice`: selected candidate under each view, dwell status and aggregate
  rejection counts. A `none` choice is explicit. A missing row is **not** a choice
  of none: candidate ranking is skipped when the live offense gate is closed.
- `recon_staffing`: sampled every 128 AI updates, with actual own unit counts,
  existing recon-flag requests, attached explorers (including travellers), and
  explorers inside their assigned flag's wrapped circular radius. Separates
  contact and economic-watch assignments from other virtual-flag assignments.
  Integrate using `game_tick`; the sample interval is not exactly 128 game ticks.
- `offense_sample`: sampled every 128 AI updates, including idle samples.
  Records the actual offensive flag position/radius, assigned warriors and those
  physically inside its wrapped circular footprint, local hostile ground units,
  workers and intersecting building footprints under both current visibility and
  full information. Units inside buildings and airborne explorers are excluded
  from ground-target counts. Siege target presence/HP and the matching cached raid
  observation's tick/count are diagnostic labels; `-1` means no matching cached
  raid observation. Mission identity includes flag creation and retarget ticks.
- `offense_outcome`: at the same sampling boundary, cumulative combat worker and
  warrior deaths, melee unit/building damage and wheat harvested for the observer
  and each hostile team. These continue while no offensive flag exists, allowing
  fixed outcome windows to survive retargeting or abandonment. They are team-wide
  counters, **not kills or damage attributed to this flag**.

All events include both the AI-local `tick` and simulation `game_tick`. These are
not interchangeable. Labels and visible observations are read during the same
AI evaluation, avoiding an approximate join to the independently sampled team
statistics. `observation_tick` exposes freshness of the stored recon report.

The oracle uses the **same ranking function** as the live policy. It supplies
all living hostile units and nonvirtual buildings, recalculates opponent scores,
and runs the existing raid clustering algorithm. It retains the observer's
current army, flag recruitment level, attack gate, route fields, quarantine,
dwell and switching history. It is therefore a conditional full-information
ranking comparison, not a complete alternate omniscient game and not proof of
optimal tactical value. In particular, it does not measure targets skipped by
an incorrectly closed force gate. Raid centroid identity may change when hidden
workers join a cluster, so exact-coordinate disagreement alone overstates some
practical target changes.

Shadow inputs and results remain local variables. The oracle emits no orders,
changes no AI memory or budgets, draws no random numbers, and does not request
or refresh extra engine route fields. Its extra reachability storage is local
to the current decision. Diagnostic state is not saved and adds no save-format,
replay, network or indexed telemetry schema version.

## Run and analyse

### Raid footprint correction and offensive outcomes

Raid clustering still chooses the same central observed worker as the flag
location. The reward now counts only cluster workers within
`raiding.flag_radius`, including the boundary, using wrapped Euclidean distance
as the engine does. Worker, harvest, carry and resource bonuses all use that
subset, as does `raiding.worker_min`. Cluster radius, placement, threat penalties,
travel cost and persistence are unchanged. The fog and full-information passes
both use this rule. Sparse clusters can now lose to a siege or yield no raid.
This fixes inflated reward; it does not predict worker movement or optimize the
flag centre. Configurable `RaidRules::flagRadius` is derived from an existing
strategy setting and is not saved.

Collect the diagnostics in both builds, then compare:

```sh
python3 -m tools.maxima_raid_audit \
  --log control=/absolute/control.log --log fixed=/absolute/fixed.log \
  --output /absolute/raid-report
# Tournament result directories work too:
python3 -m tools.maxima_raid_audit \
  --results control=/absolute/control-results \
  --results fixed=/absolute/fixed-results --output /absolute/raid-report
```

The report retains raw samples, per-objective episodes, and summaries split by
build and raid/siege. It estimates time with no hostile ground unit or building
inside the flag, time empty after assigned warriors arrive, and corresponding
on-site warrior-time. Worker-free time is separate: a worker-free siege can still
be productive. Samples are held to the next observation and weighted by elapsed
game ticks; missing intervals and terminal samples are censored. Events shorter
than the sampling interval can be missed. Movement within the same raid remains
one episode; a full retarget starts another.

The first sampled arrival anchors 2,048- and 4,096-game-tick outcome windows,
continued after retargeting using team counters. Endpoint overshoot is recorded
and limited to 256 ticks. Incomplete windows are reported as censored, not zero.
Windows can overlap; their kills must not be summed as independent mission kills.
In team/FFA games other armies can cause the target team's losses, and the
observer's damage includes its other fights. Wheat harvested is a throughput
proxy: reduced harvest alone does not establish disruption caused by a raid.
Use matched seeds/maps/opponents and game-level comparisons before drawing a
causal or general gameplay conclusion.

Build an optimized client and register an immutable binary/data bundle using
[the tournament workflow](tournaments.md). Include `maxima-recon` in its catalog;
older bundles cannot provide these records.

```sh
python3 -m tools.maxima_recon_calibration plan \
  --bundle /absolute/bundles/ID --output planned.json
python3 -m tools.tournaments submit planned.json results \
  --bundle /absolute/bundles/ID
python3 -m tools.tournaments run results --hosts hosts.json
python3 -m tools.maxima_recon_calibration analyse results \
  --output analysis --fit
```

The default design is 96 games: eight opponents including Maxima; 1v1, 2v2 and
four-colony free-for-all; symmetric arena, city states, maze and continents;
128×128 maps; 32,768-tick caps. Team games use homogeneous two-colony sides;
free-for-all opponents are mixed. All games contain Maxima. Initial, final and
8,192-tick checkpoint saves support whole-map inspection. The win-probability
ending rule is off, so it does not censor the late-game calibration sample.
The manifest retains all seeds, roster/alliance assignments and bundle identity.

The planner's `--seed`, `--generators`, and `--ticks` define new campaigns.
Repeated `--bundle` values distribute jobs over supplied builds. The default
matrix has only twelve independent map/seed/format blocks; additional seeds and
larger maps are necessary before calling an estimator broadly calibrated.

Analysis uses verified committed logs, preserves exact integers and both clock
values, and exports raw force/candidate/choice JSONL plus force CSV. It reports
absolute error, signed bias, large under/overestimation, opponent/format/phase
breakdowns, paired target agreement and raid footprint concentration.

`--fit` additionally requires NumPy and scikit-learn in the analysis environment.
It fits shallow quantile gradient-boosted models for warrior count, total unit
count and health-weighted warrior power. Predictors are an explicit allowlist of
Maxima-visible observations, memory age and time; hidden labels, opponent AI
identity, host, map seed and outcomes are excluded. Entire map/seed/format blocks
are held out with grouped cross-validation, including all opponents on the same
geography. Training weights give equal total weight to each game/observer series.
Reported aggregate errors are per observation; inspect phase/opponent strata as
well. Independent quantiles are sorted before reporting 10/50/90 percentiles.
These fitted values are **offline research results**, not engine policy.

## Target quality beyond oracle agreement

A full-information heuristic can still choose a bad target. Review saved maps
alongside candidate components and the selected flag. Distinguish:

1. Missing candidates (unscouted economy, hidden defenders or towers).
2. Stale candidates (demolished buildings or workers that moved away).
3. Score miscalibration (worker clusters larger than the flag footprint,
   defender headcount versus trained power, siege/raid scores on different scales).
4. Execution failure (army disconnected, blocked entrances, healing/training
   cycles, moving targets and travel time).

For a policy change, use paired seeds and retained saves, compare worker deaths,
worker-time disruption, building damage/destruction, own losses and time spent
travelling, and then full-game outcomes. Compare raid utility over a fixed future
window, not just the instantaneous number of workers inside a circle. Evaluate
force-gate false positives/negatives separately from target ranking. Treat capped
games as unresolved rather than automatic wins. Hold out additional map seeds
and opponent mixes before selecting a deployable count/power model.

## Temporal enemy beliefs

`tools.maxima_recon_belief` supplies a fog-only `ObservationMemory`, portable
fixed-point-tree inference, fitting and grouped validation. It fits current-state
and forward models for warrior count, warrior power and total units, with
separate medians and nominal upper quantiles. The history features assume the
existing approximately 1,000-AI-update audit cadence. This is an offline analysis
prototype; its state and predictions are not used or saved by the engine.

Training takes the force JSONL exports produced by the calibration analyser.
Repeat `--training` to combine campaigns. Exports must include geography `block`
labels and globally unique game ids so shared maps stay together during grouped
validation. Fitting requires NumPy and scikit-learn; exported-model inference
uses only Python's standard library.

```sh
python3 -m tools.maxima_recon_belief fit \
  --training /absolute/training/forces.jsonl.gz --output /absolute/models
python3 -m tools.maxima_recon_belief compare \
  --models /absolute/models --forces /absolute/holdout/forces.jsonl.gz \
  --output /absolute/comparison
python3 -m tools.maxima_recon_belief calibrate \
  --predictions /absolute/comparison/predictions.jsonl.gz \
  --output /absolute/calibration
```

Freeze models before inspecting held-out games. Report prospective point-model
accuracy separately from later cross-validated upper-bound recalibration. Pooled
calibration must not be mistaken for reliable tail coverage against every
opponent. Keep generated models, campaign manifests, logs, saves, plots and
reports in an external results directory rather than committing them as source.
