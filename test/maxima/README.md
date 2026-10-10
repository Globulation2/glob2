# Maxima tests

Maxima's C++ and Python test sources live beside the implementation in
`src/ai/maxima/`; this page is their verification entrypoint. The C++
suites are doctest cases in the shared test binaries (`Maxima.*` suites; the
standalone policy checks in `glob2-unit-tests`, the engine integrations in
`glob2-engine-tests`); the Python tests are plain `unittest` files:

```sh
scons release=1 server=0 tests maxima-strategy-dump
python3 test/run_tests.py --filter 'Maxima.*'
MAXIMA_STRATEGY_DUMP=build/darwin/client/release/test/MaximaStrategyDump \
  GLOB2_BUILD_DIR=build/darwin/client/release python3 src/ai/maxima/MaximaStrategyConfigTest.py
python3 -m unittest discover -s src/ai/maxima -p '*Test.py'
```

`--filter 'Maxima.Combat/*'` and friends select one suite. No external services
are required.

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

Run focused shared-binary cases for food accounting, continuation and relocation:

```sh
scons release=1 server=0 tests
python3 test/run_tests.py --filter 'Maxima.FoodLedger/*' --filter 'Maxima.Continuation/*'   # glob2-unit-tests
python3 test/run_tests.py --filter 'Maxima.Relocation/*'                                    # glob2-engine-tests
```

The food-ledger suite checks capped-query ordering, wrapped reach and scratch
buffer reuse; its `timing benchmark [benchmark]` case (run with `--tag benchmark`)
prints CPU timings and deterministic result digests at several map sizes. Timing
is informational. The farming correctness case also checks its existing 100 ms
CPU budget for a 512×512 fertility rebuild and prints both CPU and elapsed time.
The limit uses process CPU time (GetProcessTimes on Windows, whose CRT clock()
reports elapsed time) so unrelated builds and runner scheduling do not
turn elapsed-time contention into an algorithm regression. The relocation suite checks pending deletions, capacity
protection, failed replacements and saved handovers against real buildings. The
continuation suite checks binary and text archives, signed limits, nested records
and buffered writes.

See [Maxima](../../docs/ai/maxima/README.md) for current strategy behaviour and
[engine tests](../README.md) for shared save, replay and simulation harnesses.
