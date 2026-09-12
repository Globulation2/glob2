# Maxima switch-ablation tournament design

## Objective

Measure the causal value of every Maxima Boolean switch without paying for a
full-game tournament for every narrow or rare behavior. Use three experiment
types:

1. **From-start paired runs** for systems that alter the whole trajectory.
2. **Pre-trigger checkpoint rollouts** for rare, late, or contextual behaviors.
3. **Fresh full-game confirmation** before changing a default.

A checkpoint estimates a behavior's value *when its opportunity exists*. It
does not estimate average value over all games. Natural trigger frequency and
from-start confirmation remain necessary for that conclusion.

## What the existing results tell us

- The team-defense comparison has 1,703 paired games but only 337 independent
  map/seed blocks. The block-level standard deviation is 14.62 victory-score
  points; the mean is -0.03 with a 95% interval of -1.59 to +1.53.
- Effects vary substantially by map. Block standard deviation ranges from about
  1.6 on Migration and 2.4 on G2 to 27.8 on balanced. Keep map/topology strata
  and report interactions instead of trusting only a pooled mean.
- Farming, maintenance clearing, and recon occur in almost every game. Colony
  selection occurs in roughly 22-52% of Maxima instances, tactical missions in
  4-33%, dig-out in 0-4.4%, explorer strikes in 0-0.4%, and swarm retirement in
  4.5-6.7%. Rare systems need targeted checkpoint banks.
- Expand and recover postures are common; campaign is rare; finish was absent
  from the analyzed corpus. Posture tests also need opportunity selection.
- The staffing factorial's small effects stayed noisy at 117 games per variant.
  Future runs must retain paired checkpoint deltas, not just variant averages.

## Statistical unit and sample size

The independent unit is a **source map/seed block**, not a game or checkpoint.
Seat rotations and repeated continuations from one source are averaged inside
that block.

For paired standard deviation `sigma` and minimum practically important
difference `MPID`:

```text
n = ((z[1-alpha/2] + z[power]) * sigma / MPID)^2
```

The old defense variance implies:

| Victory-score MPID | 80% power | 90% power |
|---:|---:|---:|
| 5 points | 68 blocks | 90 blocks |
| 4 points | 105 blocks | 141 blocks |
| 3 points | 187 blocks | 250 blocks |
| 2 points | 420 blocks | 562 blocks |
| 1 point | 1,677 blocks | 2,245 blocks |

For a new partial-game metric, use a 32-40-block variance pilot. Approximate
requirements by standardized paired effect are:

| Standardized effect | 80% power | 90% power |
|---:|---:|---:|
| 0.50 | 32 | 43 |
| 0.35 | 65 | 86 |
| 0.25 | 126 | 169 |
| 0.20 | 197 | 263 |
| 0.15 | 349 | 467 |

Defaults: 64 independent blocks for a common-system screen; 40 unique source
games for a rare-event pilot; then 80, 160, or 320 source games for large,
medium, or small conditional effects unless the pilot calculation says
otherwise. Add 10% scheduling capacity for invalid runs. Use 90% power and
fresh source states for confirmation.

Use predeclared sequential looks at 64, 128, 256, and 512 blocks with an
alpha-spending rule or always-valid confidence sequence. Stop for a resolved
practically material effect, a 90% equivalence interval within `[-MPID,+MPID]`,
or the maximum budget. Screen families with Benjamini-Hochberg FDR `q=0.10` and
confirm promoted results at `q=0.05`. Test parents before children.

The default-policy rule is conservative: **inconclusive, underpowered,
practically unresolved, or heterogeneous results leave the switch on**. A switch
may default off only when a predeclared harmful effect is statistically resolved
and reproduced on the fresh confirmation bank. A heterogeneous result also stays
on unless a later experiment validates an explicit map/format-specific policy.

## Required harness work

### Deterministic checkpoint continuation

Saves preserve Maxima runtime state but currently do **not** preserve the live
Boost `mt19937` state; only the original game seed is stored. Before trusting
checkpoint experiments:

