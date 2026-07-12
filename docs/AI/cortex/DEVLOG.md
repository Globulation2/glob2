# Cortex AI — Development Log

Chronological research record for the Cortex C++ AI, migrated out of agent memory
(2026-07-07). These are **point-in-time observations** — dated findings, traces, and
hypotheses, including ones later disproven and kept for provenance. `file:line`
citations reflect the code at the time of writing and may be stale; verify against
current source before acting.

For the current one-paragraph state, see agent memory `project_cortex_status`. Durable
constraints that are NOT historical live in their own memory files:
`project_cortex_wheat_forbidden_removal` (paint-removal invariant),
`project_aicortex_handcrafted_first` (the effort A/B plan),
`project_ai_benchmark_harness` (how to benchmark), `project_nicowar_defense_flag_wrap_crash`.

Benchmarks throughout use `glob2/tools/ai-benchmark.sh --swap-sides` (Muka/SmallForTwo
have a strong side bias). "vs Nicowar", 80 games unless noted.

---

## 1. Economy foundation (2026-06-05)

### Wheat protection (checkerboard forbidden) — committed `65b95f1b`

Paint a **checkerboard forbidden pattern** `(x+y)&1` over wheat (CORN) so workers harvest
one half while the protected half stays full and reseeds it (`MapStep.cpp:80` expand
branch; forbidden blocks harvest but NOT growth). Fixes Cortex's deliberate
carrying-capacity throttle (`CortexPolicy.cpp:23-26`).

- **Step 1:** isolated geometry+reconcile core `glob2/src/ai/cortex/CortexWheat.{h,cpp}`
  (no `Player*`, no `Orders`) + headless debug tool `./build/src/glob2 -dump-wheat <map>
  [team]` (in `Glob2.cpp`, sweeps N=0..2, ASCII overlay). Depth model: a plain
  **land+wheat BFS** from the inn's walkable **exit ring** (NOT the buried centre tile),
  where land steps cost a walking move but only wheat tiles count toward depth — gives an
  inn-directional gradient with no staircase artifacts. Rejected: centroid/edge-silhouette
  (staircase false-fronts); zero-cost land flood (onion rings, no directionality).
  Validated SmallForTwo + A_big_pond.
- **Step 2:** wired into the live AI. Contract → OBSERVATION/ACTION_VERSION **5**:
  `ACTION_PROTECT_WHEAT` + `wheatOpenMargin`; obs fields `wheatOpenMargin` /
  `wheatProtectAddCount` / `wheatProtectDelCount` + `swarmsProducingExplorer` (4th field,
  to cleanly revert the early-explorer mix without reading raw swarm ratios). Live wrapper
  `reconcileWheatForbidden` in `CortexWheat.cpp` (inn-centre seeds, colony bbox +
  `WHEAT_REGION_MARGIN`=10 excluding virtual flags, real FOW, `BrushAccumulator` masks).
  Priority below defense / above sticky offense, `!starving`, counts>0. `AICortex` does a
  lazy once-per-game `syncRand()%3` N draw (persisted, NOT redrawn on load) and emits
  `OrderAlterateForbidden(MODE_DEL then MODE_ADD)`, no cooldown. `WHEAT_PARITY` in
  `CortexTypes.h`. Verified 70/70 tests, `G2.game` replay byte-equal, same-seed
  cortex-v-cortex byte-identical, 12k-tick both-teams-painting run.
- **N capped 0–2** (real starter fields are only ~5–7 deep). Open tuning knobs:
  `WHEAT_OPEN_MARGIN_MIN/MAX`, `WHEAT_REGION_MARGIN`.
- New headless map tool: **`./build/src/glob2 -dump-resources maps/<name>.map`** (in
  `Glob2.cpp`, reuses `Game::load`, renders CORN/water/terrain ASCII + team starts). Keep.

### Closed-loop wheat economy + never-halt redesign — committed `dae6bb44`, `e77fdc65`, `9c5c6440`, `82efa919`

Four layers, all gated on per-building CORN buffer:
- **Worker tuning** (new `ACTION_TUNE_WORKERS` → `OrderModifyBuilding`, Castor-style local
  `b->maxUnitWorking=` + `update()` + order): nudge each swarm/inn `maxUnitWorking` ±1 per
  cycle inside a deadband. Swarm add<5/rem≥15 corn, cap 7 (→12 at maxBuildLevel≥3 + free
  workers). Inn add<5/rem≥8 corn, cap 6.
- **Swarm expansion gate:** new swarm only when swarms==0 OR an existing swarm is at worker
  cap. Inn build also when an inn is at cap + still wheat-starved.
- **Placement:** swarm sites hard-rejected within 6 tiles of another swarm or >5 tiles from
  CORN; inns >5 from CORN. Shared `nearestCornDist` helper.
- **Observation:** per-building `TrackedBuilding` arrays (corn/maxCorn/maxUnitWorking/
  occupancy/nearestWheatDist). Contract → VERSION 6.

Engine facts driving thresholds: swarm holds 20 CORN, 5/unit, 1 unit/150t, STALLS at
corn<5; workers only refill buffer, do NOT speed production (timeout-gated) → "at worker
cap" is the expansion signal. Inn holds 10, feeds ~5× a swarm's draw.

**Pre-combat panic defense (v7):** when `!combatPhase && under attack`: (a) flip swarms to
100% warriors, (b) raise swarms to engine HIGH priority via `OrderChangePriority` (new
`ACTION_SET_PRIORITY`, restored to 0 after), (c) panic-build a hospital. Panic block sits
above Priority 1 and suppresses it. New obs: `swarmsProducingWarrior`,
`TrackedBuilding.priority`. Benchmark base→economy→panic: vs Nicowar 2.5→16.2→**27.5%**;
vs Warrush 12.5→13.8→**17.5%**.

**Never-halt / always-expand (the big win).** Diagnosed via env-gated stderr dumps
(`CORTEX_DUMP_ATTACK` one-shot at first under-attack, `CORTEX_DUMP_PERIODIC` per-cycle
econ trace; behavior-neutral). Root cause of ~77% Warrush losses: the old
`sustainable=feedCapacity*2` over-capacity guard set the swarm ratio to {0,0,0} (HALT) the
instant the first inn finished (pop 9 ≥ cap 8), freezing the colony at 2 buildings / ~10
units / 0 warriors with 8 idle workers for ~1000 ticks — a minimal Warrush harasser then
dismantled it. Placement was NOT the blocker. Redesign (CortexPolicy.cpp):
1. HARD RULE swarm ratio NEVER {0,0,0} — removed the halt + all overCapacity / popCap /
   shouldGrow / economySurplus machinery.
