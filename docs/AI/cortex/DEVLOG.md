# Cortex AI — Development Log

Condensed research record for the Cortex C++ AI, written for agents deciding on future
refinements: adopted designs, root causes, measured dead-ends (do NOT re-attempt), and
the tooling to re-verify. Entries are dated point-in-time findings; `file:line` cites
may be stale — verify against current source. Condensed 2026-07-12; the full
blow-by-blow history is in this file's git log.

Current one-paragraph state: agent memory `project_cortex_status`. Durable invariants
live in their own memories: `project_cortex_wheat_forbidden_removal` (paint-removal
invariant), `project_aicortex_handcrafted_first` (effort A/B plan),
`project_ai_benchmark_harness`, `project_nicowar_defense_flag_wrap_crash`.

Benchmarks use `glob2/tools/ai-benchmark.sh --swap-sides` (Muka/SmallForTwo have strong
side bias); "vs Nicowar", 80 games unless noted (2026-07 entries: 100 paired seeds =
200 games). Combat deltas at 60 games are noise — use 120+ and watch a castor control.

## Tooling quick reference

- Headless game: `GLOB2_TEST_SEED=<n> GLOB2_REPLAY_PATH=<abs> ./build/src/glob2
  -test-games-nox 1 --map <M> --matchup cortex,nicowar` (first matchup entry = team0;
  winner_team matching Cortex's team = win). Build `scons release=1 -j16`.
- Map tools: `-dump-wheat <map> [team]`, `-dump-resources <map>` (both in Glob2.cpp).
- Read-only, sync-safe stderr dumps: `CORTEX_DUMP_GATES` (offense gates per cycle),
  `CORTEX_DUMP_OFFENSE`, `CORTEX_DUMP_ATTACK` (one-shot at first under-attack),
  `CORTEX_DUMP_PERIODIC` (econ trace).
- CSV traces (`=<abs prefix>`): `GLOB2_CORTEX_TRACE` (worker-cap), `GLOB2_CORTEX_DECIDE_TRACE`
  (per-cycle decide eligibility/chosen), `GLOB2_CORTEX_INN_TRACE` (per-inn). Prefix must
  be basename-clean and output verified non-empty — a slashed prefix once produced empty
  traces and a false "16/16 byte-identical" A/B.
- Knobs: `GLOB2_CORTEX_TUNING=<abs file>` ("key value" lines; parse errors abort the
  process so a knob search can't silently measure defaults).
- Determinism: syncRand only, no std::set. Subagents may build but CANNOT run the game
  binary — run all games in the main session. Scratch in `glob2/.tmp/`, absolute paths.

---

## 1. Economy foundation (2026-06-05)

### Wheat protection (checkerboard forbidden) — committed `65b95f1b`

Paint a checkerboard forbidden pattern `(x+y)&1` over CORN so workers harvest one half
while the protected half stays full and reseeds it (forbidden blocks harvest but NOT
growth — `MapStep.cpp` expand branch). Geometry/reconcile core is `CortexWheat.{h,cpp}`
(no `Player*`, no `Orders`). Depth model: land+wheat BFS from the inn's walkable EXIT
RING (not the buried centre tile) — land steps cost a move, only wheat counts toward
depth. Rejected: centroid/edge-silhouette (staircase false-fronts), zero-cost land
flood (onion rings, no directionality). Live reconcile emits
`OrderAlterateForbidden(MODE_DEL then MODE_ADD)`; the open-margin N is drawn ONCE per
game via `syncRand()%3` and persisted (never redrawn on load — the draw shifts the
shared RNG stream, replay-relevant). N capped 0–2 (real starter fields are ~5–7 deep).

### Closed-loop wheat economy + never-halt — committed `dae6bb44`..`82efa919`

- **Worker tuning**: nudge each swarm/inn `maxUnitWorking` ±1 per cycle inside
  corn-buffer deadbands (`ACTION_TUNE_WORKERS` → `OrderModifyBuilding`, Castor-style
  local write + `update()` + order).
- **Engine facts driving thresholds**: swarm holds 20 CORN, 5/unit, 1 unit/150t,
  STALLS at corn<5; extra workers only refill the buffer, they do NOT speed production
  → "at worker cap" is the expansion signal. Inn holds 10, feeds ~5× a swarm's draw.
- **Pre-combat panic defense**: when attacked before combatPhase — swarms to 100%
  warriors, engine HIGH priority (`ACTION_SET_PRIORITY`), panic hospital.
- **Never-halt / always-expand (the big win)**: swarm ratio NEVER {0,0,0}; deleted the
  `sustainable=feedCapacity*2` halt and all overCapacity/popCap/shouldGrow machinery
  (it froze the colony at the first inn — root cause of ~77% Warrush losses; placement
  was NOT the blocker). Feed-led inns (`feedCapacity < totalUnit` — capacity vs
  population, wheat shortfalls are the tuning loop's job); worker-dominant combat mix;
  always-expand ladder inn→school→racetrack→hospital→barracks gated on `canExpand`.
  Every SmallForTwo matchup flipped (Warrush 22.9→58-65%, Nicowar 16→72.9%, Castor
  51→91.7%).
- **Expand-vs-upgrade**: %-of-units-maxed (60%) + spare labour; barracks need ≥2
  finished before upgrading (laggard-first); school/racetrack single-instance; inn
  upgrades feed-safe; hospital COUNT grows with army. Engine: hospital heals 2/5/7 HP
  at L0/1/2 in 30/18/6 ticks. Doc: `upgrade-expand-mechanics.md`.
- **Upgrade-blackout fix**: an issued `OrderConstruction` is INVISIBLE in the
  observation while the building evicts units / waits for the larger footprint
  (`Construction.cpp`), so the policy stacked a second upgrade and took a whole class
  offline. Fix: per-class one-upgrade-in-flight guard `pendingUpgradeType`
  (serialized), released on visible site or 2000-tick timeout.

### Offense findings — committed `290c431d`

Ruled out on SmallForTwo measurements — **do not re-attempt**:
- Explorer flags for active scouting: free-roaming explorers already discover the map
  (engine idle fog-gradient wander, auto-avoids towers); a PINNED explorer is worse —
  dies in 2-3 tower shots.
- Targeting enemy `startPosX/Y`: rejected as a FOW cheat.
- Tower-avoidance for offense: armies melt to enemy warriors, not towers.

Real defect: the single WAR_FLAG oscillated home/enemy every 50-tick cycle on light
harassment (army melted in transit, offense fired ~once/game). Fix: offense-hold
hysteresis — committed offense holds `OFFENSE_HOLD_TICKS`=600, defensive recall only at
≥`DEFENSE_SERIOUS_BUILDINGS`=2 buildings under attack; `ATTACK_MIN_WARRIORS` 12→8.

### Swim pool + second swarm

Pool: `CortexWater.cpp` `assessSwim()` — discovered-ALGA scan + bounded land-vs-swim
reach flood fill. AICastor's `computeNeedSwim` formula is INVERTED/buggy — do NOT copy
it. Second swarm gated on opening build-out + swarm supply stress. Both measured ≈ −4
to −6pp on SmallForTwo at introduction but kept.

---

## 2. The Muka investigation

### Map overfit discovered (2026-06-07)

**Do not treat SmallForTwo win rates as general strength — benchmark Muka + larger maps
before declaring a win.** Initial Muka vs Nicowar 32.3% (vs 87% on S42). The economy
angle was first WRONGLY ruled out (fixing 1-swarm-stuck `9bbec681` barely moved it);
the correction: it WAS economy — population overshooting wheat. Cortex became
utility-scoring at this point (`76c4c5c6`: every decision returns a score, `decide()`
picks max).

### Feeding governor — starvation-collapse root cause (2026-06-08), committed `9baf5133`

The "peak 1 warrior / army-size keystone" corpus read was FALSE (Cortex fields 9–50
warriors on Muka). **Real root cause: population overshoots wheat → chronic-starvation
collapse** — win/loss splits cleanly on starving-fraction, not army size. Two coupled
defects: no population governor (swarms mint workers through famine — the swarm's local
corn buffer stays full while inns starve), and `combatPhase` conflating "established"
with "!starving" (famine → workers-only production → more mouths + no army
replacement → death spiral). Fix: split into `economyEstablished` / `combatPhase
(=established && !starving)` / `foodSaturated (=established && starving)`; feeding
governor (growWorker=0 in famine); growWarrior keyed to economyEstablished (keep
building army during famine); wheat-blitz (`BLITZ_MIN_WARRIORS`=4, SCORE 6700 + a
one-time `liftAll` full-field harvest burst — the ONE sanctioned override of the
checkerboard invariant). Muka 22.5→47.5% train / 41.2% held-out (mild overfit; the
starve-to-timeout mode died, decisive mid-game losses remain).

Post-fix drill killed 4 hypotheses (governor-blind-in-bootstrap; governor too weak;
growWarrior over-minting — losing armies are 3× SMALLER, high warrior share is a
symptom; offense-fails — Cortex attacks in 82% of losses). **Loss signature =
economy SCALE / wheat throughput**: losses black out `feedCapacity==0` in 18% of
mid-late cycles, stall ~39 units, starve. feedCap==0 is a GATE-ESTIMATE collapse (the
radius-5 harvestable-corn probe), NOT literally empty inns — 57-96% of blackout inns
still hold corn hauled from beyond the probe radius.

- Fresh-patch relocation lever and max-swarm-cap removal (`13c2f4c9`, kept as cleanup):
  both FLAT. More swarms = more mouths; a 6-swarm/120-unit colony still starves.
- Hospital rungs moved off combatPhase (`75bca958`): correct design, behaviorally inert
  (byte-identical A/B) — the army only outgrows the old cap while starving.
- **feedCap sensor "fix" TESTED & REJECTED — do not re-attempt.** Correcting the
  radius-5 probe to a radius-10 haul reach cut false blackouts ~93% and CRATERED Muka
  42.5→12.5%. The understated feedCap is LOAD-BEARING: it keeps `capacityShort` true →
  stacks 5-9 inns → ~2× population → army crosses the ~50-60 win threshold. **Wins are
  carried by ARMY SIZE; the lever is the inn-build/economy-expansion driver, not
  offense timing.**

### Famine disables defense (2026-06-10) — known gap, unfixed

`scoreDefense` is gated on `combatPhase` (= … && !starving), so a starving colony
declines the recall entirely — while the SAME starvation fires the offense blitz
(SCORE 6700 > SCORE_DEFENSE 4000), throwing the whole army forward exactly when home is
overrun. Fix directions (not done): recall on serious assault regardless of famine;
SCORE_DEFENSE outranks blitz when home is seriously hit.

### War execution is NOT the Muka lever (2026-06-13 → 06-21) — four dead-ends

- Retreat + units-based serious-defense + re-arm-on-commit offense-hold: net-neutral
  (17/32 → 17/32). The retreat misreads winnable pushes: the war flag caps at 20 units
  and `veteranFlagLevel` holds low-levels home, so force-at-front ≪ available.
- Muster-then-march: REGRESSED to 12/32 — committing a bigger army deepens the
  overpopulation famine (137 units vs feedCap 56).
- Offense wave pipeline: 14/32, still below baseline. Real bug fixed en route: gate
  muster/spend on `flag->unitsWorking.size()` (transit-invariant), never spatial
  warrior counts (read ~0 during a cross-map march).
- Removing the starving gate from combatPhase, and forcing 2 explorers: byte/outcome
  neutral — the colony dies in the famine spiral regardless.

**Bottom line (confirmed 4×): the decisive lever is worker:warrior production /
feeding, not war execution.**

### Warrior overproduction (timeline traces)

Loss mechanism: Cortex out-booms early, then over-produces warriors on a too-small
worker/food base (warriors > workers at crisis; responds to starvation by minting MORE
warriors). Nicowar keeps ~4:1 workers:warriors and ramps only after the economy is
large. Deploy-vs-produce imbalance: offense is ELIGIBLE nearly every cycle but loses
the utility argmax to economy actions; one flag commits ≤20 veterans while multi-swarm
production outruns the trickle.

### Worker-target tiers — committed on feat/ai-trainer-support

3-tier worker-COUNT rule replaced both the old throttle and the governor: `base` =
Σ(swarm+inn maxUnitWorking)+2 (hauler floor); `needs` = workers + fillable jobs; `mid`
= base+(needs−base)/2; below base → workers only, below mid → 8:1 worker-dominant,
above → warriors (never-{0,0,0} kept). The old rule flipped workers→warriors the
instant `base` was met — on wheat-poor Muka that hit at ~13 workers with 16 open jobs →
warriors during a labour shortage → corn stall → famine. S42 72.5→92.5%, Muka
8.8→43.8%. **Confirms worker:warrior production ratio is THE Muka lever.**

### Inn feedCap==0: checkerboard forbids the inn's own wheat (2026-06-21)

Coordination failure: inn PLACEMENT gates on ≥5 harvestable tiles, then wheat
PROTECTION forbids that same wheat → feedCap collapses to 0 → the inn-build gate
degenerates to always-true → inn-spam on dead wheat. Fix: `countSurvivingCornWithin`
(open-parity corn the checkerboard leaves harvestable — paint-timing-independent) at
the feedCapacity gate.

### Inn placement hug-wheat + restock-trips fix (2026-06-22), committed `a697e3b3`

1. Inn worker count was structurally pinned to 1: `restockTripsNeeded` probed
   `Map::ressourceAvailable` on the inn's OWN footprint corner — always
   `GRADIENT_FORBIDDEN` → trips always 0. Fix: raw CORN deficit in trips + radius-10
   wheat-starved override.
2. Inns must have surviving corn within 1 tile of the GROWN footprint edge (was: within
   radius 5).

Muka **36.2→63.7%** (the within-1 placement change drove it), S42 96.2%. This also
dissolved the earlier side-specific collapse (team1 lost 8/8, reproduced SOLO —
root cause was inns physically unable to source corn: local wheat depleted/unreachable,
`cornGrad=GRADIENT_UNREACHABLE` all famine; worker-mix levers cannot save a game with
no collectable corn to haul).

### Muka stuck-site deadlock (2026-07-07) — site-priority-escalation family is DEAD

Dominant residual loss mechanism at the time: starting patch depletes t≈6-7k → second
swarm hard-gated behind the FULL opening ladder → site pinned LOW priority with
freeWorkers 0-2 → **site never finishes** → one-at-a-time expansion gates jammed →
famine. Lever-2 (field-depleted ladder bypass) IMPROVED Muka 63.7→67.5, committed
`4e4e4595` (later subsumed when `c9f9d7ad` deleted the ladder gate).

**Do not re-attempt site-priority escalation.** Three gate shapes — foodSaturated→HIGH
(41/80), depleted-regime→NORMAL (49/80), per-site 3000-tick-unworked latch→NORMAL
(50/80) — ALL lost more won games than deadlocks rescued (lever-2 alone: 54/80);
collateral seeds recur across arms → real labour diversion, not noise. Even the
narrowest per-site latch fires in games Cortex wins (long-pending LOW inn sites under
mid-game wheat stress are common in wins). The residual tail is demand-side: why the
NORMAL bucket's unmet demand starves the LOW bucket for 13k ticks during depletion.
Step-0 probe did CONFIRM labour-starvation (deliveries jumped 6→22 the moment workers
appeared), so the diagnosis stands — only the escalation family of fixes is dead.
Artifacts: `.tmp/muka-diag/`, `.tmp/{lever1,lever2,stacked-lever1,stuck-site}.patch`.

### Rank-gate regression + tuning seam (2026-07-10)

`c9f9d7ad` centralized decide() feasibility gates and moved expansion to ranking (ladder
gate deleted; user decision KEEP). Seed-1-rev W→L traced: the CAPPED-DRAINING face
fires on production-cycle corn noise (corn never reaches REM_HI=15 on Muka) while the
patch still holds 47 harvestable tiles → LOW site drags 7.6k ticks → its delivery jobs
inflate tierMid → worker-dominant mix → warrior ramp ~2k ticks late into Nicowar's
punish window. Full dissection: `.tmp/rankgate-diag/FINDINGS.md`. **CortexTuning seam +
`tools/cortex-knob-search.py` committed `12947373`** (defaults verified
replay-identical; debounce streak RAM-only per the settle-window precedent).

### Knob search run1 — no winner, defaults stand (2026-07-10)

~4,980 games, successive halving, fitness = MIN across {Muka, S42, Mazury}: **nothing
beat control** (min 46.5%: Muka 67.5 / S42 96.0 / Mazury 46.5 at 100 paired seeds). All
16 random configs died rung 0; the floor5 seed-anecdote knob died rung 1 (noise);
veto24 = textbook map trade (best-seen Muka 73.5% bought with Mazury net −9 flips).
**Mazury ~46% is NOT an expansion-trigger problem — tuning that knob space only trades
maps.** Artifacts: `.tmp/knob-search/run1/RESULTS.md`.

### Combat envelope arc (2026-07-11 → 07-12) — committed `05b497bc`, `6b78d692`, `cae850e6`

Batch of three Nicowar-gap combat features (obs v18 / action v13): (1) attack-range
gate — offense commits only on a target within `attackRangeBase 32 +
attackRangePerWalkLevel 8 × slowest warrior WALK level` of inn support, with a forward
inn/hospital build (decide class 18) when every target is out of range; (2) defense
flag → SET of up to 3 flags on separated threatened buildings, each 3× local visible
threat; (3) war-prep level match — normal commit counts only warriors with
ATTACK_STRENGTH ≥ highest enemy level ever seen (FOW-latched, serialized), capped at
barracks-trainable (no deadlock). Blitz bypasses all gates. Batch alone: Muka 44.0%
(−23.5pp). Knockout matrix proved the whole regression was the two delay gates;
multi-defense + the rest were BIT-IDENTICAL inert on the tested seeds.

Five implementation defects found and fixed (design kept, per user direction):
1. supportDist was max(inn, hospital) — conjunctive, so a forward inn alone could never
   open the envelope once a home hospital existed. Now inn-only, hospital advisory.
2. `scoreForwardBase` was gated on matchedWarriors — building an inn is workers'
   business; ungated.
3. A "possible" but never-ordered forward base held the gate shut forever (ordering
   additionally needs canExpand + labour + winning the argmax) → grace waiver: knob
   `attackRangeGraceTicks` 2400, serialized `rangeGateBindingSince`; past grace the
   commit attacks out-of-envelope while the forward base keeps building.
4. Underway detection was a proximity scan that false-positived on ordinary economy
   food sites near a reordering target → serialized ordered-POSITION tracking
   (forwardInnX/Y, forwardHealX/Y; reconciled each cycle, cleared on finish/cooldown).
5. Gates govern STARTING an offense, never abandoning one: `scoreRetireFlag` sustains
   on the UNGATED commit (strictly weaker-to-fail than the gated start → no
   retire/re-commit thrash).

Then two follow-ups closed the residual:
- **Forward-build terrain fix (`6b78d692`)**: forward candidates additionally require
  `Map::isFreeForBuilding` — the EXACT `Game::executeCreate` predicate for an immediate
  construction site. A ground unit on the footprint deferred the order into a
  buildProject the position latch can't track → 250-tick cooldown expiry → re-order
  churn. Muka 58.5→63.5%.
- **First-contact waiver (`cae850e6`)**: key insight — **flag targets ARE discovered
  enemy buildings** (`placeFlagTargets` uses the same seenByMask predicate as the
  totalBuilding intel), so the range gate first binds the moment the FIRST enemy
  building is discovered; a "waive while zero buildings discovered" rule is provably
  dead code (verified bit-identical). Actual failure shape: first discovery is already
  out of envelope, gate holds the full 2400-tick grace while an ungated opponent
  strikes at first contact and wins. Fix: waive while ≤1 enemy building is discovered
  (knob `attackRangeUnscoutedWaiver` default 1); bind-then-grace governs once the enemy
  base is actually mapped.

**Final: Muka 66.0% / Mazury 51.0% / SmallForTwo 95.0% vs clean pre-envelope control
67.5 / 46.5 / 96.0** — within noise of control with the envelope kept. Known residuals,
deliberately unfixed: 7-rev (waiver can't fire — ≥2 buildings discovered at bind; grace
600 made it WORSE, dragging to t=53k), 90-fwd (separate mechanism; diagnostics in
`.tmp/fwdfix/s90-*`, unanalyzed). Multi-defense recall sensitivity still suspect for
Mazury churn (sums deficit across up to 3 flags vs the old single count — lighter
harassment triggers clearAllOffenseFlags). Knob surface: attackRangeBase /
attackRangePerWalkLevel / attackRangeGraceTicks / attackRangeUnscoutedWaiver /
warPrepLevelMatch. Prior lesson that shaped the diagnosis: anything that DELAYS the
first strike turtles Cortex into Nicowar's maturing economy.

**Flag fixes (2026-07-16, `ef6a79c5`)**: the Mazury dissection named two mechanisms —
orphaned war flags (teardown racing ensureFlagAt's unlatched OrderCreate leaves an
untracked flag pinning up to 20 warriors forever) and defense-deficit offense teardown
churn on light harassment. Fixed: per-cycle sweepOrphanWarFlags (two-cycle RAM-only
settle window spares pending creates) and a serious-assault gate (scoreDefense's
predicate) on the deficit release. Orphans verified gone on the dissected seeds
(owned=0 dumps 399/23/6 → 0), but 100-paired-seed win rates unchanged within noise:
Mazury 50.0 (control 49.5), Muka 65.0 (66.0), SmallForTwo 95.5 (95.0). The grind-down
losses have another binding cause; residual ~15pp team0 side bias unexplored.

**Seed-3 dissection + amphibious staging (2026-07-17, `5740f2a9`)**: per-cycle
CORTEX_TRUTH both-teams dump named the loss shape on Mazury seed 3 (both-sides loser):
feeding fine, warriors out-trained early (101 vs 43 at t=26k), but the army was bound to
war flags from birth (freeWarriors=0 t=3k–19k, so no warrior ever idled to learn SWIM),
trickled into the enemy base ~5-at-a-time (avg 4.5 of ~14 bound arrived over the whole
offensive), and died 101→30 in 8k ticks; meanwhile the all-warrior swarm mix froze
workers at ~50 vs Nicowar's 240 and tech at lvl 1 (school site unfinished ~30k ticks).
Shipped: swim-aware amphibious classifier (dual BFS, amphibious iff swim path strictly
shorter), landing-zone CROSS phase with arrival-gated fleet release, swim-staging muster
hold, muster "massed" now counts ARRIVED not bound. On seed 3 the classifier correctly
answers NOT amphibious — field dump shows resource walls pinch land and swim routes
through the same corridor (dists locked equal 55/55) — so that seed's binding cause is
LONG-MARCH logistics: warriors hunger-commute 40 tiles front→inn (arrived oscillates
0→15→3), and even the home muster times out at 2-3 of 20 arrived. Seed 3 still a loss
(46.0k vs 52.4k); no benchmark run. Next: forward rally staging for long land
approaches (reuse CROSS + path-dist from the BFS) coupled to the forward-inn machinery.

**Forward rally staging + benchmark disaster (2026-07-17, UNCOMMITTED working-tree
diff)**: implemented forward rally staging for long land marches (obs v20, knob
forwardRallyPathDist=30, CROSS anchor generalized to a staging point picked at the
landing standoff on the shortest-path corridor), forward-inn coupling (candidate
anchors on the staging point; scoreForwardBase fires on staged campaigns), and a
muster-timeout restart while arrivals net-increase (OffenseWave.musterBestArrived).
Mechanisms verified on Mazury seed 3 both sides (rally on corridor, forward inn 5
tiles from the staging flag). But 100-paired-seed benchmarks collapsed everywhere:
Mazury 24.5 / Muka 47.0 / SmallForTwo 71.5 (controls 50.0 / 65.0 / 95.5) — and a
baseline run showed `5740f2a9` ITSELF already at Mazury 27.5, so the committed,
never-benchmarked arrived-not-bound muster gate is the prime suspect (it applies on
every map; Mazury is not amphibious, so it is 5740f2a9's only live change there);
the forward-rally layer is within noise on Mazury on top of it. The delay-the-first-
strike law strikes again. NOTHING committed; the diff sits in the working tree.
Artifacts in `.tmp/fwdrally/` (mechanism logs, benchmark games, built 5740f2a9
worktree). Next: instrument per-wave muster/staging WAIT budget on losing Mazury
seeds before touching any knob — see `.tmp/HANDOFF-muster-wait.md` (includes the
request/wait/disband knob refresher). Sequencing: engine logic-divergence bug fixes
(cpp-bugs) land first; re-baseline after.

---

## 3. ML pilot (effort B)

Plans/contracts: `docs/AI/cortex/{PILOT,ML_CONTRACT,DECIDE_PILOT,DECIDE_CONTRACT}.md`
(repo-root docs/, NOT git-tracked). teams==2 benchmark maps: Dejans, Mazury, Muka,
SmallForTwo, balanced_for_2, strange2 (matchup size must equal the map's team count).

### Worker-cap pilot (swarm `tuneWorkers`)

Learned per-swarm worker cap behind `GLOB2_CORTEX_POLICY=ml` + `GLOB2_CORTEX_NET=<abs
blob>`. Determinism via I16F16 integer inference (argmax, no softmax) in
`CortexNet.{h,cpp}`; trainers in `tools/cortex-ml/` (numpy only), fixed-point check in
`tools/cortex-ml-infer/`. BC reached 99.8% parity vs the hand rule; game-level parity
within noise. Offline RL: AWR matched the hand rule, CQL mixed. Artifacts in
`glob2/.tmp/` (`cortex_awr.*`, `cortex_cql.*`), NOT committed.

### Decision-net pilot (learn the `decide()` utility scores) — 2026-06-07

Higher-leverage seam: learn the decide() score MAGNITUDES only; action construction +
decline gates stay hand-coded (decline = hard eligibility mask). 48 feats
(`extractDecideFeatures`, SSOT) → 64→64→18 masked argmax, behind
`GLOB2_CORTEX_POLICY=ml-decide` + `GLOB2_CORTEX_DECISION_NET=<abs blob>`. BC clones the
hand rule everywhere (98.5% on multi-eligible rows; broad parity matrix ±2.5pp,
aggregate 76.4% == baseline). AWR pass 1: safe but FLAT — bit-parity gate caught a real
I16F16 overflow (|W|≈43k; fixed by freezing layer0 at BC fold scale + weight decay),
but Muka-vs-Nicowar was UNMOVED.

**Resolved: the Muka weakness was NOT decision-selection — the AWR null was correct.**
It was a production-mix/gate bug RL's selection seam cannot reach (see feeding
governor / worker tiers). **ML re-enters LATER only as the learned blitz-trigger
threshold** (the `foodSaturated` level + `BLITZ_MIN_WARRIORS` become the seam) — NOT
for the governor/blitz mechanism itself. The two ML seams (`ml`, `ml-decide`) are
independent env values, mutually exclusive for now.