- serialize/restore the complete synchronized RNG state and bump save version;
- add a headless `load checkpoint -> run N ticks` command;
- apply a strategy override on load without resetting saved AI runtime;
- support per-player/per-team overrides so only the focal Maxima changes;
- run each checkpoint twice with the same arm and require identical checksums.

Keep `source_seed` separate from an optional `continuation_seed`. Multiple
continuations from one checkpoint can reduce rollout noise, but they are averaged
and never counted as independent samples.

### Opportunity telemetry and checkpoint bank

Every switch needs a side-effect-free probe with this lifecycle:

```text
not_applicable -> eligible -> decision -> action -> resolved
```

The eligibility probe runs even when the switch is off, consumes no RNG, and
does not mutate state. For placement and posture switches, a shadow evaluator
flags states where enabled and disabled scoring would choose differently.

Capture immediately before an irreversible order or policy-owned action. A
checkpoint manifest records source game/map/seed, format, topology, opponent,
focal seat/team, tick, switch, severity, posture/environment, active policy
state, strategy/build hash, save/RNG/start-state checksums, and ancestry.

Use deterministic reservoir sampling with quotas across map/topology, format,
seat, severity, and initial advantage. Default to one checkpoint per switch per
source game, deduplicate context hashes, and freeze separate discovery and
confirmation banks. Never select states based on their later outcome.

Master switches must still be tested from the start; a late checkpoint already
contains the trajectory they created. Checkpoints for masters are mechanism
tests only.

Store raw rows keyed by experiment, switch, source block, checkpoint,
continuation, arm, and tick. Cluster intervals by source game. Repeated states
from one source do not increase effective sample size.

## Outcomes and rollout horizons

Train horizon-specific models on separate baseline full games to predict eventual
formal score from state at 5k, 10k, 20k, and 30k. Validate by held-out map and
topology, then freeze them before ablation. Predicted end value is a common
secondary metric, not a substitute for direct metrics or full-game confirmation.

| Family | Primary local outcome | Diagnostics | Horizon |
|---|---|---|---:|
| Economy/staffing | worker and population growth AUC | utilization, jobs, hunger, buildings, throughput | 10k-30k |
| Farming/food | food-security AUC | headroom, hungry unit-ticks, corn, farm capacity, clearing labor, obstruction | 10k-20k |
| Placement | economic value | construction latency, routes, displaced farm capacity, threat exposure | 10k-20k |
| Recon | information regret | force-estimate error versus observer truth, coverage, target regret, explorer cost | 10k-20k |
| Defense/emergency | survival-adjusted retained value | assets/population retained, enemy power destroyed, recovery and hunger cost | resolve+3k/end |
| Tactics/raiding | mission net value | enemy value destroyed, own losses, disruption, muster/travel time, progress | resolve+3k/end |
| Colonization | colony net present value | completion, capacity, diversion cost, time to productive colony | 20k-30k |
| Upgrade/repair | service-value AUC | uptime, completion, cost, survival and production | 10k-20k |
| Posture | trigger-appropriate survival/economy value | transition regret, duration, recovery, campaign progress | 10k-20k/end |
| Teamplay | team state/formal value | ally survival, focal opportunity cost, combined force and progress | 20k/end |

AUC is normalized area under the per-tick trajectory. For defense/tactics, stop
when the threat or mission has been absent for 2,000 ticks, then measure 3,000
recovery ticks. Continue to game end (or a safety cap) if survival diverges.

## Switch registry

Modes: **FS** from start; **CP** pre-trigger checkpoint; **SH** shadow choice
comparison; **END** extend terminal-sensitive branches to game end.

### Wave 1: major systems and critical decisions

