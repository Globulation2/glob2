# Settlement access and defended farm gates

The policy is **two maintained exits per local settlement perimeter, with every
building connected to the exit network**. A coastline fragment is not a useful
unit of ownership: several fragments can surround one settlement, while two
distant colonies can sit on the same landmass. Giving every fragment a pair
creates too many breaches; choosing one global pair abandons other settlements.

This change is confined to Maxima. It uses the game's existing movement,
forbidden-area, clearing-flag, tower and guard mechanics. It does not change crop
growth, weapon range, enemy pathfinding or the save format.

## Why Garden 3 sealed itself

`Garden_3_-_sealed_in.game` starts at game tick 144720. Maxima is player 4,
team 0, with a starting position at (33,39). The old global selector chose gates
near (18,27) and (8,45), away from the main settlement. Its emergency test examined
those two routes, so they could appear open while the main base remained sealed.

An independent ground-movement flood found that releasing just the empty
forbidden cell at (42,56) connected the main interior to the beach. Thus the
failure was not simply insufficient harvesting: forbidden growth-frontier cells
also obstruct friendly movement. Existing passive coastal repair explicitly
excluded the starting grass landmass and did not provide maintained clearing.

Three further problems made the original design fragile:

- Channels stopped after seven steps, sometimes inside the farm they crossed.
- Resource-cost ranking could move the gates as wood grew. Clearing yesterday's
  route did not establish a durable passage for tomorrow.
- Defensive placement rewarded proximity to generic protected terrain, not
  coverage of the actual gate. It could also place a building in the channel.

The unrelated enemy-base digout campaign is not a substitute for this local
access policy. A distant clearing flag cannot repair an unreserved home exit.

## Spatial contracts and their order

1. Build potential grass connectivity with buildings and permanent resources
   blocked. Renewable crops are costs to clear, not permanent topology.
2. Label connected economic cores within half the economic envelope, then grow
   their labels over potential grass. Assign coastal candidates to those cores.
   This groups fragmented coasts without joining colonies across water or stone.
3. Choose two nonoverlapping, connected three-cell mouths for each viable
   settlement perimeter. Close all other wall candidates while validating each
   mouth, so one gate cannot depend on the other gate being open.
4. Descend to a settlement building entrance and reserve a complete channel plus
   a real clearable beach outlet. Widen the inland route where terrain permits.
   Only the first interior steps are defensive staging points; the remainder
   exists to complete access, regardless of its length.
5. Apply the normal farm pattern and passive porosity, then audit every completed
   physical building against beach connectivity. Connect isolated buildings to
   the existing gate network using inland branches, sharing previous branches.
6. Reconcile forbidden/clearing areas and give the same route masks to building
   placement and defensive siting. Buildings cannot occupy or upgrade into those
   routes. Worker circulation may share them.

The building repair minimizes resource count before distance, with a larger
penalty for protected wheat. An empty forbidden frontier is preferable to a crop
cut. Explicit necessary access contracts can override a fixed seed; ordinary
farm evolution still cannot revoke one. A branch may not quietly open a third
coastal breach. If no legal connection exists, `unresolved_access` records the
failure instead of reporting an open route.

Geometry has a separate cache identity from resource state. Terrain, physical
building topology and economic scope can require replanning; routine regrowth
requires maintenance. Military staging areas consume gate geometry and are not
hashed back into it, avoiding a gate-to-guard-to-gate feedback loop. The maps,
routes, coverage scores and signatures are derived state rebuilt after loading.

## What makes a gate defensible

Initial gate selection first limits resource destruction. Among equally costly
choices, prefer mouths with legal tower positions covering the entire mouth
and interior approach; prefer a shared position when it covers both gates.
Separation is a later tie-breaker. Maximum separation alone often places the
two entrances beyond a single defensive position's reach.

Coverage uses the actual tower range from the building data and the square
range around its 2x2 footprint used by `Building::shoot`. Pads must be empty,
legal grass and off every reserved channel. The placement score rewards coverage
of still-uncovered mouths; existing towers suppress redundant coverage rewards.
This is a tower-specific field rather than a generic building safety bonus.

A limited additional tower request requires a mature workforce, local threat
and healthy food situation. An earlier live experiment requested towers when a
small colony merely saw a distant army; this diverted labor and performed badly
on Isles. The final rule reuses the existing maturity/threat thresholds and caps
this demand at the active tower target plus one. Normal emergency demands still
apply. Existing preemptive guard budgets may station warriors on the inner lane.

This does **not** guarantee that enemies use a gate. Forbidden areas are private
movement restrictions; only physically grown crops block enemy warriors. Bare
coastline, an incomplete farm wall, unmaintained openings, flying units and
amphibious approaches can bypass it. Tower geometry also does not prove that a
tower will be built, supplied or win an engagement. The tests below deliberately
keep those claims separate.

## Clearing and regrowth

Both exits blocked in any settlement is an emergency, even during food recovery.
Settlement order prioritizes the largest economic core. Blocked internal access
branches follow, before routine single-gate maintenance. Normal single-gate cuts
retain the existing economy checks, including the stricter checks for wheat.

