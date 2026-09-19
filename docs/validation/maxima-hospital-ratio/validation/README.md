# Validation commands

Run from the production checkout. Logs are retained here as gzip files.

```sh
scons -j4 release=1 server=0
python3 test/run_maxima_implementation_regressions.py --build-dir build \
  --reuse-built-objects --test MaximaImplementationIntegrationTest \
  --test MaximaDirectorRegressionTest --test MaximaLabourStandaloneTest \
  --test MaximaStrategyTest --test MaximaLifecycleTest
python3 -m unittest discover -s test -p 'Maxima*Test.py'
GLOB2_TEST_MAX_TICKS=512 GLOB2_USER_DIR=artifacts/hospital-config-profile \
  python3 test/run_maxima_implementation_regressions.py --build-dir build \
  --reuse-built-objects --test MaximaStrategyConfigTest
git diff --check
```

The final selected-0.6 logs are `hospital-selected-*.log.gz`; all five native
suites, 14 configuration tests, and the broader Python discovery pass (the
same two optional skips). The earlier provisional-0.5 native log is
`hospital-tests-final.log.gz`. The subsequent strategy-only
log covers the additional malformed legacy-value assertions.
`hospital-all-python-tests.log.gz` records 62 tests, two skips. One skip requires
optimized tournament objects for an `nm` inspection; the other is the
configuration test class, whose separate runner above passes all 14 tests.
The 512-tick cap applies only to configuration startup tests. Ablation games use
60,000 ticks.

The main report separately records cross-platform checksum and save/resume
results, including the failed late-game continuation check. These regression
passes do not override that limitation.