| Switch | Mode | Opportunity or stratum | Primary outcome |
|---|---|---|---|
| `farming.enabled` | FS 30k | land, shoreline, and island maps | food-security/economic AUC |
| `recon.enabled` | FS 30k | fog and shared-vision formats | information regret and predicted value |
| `tactics.enabled` | CP+END | mission exists, before selection/order | mission net value |
| `colonization.enabled` | FS+CP | valid distant parcel, before commitment | 30k economy and colony NPV |
| `teamplay.enabled` | FS+CP+END | 2v2; ally-aware choice differs | team value |
| `defense.reactive.enabled` | CP+END | local threat crosses threshold; no active flag | retained value |
| `military.preemptive_defense_enabled` | CP+END | qualifying choke/forecast before guard | retained value minus prevention cost |
| `military.explorer_defense_enabled` | CP+END | attack explorers near valuable colony before response | survival/value retained |
| `economy.food_service_safeguards_enabled` | FS 30k | food pressure and labor oversubscription | food-security/growth AUC |
| `upgrades.enabled` | FS+CP | eligible affordable upgrade before order | service/economic value |
| `emergencies.food_enabled` | CP+END | food distress crosses emergency threshold | survival and food-security AUC |
| `emergencies.colony_enabled` | CP+END | colony loss/danger crosses threshold | retained value |
| `postures.recover_enabled` | SH+CP | recover wins before transition | recovery value |
| `postures.defend_enabled` | SH+CP+END | defend wins before transition | retained value |
| `postures.expand_enabled` | SH+CP | expand wins before transition | economic/colony value |
| `postures.develop_enabled` | SH+CP | develop wins before transition | economic/technology value |
| `postures.mobilize_enabled` | SH+CP+END | mobilize wins before transition | readiness and survival |
| `postures.campaign_enabled` | SH+CP+END | campaign wins before transition | mission net value |
| `postures.finish_enabled` | SH+CP+END | weak enemy makes finish win | time to victory/final score |

### Wave 2: high-impact components

| Switch | Mode | Opportunity or stratum | Primary outcome |
|---|---|---|---|
| `economy.large_economy_adaptation_enabled` | FS+CP | size threshold crossed before adaptation | economy, 20k-30k |
| `economy.amphibious_network_maintenance_enabled` | CP | amphibious economy lacks pool capacity | economy/route value, 20k |
| `economy.worker_birth_throttle_enabled` | CP | excess free labor before birth decision | growth/utilization, 10k |
| `repairs.enabled` | CP+END | valuable damaged building before repair | uptime/value retained |
| `military.counterattack_enabled` | CP+END | colony threatened with campaign target available | survival plus mission value |
| `military.warrior_training_backlog_throttle_enabled` | CP | saturated backlog before birth decision | force readiness net of economy |
| `placement.food_preservation_enabled` | SH+CP | parcel differs because of farm loss | food/economic AUC |
| `placement.defensive_siting_enabled` | SH+CP+END | parcel differs because of threat/defendedness | retained value/completion |
| `placement.artery_routing_enabled` | SH+CP | parcel differs because of route/reservation | latency/economy |
| `tactics.siege_enabled` | CP+END | valuable reachable building and ready force | mission net value |
| `tactics.dig_out_enabled` | CP+END | route blocked only by clearable resources | access and mission value |
| `raiding.enabled` | CP+END | safe worker cluster and deployable force | disruption minus opportunity cost |
| `teamplay.defense_enabled` | CP+END | ally threatened; focal has surplus warriors | team survival/value |
| `explorer_campaign.enabled` | CP+END | strike threshold plus enemy warrior cluster | power destroyed net of cost |
| `recon.scouting_missions_enabled` | CP | contact/frontier mission before flag | information regret/economy |
| `recon.force_memory_enabled` | CP | observed force has just gone under fog | estimate error/downstream value |
| `farming.farm_protection_enabled` | SH+CP | protected cells alter build/clear choice | food/economic AUC |
| `farming.resource_preserving_circulation_enabled` | FS+CP | new building circulation intersects existing wheat or wood | access/food/economy |
| `farming.maintenance_clearing_enabled` | CP | parcel or firebreak needs maintenance | obstruction time/economy |
| `farming.proactive_clearing_enabled` | CP | proactive clearing candidate before action | space/economy minus labor cost |

