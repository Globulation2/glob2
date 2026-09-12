# Farming stability validation — September 5, 2026

## What failed

Holiday Island 2's farm west of the starting base began with 59 wheat tiles.
In a fresh four-player game (seed 42), the old access repair exposed both final
wheat tiles at AI tick 22,579. Classification protected both; the final repaired
plan protected neither. The patch then disappeared. Live seed (35,85) also
repeatedly changed protection in another fresh run although its classification
continued to request protection.

## Change and invariants

- The starting base's continuous grass region is identified from terrain alone.
  Ordinary mainland farms do not enter island access repair. Sand separates
  growing regions even though workers can traverse it.
- Coastal growth keeps its fertility exemption, but protection stays on the
  expansion lattice, so a shoreline run is porous by construction.
- Exceptional island repair cannot cut fixed odd/odd seeds. Its tiny-patch
  fallback cannot sacrifice singleton regrowth seeds.
- Explicit building and circulation contracts may override protection;
  ordinary growth/harvesting may not revoke the fixed seed reserve.
- A seed-only patch awaiting regrowth is distinct from inaccessible harvestable
  wheat. Protecting those seeds is intentional.

## Automated checks

The farming integration test exercises 12 adversarial harvest/regrowth rounds
on each of eight maps. Every available wheat tile is removed between rounds,
then adjacent growth changes farm boundaries. Fixed protected seeds must survive
all rounds. Additional cases cover discovery invariance, wrapping, inland sand,
coastal access and repeated Archipelago growth/harvest changes.
The farming, economy and director native suites and 25 farming policy checks pass.

## Live game matrix

Normal release binary, two players (Maxima versus Nicowar), position offset zero,
seeds 42 and 74241, up to 40,000 engine ticks per match. Matches may end before
the limit and the candidate may die; this is an invariant test, not a win-rate
or economic-balance evaluation. The runtime checks visible live fixed seeds for
unexplained protection revocation on every applied policy pass and emits a
violation event independently of its sampled diagnostic cadence.

| Map | Runs | Policy samples | Seed violation events |
|---|---:|---:|---:|
| Holiday_Island_2 | 2 | 151 | 0 |
| Archipelago | 2 | 42 | 0 |
| Isles | 2 | 334 | 0 |
| Migration | 2 | 310 | 0 |
| Garden_3 | 2 | 363 | 0 |
| A_big_pond | 2 | 182 | 0 |
| Wild_River | 2 | 213 | 0 |
| Sand_River | 2 | 264 | 0 |

All 16 runs completed successfully: 1,859 policy samples, zero seed violation
events. Temporary full-map diagnostic builds independently audited 2,032 passes
with zero unexplained fixed-seed revocations.

The exact four-player Holiday seed-42 reproduction was also repeated: minimum
wheat during observed policy passes was 45, versus zero before; the last observed
patch had 57 wheat tiles. Both previously affected Archipelago patches retained
their reserves in saved-game and fresh-game traces. Periods containing only
protected seeds are expected; they must not trigger destructive access repair.

## Repeating the checks

```sh
python3 test/run_maxima_implementation_regressions.py --test MaximaFarmingIntegrationTest --test MaximaEconomyRegressionTest --test MaximaDirectorRegressionTest
python3 test/MaximaFarmingPolicyTest.py
python3 tools/run_maxima_farm_stability.py --output-dir /tmp/farm-stability-results --jobs 3
```

This evidence covers the tested scenarios, not every possible map or game state.
Growth-frontier area updates still occur; the invariant is stable live seed
protection rather than a prohibition on all area changes. The tests should be
extended with new saves whenever another temporal failure is reported.

## Playground coastal fertility dip

An instrumented replay of `Playground.game` identified coastal cells (94,9)
and (95,9), fertility 3224 and 3140, rejected by the 3276 cutoff despite
being valid growing grass adjacent to wheat. Coastal wheat now bypasses that
cutoff both before and after growth. A 12,000-step continuation confirmed
both cells became forbidden, grew wheat, and retained protection.

The focused farming, economy, and director suites and 25 policy checks pass.
Sixteen fresh matches across the eight-map matrix reported zero seed stability
violations. The broader implementation suite still fails its swarm readiness
assertion at line 828; running the previous farming policy reproduces that
same failure. It is not included among the passing suites.

## Configurable management radius

With the default 20-tile radius, replaying the digout autosave changes the
clearing flag at (75,108) from 7 protected wheat tiles in its radius to 0
(of 11 live wheat tiles). This verifies removal of the farming conflict;
it does not by itself prove the entire enemy-base digout completes.
The integration regression checks the inclusive 20-tile boundary, exclusion
beyond it, exclusion of virtual anchors, established seed retention after
anchor loss, and zero-radius unlimited behavior.