2. Feed-led inns — build whenever `feedCapacity < totalUnit` (capacity vs population, NOT
   wheat supply; wheat shortfalls are the worker-tuning loop's job).
3. Worker-DOMINANT combat mix {2,0,1} (+1 explorer until one exists) once "established"
   (combatPhase = inn+swarm+≥10u, not starving).
4. Always-expand tech ladder gated on `canExpand` (=freeWorkers>0 & !starving & !hungry):
   **inn → school → racetrack → hospital → barracks** (NO 2nd swarm initially, per user).
   Added `CORTEX_BUILD_WALKSPEED`(=3, racetrack). Building→ability: school=BUILD+HARVEST,
   racetrack=WALK, barracks=ATTACK_SPEED/STRENGTH.

**Result (SmallForTwo):** vs Warrush 22.9→**58-65%**, vs Nicowar 16→**72.9%**, vs Castor
51→**91.7%** — every matchup flipped. Lockstep-safe (pure integer logic). Open knobs:
barracks 5th in ladder (may want earlier vs Warrush); worker count plateaus ~19 mid-game.

**Expand-vs-upgrade ladder (`e77fdc65`).** Per-class decide on two signals:
%-of-eligible-units-MAXED-at-current-level (helper `unitsServedPct`, gate
`UPGRADE_MAXED_PERCENT=60`) + spare labour (`canExpand`). Barracks require ≥2 finished
before upgrading (laggard-first `findUpgradeTarget`); school/racetrack single-instance;
inn feed-safe (spare inn first, post-upgrade feedCapacity ≥ pop); hospital grows COUNT
with army (1 per 8 warriors, cap 3), upgrades in a lull. Added `walkLevel[]`;
OBSERVATION_VERSION 7→8. Engine: hospital heals 2/5/7 at L0/1/2 in 30/18/6 ticks. Doc:
`glob2/docs/AI/cortex/upgrade-expand-mechanics.md`.

**Whole-class upgrade-blackout fix (same commit).** An issued `OrderConstruction` stays
INVISIBLE in the observation while the building evicts trainees + waits for the larger
footprint to clear (`Construction.cpp:394-423`), so the policy stacked a SECOND upgrade and
took the whole class offline (both barracks → 0 functional). Fix: per-class
one-upgrade-in-flight guard `pendingUpgradeType` in `AICortex`, released when the upgrade
becomes a visible site (`cortexBuildingsUpgrading>0`) or after
`UPGRADE_PENDING_TIMEOUT_TICKS=2000`; serialized in load/save for lockstep.

### Offense findings — settle-the-offense, committed `290c431d`

Measured combat on SmallForTwo (seeds 1-8, vs nicowar+warrush). **Ruled out — do not
re-attempt:**
- **Explorer-flags for active scouting: NOT NEEDED.** Free-roaming explorers already
  discover the map (engine idle fog-gradient wander, 7×7 reveal, auto-avoids tower range).
  Median first-contact ~3.5k ticks; only 1/16 never discover. A flag-PINNED explorer is
  *worse* — loses tower-avoidance, dies in 2-3 tower shots (38 HP).
- **Targeting enemy `startPosX/Y`: REJECTED as a FOW cheat.**
- **Tower-avoidance for offense: NOT NEEDED yet.** Offense flag never within 8 tiles of a
  tower in 16 games. Army melts to enemy warriors, not towers.

**The real defect — "settle the offense":** the single WAR_FLAG oscillated between enemy
base and home every 50-tick cycle (light harassment made defense recall preempt offense
almost every cycle; `OrderMoveFlag` teleported it home and back, army melted, offense
fired ~once/game). Fix (RAM-only, no contract bump): action-layer thrash hysteresis —
committed offense holds `OFFENSE_HOLD_TICKS`=600, ignores defensive recall unless
`buildingsUnderAttack >= DEFENSE_SERIOUS_BUILDINGS`=2; `ATTACK_MIN_WARRIORS` 12→8.
Result (120g): castor ~50%, nicowar ~2.5%, warrush ~12%.

**STILL OPEN — warrush (~12%):** insensitive to commit threshold (8/10/12/18 all ~12%);
all-in rush beats Cortex on tempo. Next lever is economy / early-defense, NOT flag routing.
**On the cooldown-split regression (`65b95f1b`):** mostly small-sample artifact — don't
trust 60-game combat deltas at low win rates; use 120+ and watch the castor control.

### Swim pool + second swarm

Two features added, both ON:
- **Swimming pool (`SWIMSPEED_BUILDING`).** New `CortexWater.{h,cpp}` `assessSwim()`:
  discovered-ALGA scan + bounded land-vs-swim reach flood-fill (`isHardSpaceForGroundUnit`
  canSwim false/true, 8-conn, radius `CORTEX_SWIM_REACH_RADIUS`=24, anchored on first
  building). Obs `algaeDiscovered` / `swimLandReach` / `swimWaterReach` (computed only
  while no pool exists). Policy (Priority 5.5) builds ONE pool when `combatPhase &&
  canExpand && warriors>0 && (algaeDiscovered || swimWaterReach*5 > swimLandReach*7)`.
  Workers+warriors swim (perf 0/10/20/30 and 0/8/16/24); explorers can't. AICastor's
  `computeNeedSwim` formula is INVERTED/buggy — do NOT copy it.
- **Second swarm.** Policy (Priority 6.95) builds another when `combatPhase && canExpand &&
  openingBuildOutDone && anySwarmSupplyStressed && swarms<CORTEX_MAX_SWARMS(3) &&
  swarmSites==0`. `openingBuildOutDone` = inn+school+racetrack+hospital+barracks each
  finished. `anySwarmSupplyStressed` = a swarm pinned at `CORTEX_SWARM_WORKER_CAP`(7) with
  corn<`CORTEX_SWARM_CORN_ADD_LO`(5).

**Benchmark cost** (SmallForTwo, 80g): both OFF 63.7%, swarm-only 60.0%, both ON 53.8% →
pool ≈ −6pts, swarm ≈ −4pts. NOTE: the prior "73% vs Nicowar" did NOT reproduce here —
actual both-OFF baseline on this sample is ~64% (wide CI). Files: `CortexWater.{h,cpp}`,
`CortexTypes.h` (OBSERVATION_VERSION 8→9), `CortexObservation.cpp`, `CortexPolicy.cpp`.

---

## 2. The Muka investigation

### Map overfit discovered (2026-06-07)

Cortex strength is highly map-dependent — tuned almost entirely on SmallForTwo. On Muka (a
larger 2-player map), --swap-sides:
- vs Nicowar: **32.3%** (vs 87% on SmallForTwo) — complete reversal.
- vs Warrush: 61.5% (down from ~89%).
- vs Castor: 68.7% with 18% draws/TIMEOUTS (vs clean 100/0/0) — gets a lead, can't close.

**Do not treat SmallForTwo win rates as general strength. Benchmark on Muka + larger maps
before declaring a win.**

- **2026-06-07:** economy angle first RULED OUT for the Nicowar loss — fixed "stuck at 1
  swarm on Muka" (commit `9bbec681`), Cortex now expands 1→3 swarms, but Muka-vs-Nicowar
  barely moved (27%, within CI). Concluded (wrongly) it was a combat/army problem. Cortex
  also became utility-scoring at this point (commit `76c4c5c6`: each decision returns a
  score, `decide()` picks max).
- **CORRECTION 2026-06-08:** it WAS economy — population OVERSHOOTING wheat, not too-slow
  expansion. More swarms made it WORSE on wheat-poor Muka. See feeding governor below.
- **UPDATE 2026-07-07:** Muka is no longer a loss. HEAD `f0335d05` (post inn fix
  `a697e3b3`): Muka vs Nicowar **63.7%**, SmallForTwo 96.2%, zero crashes/draws. Remaining
  is a ~36% loss tail (mid-to-long grinds; seeds 9/36/38 lose BOTH orderings).

### Feeding governor — the starvation-collapse root cause (2026-06-08), committed `9baf5133`

**The "peak 1 warrior / army-size keystone" premise was FALSE** (corpus misdiagnosis).
The decide-corpus `warriors` column shows Cortex builds **9–50-warrior armies** on Muka,
not 1. No army-size bug.

**Real root cause = chronic-starvation collapse from population overshooting wheat.**
Win/loss splits cleanly on chronic starvation, not army size: every WIN had ≤8% units
starving (`final==peak`); 12/13 LOSSES had 9–74% starving (colony craters peak 90–160 →
~0). Two coupled hand-rule defects:
1. **No population governor.** All swarms produce workers *every cycle* through famine; the
   "CORN-buffer stall is the supply governor" assumption fails — the swarm's local buffer
   stays full while the *inns* are wheat-starved.
2. **`combatPhase` conflated "established" with "!starving."** When starvation crossed
   `STARVE_HALT_PERCENT`=6, `combatPhase` flipped false → `growWarrior=0`, `growWorker=1` →
   swarm made ONLY workers (more mouths) and stopped replacing the army → vicious loop.

**Fix (hand-rule):** split `combatPhase` in `computeFacts` into `economyEstablished`
(mature, regardless of food); `combatPhase = established && !starving`;
`foodSaturated = established && starving`.
- **A. feeding governor:** `growWorker=0` when `foodSaturated || (established && hungry)`.
- **B. decouple army from starving:** `growWarrior = economyEstablished ? 1 : 0` (keep
  building the army during famine — convert doomed surplus food into soldiers).
- generalized `scoreProductionControl` rungs from `combatPhase`→`economyEstablished`.
- **C. wheat-blitz:** `scoreOffense` commits at `BLITZ_MIN_WARRIORS`=4 during foodSaturated
  (`SCORE_OFFENSE_BLITZ`=6700, above tech band); `scanWheatForbidden` /
  `reconcileWheatForbidden` gain `liftAll` → un-forbid the WHOLE field for a one-time
  harvest burst (a deliberate famine override of the steady-state paint-removal invariant).
  Files: CortexPolicy.{h,cpp}, CortexPolicyEconomy.cpp, CortexPolicyCombat.cpp,
  CortexWheat.{h,cpp}, AICortex.{h,cpp}, AICortexTranslate.cpp.

**Results:** Muka-vs-Nicowar 22.5→47.5%. Regression matrix 40g/cell: no cell regressed
beyond 1-game noise; aggregate 74.7→80.6%; warrush improved. Determinism PASS.

**HELD-OUT CHECK (seeds 101–140, 80g): only 41.2%** (~6pt below the 47.5% on training
seeds 1–40) — a mild overfit signal. Losses = 46 decisive mid-game (median ~16.7k ticks,
SHORTER than wins ~21.3k), only 1 timeout — the governor killed the starve-to-timeout
failure mode but losses remain.

**TEST 2 (Φ-trajectory decomposition): residual losses are STILL self-starvation, NOT
army-melt.** Early divergence (tick 4–12k) driven by STARVING-FRACTION (loss 0.11→0.39 vs
win 0.01→0.04), NOT buildings-under-attack. Eco melts by UNITS DYING not buildings razed.
**17/46 losses have ZERO enemy building-contact — pure starvation, no combat.** `mil`
diverges only LATE (downstream symptom) → offense seam RULED OUT as the loss cause.
Fully reproducible solo. Test 0 + Test 2 both rule out RL/offense.

**DEEPER DRILL (2026-06-09) REFUTED the simple governor-lever theory — 4 hypotheses killed:**
1. governor-blind-in-bootstrap: FALSE — all 46 losses are economyEstablished when
   starvation first crosses 6%. Governor CAN fire.
2. worker governor too weak: FALSE — `swarmsProducingWorker` flips OFF *harder* in losses
   than wins.
3. growWarrior over-mints army: FALSE — losing armies are 3× SMALLER (peak-warriors median
   26 vs wins 82). High loss warrior-share is a SYMPTOM (workers starved away). A
   feed-capacity `growWarrior` gate would shrink an already-undersized army → WRONG fix,
   retracted before implementing.
4. offense fails on Muka: FALSE — Cortex attacks in 82% of losses; posture mix near
   identical. Army commits; it's just small+underfed.

**REAL signature = economy-SCALE / wheat-feeding throughput.** Wins sustain feedCap 84/80
units; losses BLACK OUT (`feedCapacity==0`) in 18% of mid-late cycles vs 4% for wins, stall
at ~39 units, then starve. Inns stand but sit empty of corn → a wheat-SOURCING problem.

**PHASE 1 (2026-06-09) — feedCap==0 root-caused.** `feedCapacity`
(`CortexObservation.cpp:82-93`) sums `cortexInnUnitSupport` over inns ONLY when
`countHarvestableCornWithin(inn, radius=5) >= 5` non-forbidden CORN tiles. It NEVER reads
corn-stock, haulers, or restockTripsNeeded → **hauler-caps is STRUCTURALLY IMPOSSIBLE as a
feedCap==0 cause** (the tuneWorkers inn rung cannot move feedCap at all). Eliminated by
code, not data. Instrumented per-inn (forbidden-BLIND corn count discriminator: forbidding
KEEPS tiles CORN so blind-count stays high; depletion DESTROYS CORN-type). 5 loss seeds:
split is **BIMODAL** — 101/103 ~82-91% (c) DEPLETED (stock ~2/10); 110/112 ~75-78% (b)
FORBIDDEN (stock fine, pessimistic proxy); 105 mixed. Aggregate ~63% (c) / ~37% (b). WIN
contrast is CLEAN: zero feedCap==0 blackouts after tick 6000. **Reframe: feedCap==0 is a
GATE-ESTIMATE collapse, not literal empty inns** — 57-96% of blackout inns STILL HOLD CORN
(stocked from wheat OUTSIDE the radius-5 gate; haulers reach farther than the gate
measures). Causal chain: feedCap drives the 2nd-inn gate
(`CortexPolicyEconomy.cpp:188`, `capacityShort = totalUnit*100 >= feedCap*PCT`, always true
at feedCap=0) → inns keep STACKING on the exhausted catchment; and
`canExpand=(freeWorkers>0 && !starving && !hungry)` (`CortexPolicy.cpp:181`) goes FALSE →
new-SWARM expansion onto FRESH wheat is LOCKED OUT. Trap closes.

**PHASE 2 relocation lever — FLAT, NOT committed (2026-06-09).** `scoreSecondSwarm` gate
`combatPhase`→`economyEstablished` + `SCORE_SECOND_SWARM_FAMINE`=6800 so a starving
established colony can place a fresh-patch swarm. Held-out Muka 101-140: **42.5% vs 41.2% =
FLAT.** The "trapped at 1 swarm by canExpand" framing was PARTLY WRONG — the colony expands
to `CORTEX_MAX_SWARMS`=3 and STILL starves:
- seed 101 (lever INERT): builds all 3 swarms during the HEALTHY phase, so by famine
  `f.swarms<MAX` is already false. Overshoots to u=109/A=79, craters to u=7.
- seed 103 (lever FIRES, insufficient): 3rd swarm at t≈17600, transient reprieve, then pop
  overshoots the combined catchment AGAIN and re-collapses.

**Verdict: binding constraint is wheat-feeding THROUGHPUT vs population overshoot, NOT the
canExpand gate.** More swarms = more mouths.

**MAX-SWARM CAP REMOVAL — also FLAT, COMMITTED anyway `13c2f4c9`.** Removed arbitrary
`CORTEX_MAX_SWARMS`=3, bounded at `CORTEX_MAX_TRACKED_SWARMS`=8 (the real POD-array limit).
Held-out Muka: **43.8% vs 42.5% vs 41.2% baseline = flat.** seed-101 with cap removed:
builds 6 swarms, 120 units/95 warriors, survives 42146 ticks (+48%) — and STILL craters to
u=1. A 6-swarm/120-unit colony starves to death anyway. Legit cleanup (more robust, longer
survival) but does NOT flip outcomes. Committed per user request. Determinism PASS.

**RECURRING TRAP = the `combatPhase` (=established && !starving) gate.** Locked out
relocation (fixed) and ALSO freezes HOSPITALS during the blitz (`scoreHospital` +
`scoreHospitalExpandUpgrade` gated `combatPhase && canExpand`, both FALSE in foodSaturated).
CAVEAT: blitz losses die of STARVATION not battle wounds (hospitals heal HP not hunger), so
hospitals are a blitz-sustain defect but NOT the primary loss cause.

**HOSPITAL FIX — committed `75bca958`, BEHAVIORALLY INERT here.** Design converged AWAY
from "build hospitals during blitz" (rejected: survival should outrank them in famine).
Final: all 3 hospital rungs `combatPhase && canExpand` → `economyEstablished &&
freeWorkers>0` (provision off STANDING-army size); removed `HOSPITAL_MAX` cap; no new score
constant. Same-seed A/B (16/16 games Muka+SmallForTwo): BYTE-IDENTICAL — the army only
exceeds the old cap's coverage WHILE STARVING, exactly where hospitals are correctly
deprioritized. Correct cleanup, dormant on these matchups.

**feedCap SENSOR FIX — TESTED & REJECTED (2026-06-09), and it exposed WHY Cortex wins on
Muka.** New dedicated `CORTEX_FEED_REACH_RADIUS`=10 (haul reach) used ONLY at the feed
gate. Mechanically a SUCCESS: cut feedCap==0 false blackouts ~93% (seed101 906→62 cycles).
But held-out Muka **CRATERED 42.5→12.5%** (~30pt regression). REVERTED. **KEY INSIGHT: the
false feedCap==0 is load-bearing via ECONOMY SCALE, not the offense trigger.** Offense
timing identical A/B; the ONLY thing that moves is economy size: understated feedCap keeps
`capacityShort` TRUE → STACKS 5-9 inns → ~2× population → maxArmy A≈73 vs sensorfix's 37.
**Wins are carried by ARMY SIZE, not blitz timing** (wins iff army crosses ~50-60). Fixing
the sensor calms the economy (2 inns) and shrinks the army → loses. **The lever is the
INN-BUILD / economy-expansion driver, not offense.** The bug accidentally found a
net-positive "grow big" policy. (Methodology note: a trace-capture helper bug — binary path
with slashes in `GLOB2_REPLAY_PATH` → empty trace — briefly produced a false
"16/16 byte-identical"; only basename-clean captures with `grep -c CORTEX_TRACE >0` are
valid.)

### Famine disables defense (2026-06-10)

From a seed-3 decision-trace post-mortem: **the army never recalls to defend because the
base is starving.** `combatPhase = economyEstablished && !starving`
(`CortexPolicy.cpp:172`); `scoreDefense` is gated on `combatPhase`, so a starving colony
**declines the recall entirely** — even with 2–3 buildings under attack and 40+ warriors.
In the seed-3 loss, all 42 serious-threat cycles were starving → DEFENSE fired 0 times.
Worse, the SAME starvation fires `scoreOffense`'s blitz branch
(`SCORE_OFFENSE_BLITZ=6700 > SCORE_DEFENSE=4000`) → a starving colony throws its whole army
FORWARD exactly when home is overrun. The flag-count-scaling combat change (swim-pool era)
AMPLIFIED this, consistent with the Muka-Nicowar 47.5→37% regression — it exposed, not
created, the gap. **Fix directions (not done):** decouple recall from combatPhase (serious
assault ≥2 buildings recalls regardless of famine); make SCORE_DEFENSE outrank
SCORE_OFFENSE_BLITZ when home is seriously hit. Repro: `CORTEX_DUMP_ATTACK=1
GLOB2_CORTEX_DECIDE_TRACE=<abs> GLOB2_TEST_SEED=3 glob2 -test-games-nox 1 --map Muka
--matchup cortex,nicowar` (candidate index: 15=defense, 17=offense).

### War execution is NOT the Muka lever — three dead-ends (2026-06-13 → 06-21)

War-logic pass (Muka vs Nicowar): losing-push **retreat** (clear offense flag + re-mass
when outnumbered at the front), units-based serious-defense (`unitsUnderAttack >= 6`), and
an offense-hold that re-arms only on a fresh commit. New obs `ownWarriorsNearFlag` +
`offenseRecommitUntil`; new `ACTION_RETREAT`; VERSIONS 16→17 / 11→12. **Result: net-neutral,
17/32 → 17/32.** 6 games flipped (3 rescued, 3 regressed). Regression cause (seed 2): the
war flag caps at 20 units (`MAX_UNIT_WORKING`) AND `veteranFlagLevel` holds low-level
warriors home, so "force present at front" ≪ "available" — the retreat misreads winnable
pushes (freeW=23 while leading 82-53, only ~2 actually committed) as losing.

- **Muster-then-march REGRESSED to 12/32.** Made Cortex commit a bigger massed army that
  nearly wiped Nicowar (seed 2: 137u/15796atk vs 14u/0atk at t23k) but then collapsed
  137→10 from FAMINE — feedCap=56 vs 137 units, 66 starving. Committing harder deepens the
  overpopulation famine.
- **Offense WAVE PIPELINE (2026-06-15): 14/32, still below 17/32 baseline.** Multiple
  offense flags, one MUSTERING at home rally while others MARCH (LOW prio, keep cohort);
  defense flag HIGH. KEY BUG FIXED: gate muster/spend on `flag->unitsWorking.size()`
  (transit-invariant), NOT spatial `countWarriorsNear` (reads ~0 during cross-map march).
  After fix, deploys ~60 warriors across 3 flags and rescues collapse games — but famine
  still dominates: 138 units vs feedCap 56.
- **Removing the starving gate from combatPhase (2026-06-21): BYTE-NEUTRAL on Muka**
  (34/80 both). Inert because the army has no scouted target anyway (1 explorer →
  flagTargets[0] invalid) AND the colony is already in the famine spiral.
- **Forcing 2 explorers instead of 1 (2026-06-21): OUTCOME-NEUTRAL** (byte-identical 40
  seeds). Extra scouting can't help — colony dies in the famine spiral regardless.

**BOTTOM LINE: the decisive fix is warrior-production capping / feeding, NOT war
execution.** (Confirmed a 4th time.)

### Warrior overproduction (Muka losses, GLOB2_TEAM_TIMELINE traces)

Two Cortex-vs-Nicowar Muka losses (seeds 2 & 7, cortex=team0), identical mechanism: Cortex
out-booms early, then over-produces warriors on a too-small worker/food base and
death-spirals. At crisis ~65–95 warriors vs only ~36–40 workers (warriors > workers),
foodCritical chronically 20+. Starving warriors compete with workers for food → worker base
cannibalized (40→2) → swarms/inns collapse (swarm 6→0) → Nicowar mops up. Cortex responds
to starvation by building MORE warriors. **Nicowar** keeps a disciplined ~4:1 worker:warrior
ratio, grows its worker base monotonically (53→94), only ramps warriors after the economy
is large and stable.

**ROOT CAUSE (decide-trace, seed 2): deploy-vs-produce imbalance, NOT refusal to attack.**
`scoreOffense` (index 17, SCORE_OFFENSE=2000) is ELIGIBLE from tick 10233 (389/389 cycles)
but chosen only 38× — `scoreProductionControl` (index 1) wins 321× because SCORE_OFFENSE
loses the utility contest to economy/upgrade actions that always have work. Commit deferred
~8k ticks while warriors accumulate 12→33 unused. Once a war flag is up, ONE flag commits
≤`CORTEX_MAX_FLAG_UNITS`=20 and only veterans (`veteranFlagLevel` floor 8); thin tech →
weak/capped force. Multi-swarm production (growWarrior=1) outpaces the 20-cap trickle,
warriors climb 33→87, overshoot the catchment, starve, collapse. **Fixes (not done):**
raise/condition SCORE_OFFENSE; cap warrior PRODUCTION to deployable+feedable capacity;
earlier feeding throttle keyed to foodCritical/worker count. Tooling:
`GLOB2_CORTEX_DECIDE_TRACE=<abs-prefix>` (per-cycle CSV, `chosen`/`eligible` index =
`candidates[]` order in `CortexPolicy::decide`, 0=panicDefense … 17=offense).

### Worker-target tiers — Muka fix without the governor, committed on feat/ai-trainer-support

Cortex's swarm production mix (`computeFacts`) replaced both the old worker-surplus throttle
AND the feeding governor with a 3-tier worker-COUNT rule:
- `base` = Σ(swarm+inn `maxUnitWorking`) + 2 (the hauler floor).
- `needs` = `obs.workers` + `fillableNeeded` (full staffable demand).
- `mid` = base + (needs−base)/2.
- workers < base → workers only; base ≤ workers < mid → worker-dominant mix (ratio 8:1);
  workers ≥ mid → warriors. Warriors gated on economyEstablished; never-{0,0,0} kept.

**Why:** the earlier rule flipped workers-only → warriors-only the instant `workers` met
`base`. On wheat-poor Muka that hit at ~13 workers while 16 jobs were open and 0 idle → made
warriors during a labor shortage → haulers fell behind → swarm corn-stalled → famine.
**Results:** SmallForTwo 72.5→92.5%, Muka 8.8→43.8% (≈ old governor baseline, governor NOT
re-added). Confirms the Muka lever is worker:warrior production ratio.

### Inn feedCap==0: checkerboard forbids the inn's own wheat (2026-06-21)

Root cause of Muka inn-spam / one-swarm collapse: **`obs.feedCapacity` collapses to 0
mid-game**, making the inn-build gate `scoreFeedCapacity` (`CortexPolicyEconomy.cpp:182`,
fires when `totalUnit*100 >= feedCapacity * INN_BUILD_CAPACITY_PERCENT(80)`) degenerate to
`totalUnit >= 0` — always true — so Cortex builds inns every cycle forever, each in the same
dead-wheat zone, while stuck at one swarm. Why feedCap=0: an inn counts only if
`maxUnitWorking>0` AND `>= CORTEX_WHEAT_MIN_TILES(5)` harvestable (non-forbidden) CORN
within radius 5 (`CortexObservation.cpp:82-93`). Diagnostic (seed 4): both inns had a hauler
but `harvestable=0/1` while `nearestWheat=4` forbidden-BLIND — corn EXISTS but is FORBIDDEN
by the **wheat-protection checkerboard**, not depleted. PROVEN causal: forcing
`enqueueWheatForbidden(liftAll=true)` (protection OFF) on seed 4 → feedCap holds 28→56→84,
workers grow 14→28, **loss→WIN**. Reverted (global disable is not a fix — protection is
load-bearing). Coordination failure: inn PLACEMENT gates on ≥5 harvestable tiles, but
PROTECTION then forbids that wheat — the two don't know about each other.

**FIX:** new `Cortex::countSurvivingCornWithin` (`CortexPlacementGeo.cpp/.h`) counts the
OPEN-parity corn the checkerboard leaves harvestable (`((x+y)&1) != WHEAT_PARITY`) — a
paint-timing-independent measure of SUSTAINED harvestable wheat. Swapped in at BOTH gates
that used the forbidden-AWARE `countHarvestableCornWithin`: the feedCapacity inn gate and
the placement HARD-REJECT (`CortexPlacementCandidates.cpp`). Seed-4 Muka: loss→WIN, workers
14→56, feedCap 28→56→84. (NOTE: a later re-audit — see stuck-site deadlock — found the
`countSurvivingCornWithin` switch at gate-2 REGRESSED Muka 42.5→26.6% in one bundling; the
inn-placement fix below is what actually stuck.)

### Inn placement hug-wheat + restock-trips fix (2026-06-22), committed `a697e3b3`

Two changes, validated together (Muka & SmallForTwo vs Nicowar, 80g):
1. **Inn worker count was structurally pinned to 1.** `restockTripsNeeded`
   (`CortexObservation.cpp`) gated each deficit on `Map::ressourceAvailable(team, r, false,
   b->posX, b->posY)` — which probes the inn's OWN footprint corner, always marked
   `GRADIENT_FORBIDDEN` by `updateRessourcesGradient` (`MapGradientGlobal.cpp:141`). So the
   probe was always false, trips=0 → after the 1500-tick settle every inn dropped to
   `CORTEX_INN_WORKER_MIN=1` regardless of how empty. Fix: compute `restockTripsNeeded` =
   raw CORN deficit in trips (corn only; fruit is happiness garnish). Reachability handled
   coarsely via a radius-10 override: `nearestWheatDist < 0 || >
   CORTEX_INN_WHEAT_STARVED_RADIUS(10)` → 1 worker.
2. **Inns placed too far from wheat.** Added gate: `countSurvivingCornWithin(gx, gy, ew, eh,
   CORTEX_INN_WHEAT_EDGE_DIST=1) >= 1` on the GROWN (expansion-inclusive) footprint —
   harvestable corn must be within 1 tile of the final footprint edge (was: up to 5
   Chebyshev tiles via the radius-5 cluster check).

**Results:** Muka **36.2→63.7%** (the within-1 placement change drove this; CIs don't
overlap). SmallForTwo 95.0→96.2% (no regression). Committed bundled with the gate-2 switch +
the Nicowar defense-flag wrap seam fix (per user's "commit everything dirty"; baseline A/B
not run — deltas vs the intermediate build).

### Side-specific collapse — SUPERSEDED (2026-06-15, retired 2026-07-07)

*Retained for provenance; the side-specificity described here is GONE at HEAD `f0335d05`.*
At the time (pre inn fix `a697e3b3`): sweep seeds 1-8 × both orderings showed cortex=team0
WON 8/8, cortex=team1 LOST 8/8 — outcome determined almost entirely by START SIDE.

- **Q2 (deployed warriors starving in the field) — REFUTED with numbers.** Marching cohorts
  had MED_HUNGRY=0, minHungry never below ~30000 (of HUNGRY_MAX=150000); HP loss was COMBAT
  damage, not hunger. The assault force was fine.
- **Q3 (warrior:worker inversion) — CONFIRMED as the t1 loss mechanism.** seed2 t1: economy
  FREEZES at 1 swarm / 2 inns / feedCap 56; 2nd swarm site placed t5030 NEVER finishes
  (freeW=0). growWarrior keeps minting: A climbs 8→23 while W is cannibalized 18→0. Collapse
  to 1 unit by t14000.
- **The t1 collapse reproduces SOLO** (cortex-vs-cortex, passive opponent) → structural, not
  combat. Nicowar CAN win from the t1 start (builds a bigger economy); Cortex specifically
  fails to scale from the slow side.
- **Sharpest root cause (CORTEX_INNGRAD probe): the inns physically CANNOT source corn.**
  Both inns show `restockTripsNeeded=0`, `cornAvail=0`, `cornGrad=GRADIENT_UNREACHABLE` for
  the entire famine, despite Cortex's own `harvestableWheatNearby=7-15`.
  `countHarvestableCornWithin` counts CORN-*type* tiles ignoring ripeness/reachability; the
  engine gradient only flows from COLLECTABLE corn → the inns' local wheat is
  depleted/unripe/unreachable (matching Phase-1 "(c) DEPLETED"). **Worker-mix and
  hauler-priority levers CANNOT save this game — there is no collectable corn to haul.** The
  binding constraint is INN WHEAT-SOURCING / placement — which is exactly what the 2026-06-22
  hug-wheat placement fix addressed, dissolving the side-specificity.

### Muka stuck-site deadlock — current frontier (2026-07-07, HEAD f0335d05, 63.7% baseline)

Trace dissection (5 games, per-inn CSV via `GLOB2_CORTEX_INN_TRACE`, behavior-neutral,
uncommitted). **Dominant loss mechanism** (seeds 36rev/9fwd/38rev/20rev): starting wheat
patch depletes by t≈6000-7000 → second swarm is hard-gated behind the FULL opening ladder
(`openingBuildOutDone` = inn+school+race+hospital+barracks, `CortexPolicyEconomy.cpp:263`;
school took 12k ticks in 9fwd) → when it finally fires, the site is pinned LOW priority and
fed only by the free-worker pour, but freeWorkers=0-2 under wheat stress → **site never
finishes (20rev: 13k+ ticks stuck)** → `swarmSites==0` / `innSites!=0` one-at-a-time gates
stay jammed forever → expansion permanently dead → always-produce mints population into
feedCapacity=0 → famine collapse. Nicowar ends with 2-4 swarms + 8-9 inns vs Cortex 1-2
swarms + 1 inn.

**Hypothesis verdicts (trace-tested):**
- Tier-base feedback loop (base = Σ maxUnitWorking): PARTIAL — a bias (base≤12 → (0,1)-mix
  dominant) but oscillates rather than latches. Secondary.
- Inn stock-not-flow hauler ceiling: REFUTED — at 1-hauler pins during starvation, inn
  buffers are FULL (avgCorn 7-10/10). Feeding fails on inn COUNT and unit distance, not
  restock rate.
- Warriors-through-famine: CONFIRMED amplifier (emergent from tiers) — but symmetric
  (20fwd WON via the same overshoot race).
- Forbidden-blind inn wheat override: REFUTED — discriminator matched zero rows in 5 games.

**Ranked levers (none committed):** (1) un-stall sites under food stress — escalate
fresh-patch swarm/inn sites to NORMAL priority when foodSaturated, and/or a no-progress
watchdog that retires a stalled site (site-priority pin `CortexPolicyEconomy.cpp:420-426`,
pour `:436-449`, gates `:184/:272`); (2) decouple second swarm from the full opening ladder
on `fieldDepleted` (wheat clock ~6k beats ladder clock 4.4k-12k); (3) production-vs-feedCap
governor — secondary; (4) do NOT pull: inn consumption-rate term, override unification.

- **Lever-1 A/B: naive escalation REGRESSED — Muka 63.7→51.2%.** Step-0 probe CONFIRMED
  labour-starvation (stuck site had 0 assigned workers at LOW; deliveries jumped 6→22/35 the
  moment 2-4 workers appeared → materials were fine). Failure: fires on TRANSIENT early
  famines (first escalation t≈2035), yanking haulers during the fragile ramp (19 W→L vs 10
  L→W; seed 9 flipped to DOUBLE WIN, proving the rescue works on the actual deadlock).
  Refinements not iterated: gate on famine persistence / depleted-catchment; escalate to
  NORMAL not HIGH. Diff: `glob2/.tmp/lever1.patch`.
- **Lever-2 A/B: field-depleted ladder bypass IMPROVED — Muka 63.7→67.5%** (54/80), S42
  unchanged 77/80. Shape: `scoreSecondSwarm` bypasses `openingBuildOutDone` when any swarm
  is FIELD-DEPLETED (shared `swarmFieldDepleted` predicate; requires `f.inns>=1` finished).
  One file, `CortexPolicyEconomy.cpp`. High churn (15/80 flipped, 9 L→W, 6 W→L). Seeds
  36/38 → LL→WL; seed 9 STILL LL (swarm lands t=10066 but famine-era freeWorkers=0/1 stalls
  it — likely needs BOTH levers). Patch: `glob2/.tmp/lever2.patch`. +3/80 is within CI
  noise alone; the S42-no-regression + mechanism evidence are what support it.
  **COMMITTED as `4e4e4595`** (2026-07-07, on top of `10b00852`; seed-9-fwd trace
  cross-checked bit-identical to the lever-2 worktree build — swarm tracked t=10066).
- **Refined lever-1 stacked on lever-2 REGRESSED — Muka 49/80** (vs lever-2 54/80), S42
  76/80. Gate `anySwarmFieldDepleted` (regime, not transient famine), target NORMAL (in-
  bucket analysis verified: `prioritize_building` ranks finished inns 2+level*10 ahead of
  sites 1+level*10, so feeding keeps first claim). Fixed the true deadlocks (9/16/18
  LL→WL; 9fwd win t=18690) but the field-depleted regime is near-universal mid-game on
  Muka: ~11 won arms flipped (fwd WW→LW: 4,5,6,12,17,21,30,31,33,34; seed 20 WL→LL).
  Diff: `glob2/.tmp/stacked-lever1.patch`.
- **Stuck-site trigger stacked on lever-2 REGRESSED — Muka 50/80**, S42 **78/80 (best
  ever)**. Narrowest gate tried: per-site latch, trips after 120 decision cycles (≥3000
  ticks) cumulative at ≤1 ACTUALLY-assigned workers (new `TrackedSite.assignedWorkers` =
  `b->unitsWorking.size()`; OBSERVATION_VERSION 17→18), gid-keyed persistent state in
  AICortex (offenseWaves save/load pattern, gid-recycle guard via deliveriesLeft
  monotonicity), one-way latch to NORMAL until site leaves the table. Fixed 9-rev/18-rev
  (+8, +10) but 9-fwd/16 stayed lost and 8 won arms flipped (1,15,21,22,24,31,37,39).
  Diff: `glob2/.tmp/stuck-site.patch` (worktree `glob2/.tmp/stacked-tree`, uncommitted).
- **VERDICT: the site-priority-escalation FAMILY is dead** — three gate shapes
  (foodSaturated→HIGH 41/80, regime→NORMAL 49/80, per-site-stuck→NORMAL 50/80) all lose
  more won games than deadlocks they rescue, and collateral seeds 21/31/12 recur across
  arms → real labour diversion, not lockstep noise. Even a 3000-tick-unworked per-site
  latch fires in games Cortex wins (long-pending LOW inn sites under mid-game wheat
  stress are common in wins). The residual tail needs a different mechanism — likely
  demand-side: why the NORMAL bucket's unmet demand starves the LOW bucket for 13k ticks
  during the depletion regime.

Artifacts: `glob2/.tmp/muka-diag/` (CSVs, timelines, scripts); benchmarks
`glob2/.tmp/bench-{baseline,lever1,lever2,stacked,stuck}-{muka,s42}*`; per-seed tables
`glob2/.tmp/{baseline,lever2,stacked,stuck}-{muka,s42}-perseed.txt`.

### Rank-gate Muka rev regression diagnosed + tuning seam & knob-search driver (2026-07-10)

Traced seed-1-rev (W→L flip under `c9f9d7ad`) vs the pre-lever baseline binary: runs are
decision-identical until t=2968, where the **CAPPED-DRAINING face fires on production-cycle
corn noise** (mUW=7 latched — corn never reaches REM_HI=15 on Muka — corn dip 3, severity 2)
while the patch holds **47 harvestable tiles**; wheat was NOT binding. The site drags 7.6k
ticks at LOW priority, its delivery jobs inflate tierMid → worker-dominant mix → warrior
ramp ~2k ticks late (8-13 vs baseline 13-17 in the 5-8k window) → Nicowar's punish window
lands on the thin army; baseline instead banks the army, fires expansion at t=7104 with
harvWheat=10 (genuinely near-spent) and kills Nicowar by 24k. Seed-4-rev control: same early
fire + tax, but that seed's Nicowar never builds an army → unpunished → per-seed churn.
Trigger also re-fires the moment swarm #2 finishes (orders a #3 that never completes).
Full dissection: `glob2/.tmp/rankgate-diag/FINDINGS.md` (+ traces/replays alongside).

**Params seam committed to disk (uncommitted): `CortexTuning`** (`src/ai/cortex/
CortexTuning.{h,cpp}`) — 11 integer knobs (trigger: expandCornLo / swarmWorkerCap /
swarmCornRemHi / wheatStarvedTiles / NEW expandWheatVeto / NEW expandDebounceCycles;
ranking: scoreSecondSwarmBase/Step / NEW expandSeverityFloor; mix: tierMidDiv /
workerRatioTier2), loaded once from `GLOB2_CORTEX_TUNING=<abs file>` ("key value" lines),
parse errors abort the process (search integrity). Defaults verified **replay-identical**
to `c9f9d7ad` on Muka seeds 1+4 rev (no-env AND explicit-defaults file); debounce streak is
RAM-only (settle-window precedent). Anecdote: `expandSeverityFloor 5` alone flips seed-1-rev
back to a W. **Search driver `tools/cortex-knob-search.py`**: successive halving (blocks
20/+30/+50 paired seeds, keeps 6→3), fitness = MIN across {Muka, SmallForTwo, Mazury},
control always alive with per-seed flip counting, 4 hypothesis configs + 16 random,
checkpoint/resume via `state.json`, holdout mode ({Dejans, balanced_for_2, strange2}, fresh
seeds) kept out of fitness. ~4.5k games ≈ overnight. NOT yet launched.

### Knob search run1 — no winner, defaults stand (2026-07-10)

Full search ran (~4,980 games, 3 rungs, artifacts in `glob2/.tmp/knob-search/run1/`,
`RESULTS.md` + driver log alongside). **Nothing beat the control** on min-across-maps at
100 paired seeds: control min 46.5% (Muka 67.5 / SmallForTwo 96.0 / Mazury 46.5). All 16
random configs died at rung 0; `floor5` (the seed-1-rev anecdote knob) died at rung 1 —
its rung-0 lead was small-sample noise. Finalists: `base5800` ties control's min only by
being decision-identical on Mazury+S42 (+0/−0 flips) while net −3 on Muka; `debounce6`
net-loses both non-trivial maps; `veto24` is a textbook **map trade** — Muka +30/−18
(net +12/48 discordant, ≈1.7σ, best Muka number seen at 73.5%) bought with Mazury +38/−47.
Holdout/dissection skipped (winner-only steps). Takeaway: the expansion-trigger knob space
can shift Muka (consistent with the CAPPED-DRAINING diagnosis) but does NOT touch the
binding constraint — **Mazury ~46% is not an expansion-trigger problem**; further tuning
here only trades maps. Defaults kept; seam + driver remain uncommitted on the working tree.

### Combat envelope batch (attack range + forward base, multi-defense, war-prep level match) — Muka regression (2026-07-11)

Implemented all three Nicowar-gap combat features at once on top of `12947373`
(user-directed batch; observation v18 / action v13, uncommitted): (1) attack-range
gate — offense commits only on a target within `attackRangeBase + perWalkLevel×slowest
warrior WALK` of the nearest finished inn (maxed w/ hospital when one exists), with a
NEW forward-inn/hospital build (`scoreForwardBase`, decide class 18) when every target
is out of range, waived when no legal forward spot exists; (2) defense flag → SET of up
to 3 flags on separated threatened buildings, each sized 3× local visible threat;
(3) war-prep level match — normal commit counts only warriors with ATTACK_STRENGTH ≥
highest enemy level ever seen (FOW-latched, serialized), capped at barracks-trainable
(level+1, no deadlock); blitz bypasses all gates. Knobs: `attackRangeBase 32`,
`attackRangePerWalkLevel 8`, `warPrepLevelMatch 1`. Mechanically verified live (class 18
fires, forward inns built, waves muster at innDist≈5).

**Benchmark (100 paired seeds, swap-sides, vs run1 control): Muka 44.0% (−23.5pp, flips
+10/−57 — a real, large regression), Mazury 43.5% (flips +43/−49, net −6 ≈ noise; huge
churn though), SmallForTwo 96.5% (+4/−3, unchanged).** Batch is net-negative; per the
one-at-a-time lesson the three features can't be attributed from this run alone, but the
prior measured commit-size lesson (ATTACK_MIN_WARRIORS comment: anything that DELAYS the
first strike turtles us into Nicowar's maturing economy) points at the two delay-adding
gates (range/forward-base detour + level match) as prime suspects on Muka, which the old
early-harassment commit was winning at 67.5%. Attribution is cheap without new code: the
knob seam can disable each gate (`attackRangeBase 0`, `warPrepLevelMatch 0`) — a 3-config
paired run isolates each feature's delta; multi-defense is the only unknobbed structural
change. NOT adopted; working tree left as-is pending user decision.

### Muka flip diagnosis: range gate never opens, level gate throttles its own cure (2026-07-11)

Per-seed analysis of the hard-flipped Muka seeds (control 2/2 → batch 0/2: 18, 25, 41,
51, 63, 64, 68, 87, 95, 96), deep-traced on 87-rev (fastest collapse, 18,786 ticks) with
a new gated `CORTEX_DUMP_GATES` stderr dump (AICortex.cpp, prints latch / own-level
histogram / effective range / inRangeSlot / fwdInn-fwdHeal valid+underway / per-target
supportDist each cycle; read-only, sync-safe). Key facts, all measured:

- **The range gate never opened**: `inRangeTargetSlot = -1` from tick 0 to t=15151.
  Muka targets sit at supportDist 46–64 vs range 32–40, every cycle. Normal offense was
  hard-blocked the whole game; the only attacks were famine-blitz pulses (foodSaturated
  bypass). Control (same seed, clean 12947373) attacked continuously from t≈7700 with
  the raw `warriors>=8` commit and won at 21,634.
- **The envelope needs TWO forward buildings, not one**: supportDist = max(nearest
  finished inn, nearest finished hospital) (CortexObservationObserve.cpp support pass),
  so once the colony owns its home hospital a forward INN alone can never open the gate
  — the home hospital's distance dominates the max. 87-rev opened the envelope only at
  t=15151 (d52→d13) after BOTH a forward inn and a forward hospital finished, ~7,600
  ticks after the first forward site went up. By then the colony was being razed.
- **The level gate throttles the range gate's cure**: scoreForwardBase requires
  `matchedWarriors >= ATTACK_MIN_WARRIORS`. Latch hit 1 at t=8049 while ownStr was
  [8,0,0,0] → matched=0 → class 18 blocked; it fired (10766) exactly when the level-1
  count reached 8, ~2,700 ticks after a forward candidate was first valid.
- **forwardInnUnderway false-positives**: ANY friendly food/heal construction site
  within maxD of tgt0 counts as "the forward base is underway" — including ordinary
  economy inn sites near mid-map wheat — and tgt0 itself reorders as new enemy buildings
  are discovered, so valid/underway flapped for thousands of ticks while contested
  forward sites never finished. "Waived when no legal forward spot exists" never
  triggers in this state: a spot exists, it just can't be completed → turtle forever.

**Knockout matrix (seeds 87, 51, 18, both sides, tuning-seam knockouts, multi-defense
always on): all-on 0/6, rangeoff 5/6, lvloff 1/6, bothoff 6/6.** bothoff reproduced the
control games BIT-IDENTICALLY (same ticks + order counts) on all six — multi-defense and
the rest of the batch are behaviorally inert on these seeds; the whole regression is the
two gates. Range gate = dominant (on s51 rangeoff == bothoff bit-identical; level gate
never binds there), level gate = real secondary (needed on 87-rev, flips 18-rev alone).

Implementation defects (design kept, per user direction): (1) hospital term makes the
support max conjunctive — a forward inn alone should extend the envelope; (2)
scoreForwardBase must not be gated on matchedWarriors — building an inn is workers'
business; (3) no fallback while the cure is pending — the waiver keys off placement
LEGALITY (`forwardPossible`), but ordering the build additionally needs canExpand +
GATE_BOOTSTRAP|GATE_LABOR + winning the economy argmax, so a merely-"possible" forward
base that is never even ordered holds the gate shut indefinitely, with no
progress/timeout check; (4) underway detection should track sites the forward path
actually ordered (gid), not any food site near a reordering tgt0; (5) minor: effective
range flickers 40→32 whenever a fresh walk-0 warrior spawns (slowest-occupied-level
formula). Related, unmeasured on Muka: multi-defense sums its recall deficit across up
to 3 flags (each 3× local threat) vs the old single 3× count, so lighter harassment now
triggers clearAllOffenseFlags() — inert on the three seeds tested, but a prime
candidate for the Mazury ±43/49 churn. No fixes applied yet; artifacts in
`.tmp/flip87/` (traces, replays, knockout logs, tuning files).

### Gate fixes implemented — Muka mostly recovered, freeze mode eliminated (2026-07-12)

Implemented the five defects above (uncommitted, on top of the batch): (1) supportDist
= nearest finished inn only, hospital advisory (CortexObservationObserve.cpp); (2)
scoreForwardBase ungated from matchedWarriors; (3) forward-site underway now
position-tracked in AICortex (serialized forwardInnX/Y, forwardHealX/Y set when
translateActionBuildForward emits the order; reconciled each cycle: site at pos →
underway, finished → clear, gone past buildCooldown → clear) replacing the proximity
scan and its candidate-surfacing guards; (4) grace waiver — NEW knob
`attackRangeGraceTicks` (default 2400, 0 = never waive), serialized
`rangeGateBindingSince` armed while (targets valid ∧ warriors>0 ∧ inRangeSlot<0)
holds; past the grace the commit attacks out-of-envelope while the forward base keeps
building (`obs.rangeGateWaived` echo); (5) computeOffenseCommit gained `sustain`:
scoreRetireFlag now sustains on the UNGATED commit (raw warriors ≥ bar), strictly
weaker-to-fail than the gated start — gates govern starting an offense, never
abandoning one. CORTEX_GATES dump extended with `waived=`.

Mechanically verified on 87-rev: waiver fires at binding+2400, waves march through a
mid-war latch rise 1→2, the forward inn ALONE opens the envelope (supportDist 52→32→14
as it finishes), tracked site clears on completion. Smoke matrix (the 3 worst flipped
seeds × both sides, all gates on): 5/6 wins, was 0/6. The one loss (51-rev) is a
genuine attrition war (30-warrior peak, continuous waves), not a gate freeze.

**Benchmark (100 paired seeds, swap-sides, vs run1 control): Muka 58.5% (batch was
44.0%, control 67.0%; flips vs control +13/−31, hard 2→0 flips 1 — seed 43 — down from
10), Mazury 46.5% (== control 46.5), SmallForTwo 95.0% (control 96.0, noise).** The
freeze failure mode is gone; the residual Muka −8.5pp is a broad shallow spread — the
by-design cost of gated first strikes (and possibly the multi-defense recall
sensitivity), no longer a single mechanism. Knob surface for a search now exists:
attackRangeBase / attackRangePerWalkLevel / attackRangeGraceTicks / warPrepLevelMatch.
Bench artifacts: `.tmp/gatefix-bench/`. NOT adopted; awaiting user decision.

---

## 3. ML pilot (effort B)

Effort B = actual ML training on Cortex. Plan: `docs/AI/cortex/PILOT.md` (repo-root docs/,
NOT git-tracked). **subagents CANNOT run the game binary** (permission layer) — run all
games in the MAIN thread; they CAN run tool binaries + scons. teams==2 maps for
benchmarking: **Dejans, Mazury, Muka, SmallForTwo, balanced_for_2, strange2** (matchup size
must == map `getNumberOfTeams()`, via `-dump-resources <map>`).

### Worker-cap pilot (swarm `tuneWorkers`)

Target: replace the swarm worker-cap loop in `CortexPolicy::tuneWorkers`
(`CortexPolicyEconomy.cpp:224-248`) with a learned per-swarm policy. Absolute action
{1..20} masked to `swarmWorkerCap`. Determinism kept via I16F16 integer inference (argmax,
no softmax). Method: BC warm-start → offline RL → self-play. Reward = dense/local
(corn-in-band), computed offline in Python.

Four infra pieces ALL DONE (parity baseline reached):
1. Trajectory dump — `AICortex::dumpWorkerTrace` (`AICortexDebug.cpp`),
   `GLOB2_CORTEX_TRACE=<ABS-prefix>` → `<prefix>.team<N>.csv`. Determinism-neutral.
2. Corpus — `glob2/.tmp/corpus/` 44,357 rows, SmallForTwo+Muka × 4 matchups × 4 seeds.
3. BC trainer — `glob2/tools/cortex-ml/` (numpy only). 99.52% val parity. Output f32 JSON
   `cortex-mlp-f32-v1`.
4. Fixed-point — `glob2/tools/cortex-ml-infer/` + `glob2/src/ai/cortex/CortexNet.{h,cpp}`,
   blob `cortex-i16f16-v1`. C++↔numpy parity 0/1000; real net 99.80% vs hand rule.
5. Wired into `tuneWorkers` (swarm caps only) behind `GLOB2_CORTEX_POLICY=ml` +
   `GLOB2_CORTEX_NET=<abs blob>`. Determinism gate PASS. Parity vs Nicowar SmallForTwo 32g:
   ML 87.5% vs baseline 93.8% (overlapping CIs = parity within noise).

Contract: `docs/AI/cortex/ML_CONTRACT.md` (16 feats → 32→32→20, I16F16, argmax, clamp +
mask). Offline RL (step 6): AWR matched the hand rule (dense proxy gave little advantage
signal), CQL mixed. Artifacts experimental in `glob2/.tmp/` (`cortex_awr.*`, `cortex_cql.*`),
NOT committed.

### Decision-net pilot (learn the `decide()` utility scores) — 2026-06-07

Higher-leverage seam: learn the `decide()` utility SCORES (colony-level strategy selection),
not just worker caps. Replace each scorer's score magnitude only; keep action-construction +
decline gates hand-coded (decline = hard eligibility mask). 48 raw feats → 64→64→18 masked
argmax. Reward = SPARSE terminal win/loss + potential-based shaping. Plans:
`docs/AI/cortex/DECIDE_PILOT.md` + `DECIDE_CONTRACT.md`.

BC PILOT BUILT & PASSING (steps 1-5):
- Trace: `CortexPolicy::extractDecideFeatures` (48-feat SSOT) + `DecideTrace` in `decide()`
  + `AICortex::dumpDecideTrace` (`GLOB2_CORTEX_DECIDE_TRACE=<abs prefix>`).
- Corpus: `.tmp/decide_corpus/` 480 CSVs / 462,300 rows; 56,726 popcount≥2 (learnable).
- BC: `tools/cortex-ml/train_decide_bc.py`. Val 99.52% / **98.52% on popcount≥2**.
- Export+parity: `CortexNet::loadDecide`/`scoreDecision`/`forwardDecide`. C++↔numpy 0/5000.
- Wired: `GLOB2_CORTEX_POLICY=ml-decide` + `GLOB2_CORTEX_DECISION_NET=<abs blob>`.
  Determinism PASS. Broad parity matrix (3 maps × 3 opps × 80g): every cell ±2.5pp;
  aggregate ML 76.4% == baseline 76.3%. BC clones the hand rule everywhere.
- **RL headroom = Muka** (26% vs Nicowar). **Step 6 AWR pass 1 DONE — safe but FLAT:**
  `decide_reward.py` (Φ variant-A FOW-only, Φ↔outcome corr 0.44) + `train_decide_awr.py`.
  Bit-parity 0/5000 (the gate CAUGHT a naive |W|≈43k I16F16 overflow → fixed by freezing
  layer0 at BC fold scale + weight decay). AWR 76.4% == baseline; **Muka-vs-Nicowar UNMOVED
  26.2→26.2%.**

**RESOLVED: Muka weakness is NOT decision-selection — the AWR null was correct.** RL can't
reach it because it's a production-mix/gate bug, not a pick-among-eligible-candidates
problem. (⚠️ The original 2026-06-07 explanation — "Cortex fields peak 1 warrior, army-size
keystone" — was WRONG, a corpus misread; the col-19 `warriors` field peaks at 9-50. Real
cause = population overshooting wheat → starvation collapse; see the feeding governor
section.) **ML re-enters LATER only as the learned blitz-trigger threshold** (the
`foodSaturated` level + `BLITZ_MIN_WARRIORS` become the seam) — NOT for the governor/blitz
mechanism. The two ML seams (worker-cap `ml`, decision `ml-decide`) are independent env
values, mutually exclusive for now. Utility switch (`76c4c5c6`) changed `decide()` only; the
worker-cap seam runs PARALLEL and is untouched.