### Wave 3: narrow or highly conditional heuristics

| Switch | Mode | Opportunity or stratum | Primary outcome |
|---|---|---|---|
| `economy.swarm_retirement_enabled` | CP | remote unproductive swarm reaches criteria | economy/service capacity |
| `military.preemptive_amphibious_enabled` | CP+END | route classification differs through water | retained value |
| `placement.spacing_compactness_enabled` | SH+CP | parcel differs due to spacing score | completion/economy |
| `tactics.failed_target_quarantine_enabled` | CP+END | recent fast failure before repeat targeting | loss avoidance/mission value |
| `tactics.siege_target_lock_enabled` | CP+END | productive siege before retarget decision | progress/mission value |
| `teamplay.pressure_coordination_enabled` | SH+CP+END | ally pressure changes target choice | combined mission value |
| `fruit.enabled` | CP | accessible fruit before flag/sharing action | food/happiness/economy |
| `recon.economic_watch_enabled` | CP | late-game watch patrol before creation | information net of explorer cost |
| `farming.wheat_invasion_clearing_enabled` | FS+CP | wood can invade protected wheat boundaries | food/economy minus clearing cost |
| `farming.wood_firebreak_enabled` | FS+CP | wood lies in the fertility-banded firebreak | access/food/economy minus clearing cost |

This registry contains all 53 Boolean switches currently declared in
`AIMaximaStrategy.cpp`.

## First tournament to launch

### A. Qualify the infrastructure

1. Add RNG-state save/load, per-player overrides, headless checkpoint execution,
   opportunity telemetry, and raw paired result storage.
2. Create fixed-tick checkpoints from 10 ordinary games.
3. Run ON twice and OFF twice from every checkpoint. Same-arm checksums must
   match; start-state checksums must match across arms.
4. Toggle a switch with no opportunity. Its branches must stay identical. Any
   difference exposes override, RNG, or hidden-state contamination.

### B. Major from-start screen

Start with:

```text
farming.enabled
recon.enabled
colonization.enabled
economy.food_service_safeguards_enabled
economy.large_economy_adaptation_enabled
upgrades.enabled
teamplay.enabled                 # 2v2 stratum only
```

Use 64 independent map/seed blocks per switch and arm. Average candidate seat
rotations inside each block. Run 30k ticks, retaining 5k, 10k, 20k, and 30k
observations. The opponent pool should contain a frozen Nicowar and a frozen
baseline Maxima; only the focal player changes.

Include at least FourSquares1, G2, Garden 3, Holiday Island 2, Isles, Migration,
and balanced because earlier defense results showed materially different map
behavior. Also report topology classes so a result is not tied only to names.

### C. Major event screen

Harvest at least 40 unique pre-trigger source states for:

```text
tactics.enabled
defense.reactive.enabled
military.preemptive_defense_enabled
military.explorer_defense_enabled
emergencies.food_enabled
emergencies.colony_enabled
```

Run paired 20k rollouts with dynamic resolution and end-game extension. Use the
pilot variance to promote each switch to 80, 160, or 320 source states. Explorer
defense will require targeted source generation because natural explorer strikes
were observed in at most 0.4% of Maxima instances.

### D. Posture screen

Bank states where the shadow director says a candidate posture is the winner.
Start with recover, defend, expand, and mobilize. Campaign and finish need
targeted late-game generation. Disable only the candidate posture and let the
unchanged director choose its next-best valid posture.

### E. Fresh confirmation

Promote only effects that are statistically resolved, practically meaningful,
and directionally consistent in relevant strata. Confirm on untouched map seeds
and checkpoint source games at 90% power. Run whole games for every proposed
default change, even if discovery used a partial or conditional endpoint.

## Reporting contract

Every switch report includes:

- independent source blocks, total engine runs, opportunities, actions,
  resolutions, invalid runs, and effective sample size;
- paired mean/median effect, 95% cluster interval, equivalence interval, MPID,
  standardized effect, and sequential boundary;
