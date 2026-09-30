# Maxima tests

Maxima's policy, configuration and engine integration tests live here. Build the
game from the repository root, then run the suite against its engine objects:

```sh
scons --build=build release=1 server=0 -j4 build/src/glob2
python3 test/maxima/run_maxima_implementation_regressions.py --build-dir build
```

Use repeatable `--test NAME` arguments to select suites. The runner builds
temporary test binaries and requires no external services.

| Coverage | Suites |
| --- | --- |
| Configuration resolution, bounds and defaults | `MaximaStrategyTest`, `MaximaStrategyConfigTest`, `MaximaStrategyPolicyTest` |
| Director budgets, production and labour | `MaximaDirectorRegressionTest`, `MaximaEconomyRegressionTest`, `MaximaLabourStandaloneTest` |
| Stock-band staffing | `MaximaStaffingControlStandaloneTest` |
| Food claims, bounds and relocation | `MaximaFoodLedgerStandaloneTest`, `MaximaRelocationIntegrationTest` |
| Permanent farm seeds, harvest lanes, wood reserves and clearing | `MaximaFarmingStandaloneTest`, `MaximaFarmingIntegrationTest` |
| Placement and wrapped routes | `MaximaPlacementStandaloneTest` |
| Defence, scouting and force inference | `MaximaDefenseStandaloneTest`, `MaximaReconStandaloneTest`, `MaximaForceModelStandaloneTest` |
| Rally arrival, recruitment and attack waves | `MaximaTacticsStandaloneTest`, `MaximaCombatIntegrationTest` |
| Runtime orders, building lifetimes and continuation | `MaximaImplementationIntegrationTest`, `MaximaLifecycleTest`, `MaximaDiagnosticsTest` |

Three of these suites are doctest cases in the shared test binaries:

```sh
scons release=1 server=0 tests
python3 test/run_tests.py --filter 'Maxima.FoodLedger/*' --filter 'Maxima.Continuation/*'   # glob2-unit-tests
python3 test/run_tests.py --filter 'Maxima.Relocation/*'                                    # glob2-engine-tests
```

The food-ledger suite checks capped-query ordering, wrapped reach and scratch
buffer reuse; its `timing benchmark [benchmark]` case (run with `--tag benchmark`)
prints CPU timings and deterministic result digests at several map sizes. Timing
is informational. The relocation suite checks pending deletions, capacity
protection, failed replacements and saved handovers against real buildings. The
continuation suite checks binary and text archives, signed limits, nested records
and buffered writes.

See [Maxima](../../docs/ai/maxima/README.md) for current strategy behaviour and
[engine tests](../README.md) for shared save, replay and simulation harnesses.
