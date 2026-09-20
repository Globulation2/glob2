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
| Farm protection, growth and clearing | `MaximaFarmingStandaloneTest`, `MaximaFarmingIntegrationTest` |
| Placement and wrapped routes | `MaximaPlacementStandaloneTest` |
| Defence, scouting and force inference | `MaximaDefenseStandaloneTest`, `MaximaReconStandaloneTest`, `MaximaForceModelStandaloneTest` |
| Rally arrival, recruitment and attack waves | `MaximaTacticsStandaloneTest`, `MaximaCombatIntegrationTest` |
| Runtime orders, building lifetimes and continuation | `MaximaImplementationIntegrationTest`, `MaximaLifecycleTest`, `MaximaDiagnosticsTest` |

The top-level build also exposes dedicated harnesses:

```sh
scons release=1 server=0 maxima-food-ledger-test maxima-relocation-test maxima-continuation-test
build/src/MaximaFoodLedgerStandaloneTest
build/src/MaximaRelocationIntegrationTest
build/src/MaximaContinuationTest
```

The food-ledger harness accepts `--benchmark` for CPU timings and deterministic
result digests at several map sizes. Timing is informational. The relocation
harness checks pending deletions, capacity protection, failed replacements and
saved handovers against real buildings. The continuation harness checks binary
and text archives, signed limits, nested records and buffered writes.

See [Maxima](../../docs/maxima/README.md) for current strategy behaviour and
[engine tests](../README.md) for shared save, replay and simulation harnesses.