- effects by map, topology, format, opponent, seat, severity, and initial
  advantage, using shrinkage for small cells;
- natural trigger frequency and conditional checkpoint effect as separate values;
- primary outcome, diagnostics, and full-game confirmation status;
- verdict: helpful, harmful, practically neutral, heterogeneous, or inconclusive.

## Implemented harness

The engine and tools now support the first major-system and event-screen wave:

- Save format 93 preserves the complete synchronized `mt19937` continuation
  state. Checkpoint runs report start/end world and RNG checksums.
- `--maxima-player-overrides` and `--maxima-team-overrides` isolate the focal
  Maxima without changing its opponents.
- `--maxima-checkpoint-harvest <directory> <interval>` writes periodic source
  states while opportunity probes identify major-system, posture, tactical,
  defense, and emergency contexts.
- `--maxima-checkpoint-run <save> <ticks>` continues a state for a relative
  horizon. Loaded AI runtime is preserved and only the selected strategy switch
  changes.
- `tools/harvest_maxima_checkpoints.py` generates source games and a stratified
  manifest. `tools/build_maxima_checkpoint_bank.py` can rebuild a bank from
  existing logs. `tools/run_maxima_switch_ablation.py` runs paired ON/OFF arms,
  repeats each arm for determinism qualification, clusters by source game, and
  emits raw runs, paired rows, JSON, and Markdown summaries.

For example, harvest the seven initial maps and then run the major-system bank:

```sh
python3 tools/harvest_maxima_checkpoints.py \
  --output-dir tournament-results/maxima-ablation-bank \
  --rounds 10 --max-steps 30000 --interval 1000

python3 tools/run_maxima_switch_ablation.py \
  tournament-results/maxima-ablation-bank/manifest.json \
  --output-dir tournament-results/maxima-ablation-wave1
```

Both commands default to the five remote Linux hosts and their complete 60-slot
capacity (`4 + 4 + 4 + 16 + 32`). The controller's local macOS host is excluded.
Every configured remote slot must connect before a campaign starts; use
`--allow-missing-workers` only for an explicitly degraded run. Repeat
`--worker HOST:JOBS` to override the locked allocation. Checkpoints are copied
to a content-specific remote staging directory, while logs and results are
collected centrally with the executing host recorded on every row.

For a production run, execute these tools from an immutable source snapshot and
pass that snapshot's remote path through `--remote-root`. Record the snapshot
digest and the identical Linux binary digest in the result directory before
launch so later working-tree edits cannot affect an in-progress campaign.

Harvest teamplay opportunities separately on a four-team map so allied behavior
is not mixed with duel/FFA source blocks:

```sh
python3 tools/harvest_maxima_checkpoints.py \
  --output-dir tournament-results/maxima-ablation-teamplay-bank \
  --format 2v2 --map maps/FourSquares1.map --rounds 40
```

Manifest rows may select `victory_score`, `economy_advantage`,
`military_advantage`, `resilience_advantage`, or `prestige_advantage` and may
set a metric-specific MPID. Economic, farming, placement, colonization, upgrade,
staffing, and fruit switches default to the economy trajectory; defense,
emergency, and repair switches default to resilience; the remainder default to
the composite victory score. CLI `--metric` and `--mpid` options override these
defaults for a predeclared experiment.

The runner uses Student-t intervals for small samples and reports the number of
blocks needed for 90% power at the observed paired variance. It enforces a hard
minimum of 32 independent source blocks before calling any result powered.
Discovery results never disable a switch. Even on a fresh confirmation bank, a
switch can be disabled only when the run is adequately powered and the entire
95% interval is worse than `-MPID`; every underpowered, neutral, heterogeneous,
or inconclusive result keeps the switch enabled.

The generated recommendation for every verdict except confirmed harmful is
`keep enabled`.

Do not report checkpoint effects as unconditional win-rate gains. A rough
population contribution may be shown as natural opportunity frequency times
conditional effect, explicitly labeled as an estimate and validated by the
from-start confirmation.