The existing directed clearing flag advances through worker-reachable blockers.
It starts unstaffed, installs its resource selector, and only then takes workers.
The executor explicitly distinguishes a mouth from an already-complete internal
branch. Otherwise a long branch could be mistaken for one enormous mouth, or its
first tile could accidentally resolve to a different gate route. A native
regression checks that a 19-cell branch, including a gate-key collision, produces
only a local group of actual blockers.
An escape emergency can use up to three workers, bounded by workforce size and
never below the configured crew. One worker lost the race against fertile wood
in the saved position. The selected route remains reserved after the last crop
is cut, so regrowth schedules another repair instead of moving the exit.

The planner cannot promise instantaneous clearance or continuous movement on
every tick. Workers still need to reach and harvest the blockers. Reported
`blocked_gates` and `blocked_access` describe obstructions in maintained routes;
`unresolved_access` means no permitted repair was found. A blocked route is not
evidence that all buildings are currently trapped: an alternate route may work.

## Validation and how to reproduce it

`MaximaBarrierScenarioTest` loads eight real maps and all 36 starting positions:
Holiday Island 2, Archipelago, Isles, Migration, Garden 3, A big pond, Wild River
and Sand River. It exercises the runtime planner, not a duplicate gate selector.
Its independent eight-neighbor toroidal flood checks all 85 completed buildings.

It reports three distinct access measurements:

- `engine_trapped`: actual saved engine terrain/resource/forbidden state, using
  `Map::isHardSpaceForGroundUnit`; transient unit occupancy is intentionally ignored.
- `trapped_after`: desired policy after hypothetically clearing only promised routes.
- `trapped_regrowth`: the same contract after injecting wood into empty route cells
  and running another policy pass. Candidate runs assert zero trapping and no relocation.

The test also funds a tower intent through the real placement planner, verifies
full-mouth coverage and forbids route overlap, including after placement
revalidation. This isolates siting quality from affordability. A separate mature
barrier experiment physically closes the proposed coastal wall and then closes
its gates, ignoring friendly forbidden masks. It counts buildings for which the
gate is necessary, exposing natural bypasses rather than pretending they vanish.

Build the game normally, then run:

```sh
python3 test/run_maxima_implementation_regressions.py --build-dir build \
  --test MaximaBarrierScenarioTest --test MaximaFarmingIntegrationTest \
  --test MaximaCombatIntegrationTest --test MaximaDirectorRegressionTest \
  --test MaximaPlacementStandaloneTest --test MaximaFarmingStandaloneTest
python3 test/MaximaFarmingPolicyTest.py
```

The native scenario main also accepts `<map-or-save> <team> [save]` when built as
a standalone binary. Defining `BARRIER_BASELINE` removes candidate-only field
references and allows recording failures of the old implementation. Assertions
must remain enabled for candidate regression runs.

The paired live runner accepts two previously built executables:

```sh
python3 tools/run_maxima_barrier_comparison.py \
  --baseline /path/to/old/glob2 --candidate /path/to/new/glob2 \
  --output-dir tournament-results/barrier-comparison --steps 40000 --jobs 2
```

It runs Maxima against Nicowar on the first and last position of each map, using
seeds 42 and 74241 respectively: 16 pairs, 32 matches. It records binary hashes,
logs and surviving checkpoints. The runner snapshots strategy files and passes absolute paths for the full base,
the duel layer and each map. This matters on macOS: the executable changes its
own working directory, so setting the subprocess directory alone does not
isolate it from source-tree strategy edits. The manifest records input hashes
and commands as well as binary hashes.
Candidate player zero can belong to a nonzero map team; observer filtering uses
the actual team from the result record.

The September 5 comparison used an isolated common source/data snapshot so
concurrent colonization work would not contaminate the before/after experiment.
The farming changes were then merged with that work and checked again in the
shared workspace. The detailed numerical results, checkpoint audit and known
baseline test failures are recorded in [the validation report](MaximaBarrierValidation.md).

## Maintenance notes

- Keep the gate count constant local to a settlement. Do not restore a global
  count assertion or allocate two gates to every disconnected shoreline fragment.
- Do not shorten access routes to weapon range. Access and firing lanes have
  different purposes and different lengths.
- Do not feed resource amounts into the geometry cache. Refresh clearing and
  tower feasibility as crops change, while preserving standing route obligations.
- Keep placement signatures aware of corridor/coverage changes. A previously
  selected parcel must be rejected if it now overlaps a maintained route.
- Do not count a hypothetical cut, a forbidden-area release, a feasible tower
  pad or a funded placement experiment as completed construction or a combat win.
- Small or structurally obstructed coastlines may lack two valid mouths. Those
  cases need explicit failure telemetry; water/stone cannot be harvested away.

The implementation comments around settlement labeling, closed-wall routing,
route retention, branch repair, clearing priority and tower scoring explain the
reason for each constraint. The native map suite is the behavioral contract;
Python source checks only protect important module boundaries and switches.
