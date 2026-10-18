# PR 1194 building refresh queue fix

Tested commit: `3fb6d1a934268107c20c641ede2eb2e765dc16f3`, directly extending PR head `9f45cb9d8b0e49ef03a1b3026c1166b70812920a`. Integrated base remains `987c953698bb3f0bfd1078184773075ba3313091`. Current master `64c792a61c4b5a7cdbfc49f33e7c846127645056` was fetched and inspected: the building scheduler, gradient runtime header, serializer and building invalidation harness have no intervening changes from the integrated base. This is PR-head verification, not merge-result verification against current master.

The fix restores the historical 64-request synchronous overflow fallback. A new engine regression checks no overflow at64, deduplication, oldest-first service at65, and preserved FIFO/reservation flags after binary/text save round trips, with0 and2 workers. The same test fails against the unfixed production source (retained `before.log`/`before.xml`) and passes against the fix (`after.log`/`after.xml` and final suite). Bounding normal queue creation also avoids exceeding the unchanged16-bit saved count. No save encoding changes. SIM_REVISION70 follows current master's69; the multiplayer record is regenerated, and its checksum trace is byte-identical.

Environment: macOS26.6.2 ARM64, Apple Clang21.0.0, Python3.14.7. Existing repository development dependency prefixes; full compiler/dependency paths are in build logs. `dev_fast=1`, `-O0 -g1`, default linker, no PCH/unity, no runtime dependency changes. Binary SHA256 values and clean-source provenance are in the archive.

## Results

- Native engine/unit builds: exit0. Final clean-source build log: `build-final.log`.
- Regression before fix: expected exit1, queue65 and missing overflow; after fix: exit0,1 case passed.
- Final engine suite: exit0,98 passed,0 failed,1 display-only market-panel skip. Includes overflow/save continuity, existing building lifecycle/deadlines, hiring, delayed publication and participant-count equality, historical saves, market/guard goldens, replay/network/version boundaries and committed match verification.
- Final unit suite: exit0,34 doctest cases passed (the runner reports4 invocation groups).
- Sim contract Python unit suite: exit0,9 passed. Sim-revision check against inspected master and whitespace checks: exit0.
- Golden regeneration: exit0,1 case passed. Ordinary final engine run verifies the regenerated record.

Initial build without `PYTHONPATH=.` failed during asset packaging on a Python multiprocessing import (`tools`); `build.log` retains the failure. `build-retry.log` is the successful retry. No production change was made for that environment issue.

## Exact commands

### build

```sh
PYTHONPATH=. python3 tools/dev_build.py engine-tests unit-tests
```

### regression

```sh
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'BuildingGradientInvalidation/building refresh overflow*' --no-display --timeout 120 --artifacts artifacts/pr1194-queue-fix/after --junit artifacts/pr1194-queue-fix/after.xml
```

### golden

```sh
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'TurnEngineHarness/the committed*' --update-fixtures --no-display --timeout 600 --artifacts artifacts/pr1194-queue-fix/golden --junit artifacts/pr1194-queue-fix/golden.xml
```

### engine

```sh
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary engine --filter 'BuildingGradientInvalidation/*' --filter 'DelayedGradientPublication/*' --filter 'HiringPlanner/*' --filter 'HiringPipeline/*' --filter 'TurnEngineHarness/the committed*' --filter '*Replay*/*' --filter '*Version*/*' --filter 'LegacyRoundTripSave/*' --filter 'MarketsV2/*' --filter 'GuardAreaBalance/*' --exclude-tag benchmark --no-display --timeout 600 --artifacts artifacts/pr1194-queue-fix/final --junit artifacts/pr1194-queue-fix/final.xml
```

### unit

```sh
python3 test/run_tests.py --build-dir build/darwin/client/debug/dev-dev_fast-true-linker-auto --binary unit --filter 'GradientPipeline/*' --filter '*Replay*/*' --filter 'SimVersion/*' --no-display --timeout 180 --artifacts artifacts/pr1194-queue-fix/final-unit --junit artifacts/pr1194-queue-fix/final-unit.xml
```

### contract-unit

```sh
python3 -m unittest discover -s test -p test_check_sim_revision.py -v
```

### contract

```sh
python3 test/check_sim_revision.py --base origin/master
```

### whitespace

```sh
git diff HEAD^ HEAD --check
```

## Coverage limits

No Linux/Windows/browser/mobile execution for this fix, no cross-OS checksum claim, no full native suite, release performance or maintainer play review. Serial/threaded consistency and binary/text continuation were exercised on macOS. Broader platform evidence from earlier PR revisions is not asserted to verify this new revision. Current-master integration remains a separate check; the PR stays draft. No maintainer acceptance is claimed.

The archive contains build logs (including superseded attempts), final and negative-control JUnit, clean-source/binary provenance, exact commands, plus saves/checksum traces emitted by the selected artifact tests. Evidence is separate from the source branch.
