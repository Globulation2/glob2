# Worker status traversal race repair

Tested c456e2cf44dbb65981bb965c3a7c3d2047ad8378, fetched base f5d1bc51ef61795d4cc165e9ac530f5d2ad21a42 unchanged before final verification. Ubuntu26.04.1 x86_64; Python3.14.4 and isolated uv CPython3.10.21 (same Python minor as GCC11 Ubuntu22.04 job). Standard-library dependencies only.

Original failure: https://github.com/Globulation2/glob2/actions/runs/37408019668/job/112096398496 — test_slow_collection_does_not_starve_execution_or_control, status RPC failed with FileNotFoundError for attempts/.../packing-*. Execution and control must remain available while packing removes staging.

Exact commands from repository root:
- python3 test/test_tournament_pipeline.py:13 PASS
- python3 test/test_tournaments.py:41 PASS
- uv run --python 3.10 --no-project python test/test_tournament_pipeline.py:13 PASS
- uv run --python 3.10 --no-project python test/test_tournaments.py:41 PASS
- uv run --python 3.10 --no-project python artifacts/worker-usage-repair/baseline-reproduction.py:PASS (asserts original committed traversal raises FileNotFoundError when a discovered directory disappears).
- git diff --check:PASS

Original reproduction extracts the unchanged usage function from the base. Controlled scandir fault represents the CI removal race; on Python3.10 pathlib caches its scandir accessor, so reproduction patches that accessor. An initial os.scandir-only patch did not intercept the original cached accessor and was not a valid reproduction; corrected script/log attached. Final test directly forces os.walk to encounter a disappeared child and verifies it still counts the live sibling, excluding symlink bytes. Permission-error case verifies non-disappearance errors surface. Pipeline tests exercise real packing, concurrent collection and control; no extended deadlines/retries/removed assertions.

Worker filesystem accounting only; no simulation/save/replay/game-wire protocol changes. Package identity follows the normal source-bundle machinery tested by the tournament suite. No full native/platform/browser matrix locally; corresponding game inputs are unchanged. One CI failure plus controlled reproduction establish the race, not an estimated frequency.
