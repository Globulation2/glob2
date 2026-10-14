# Service-restoration fix verification

Tested source: `589a8408aa14a17ff27c3f711a42fa5b8d43c3e7`. Base: `441870ef12bcc96337e65f1fbfe036a13d5eefb1`. PR head built directly on that base; master was fetched again before final validation and was unchanged.

macOS26.6.2 ARM64, Apple Clang21.0.0 (clang-2100.3.34.2), fast development `-O0 -g1`, PCH/unity disabled. Build commands use `PYTHONPATH=$PWD`. Pinned dependency metadata, linked libraries, compiler versions, binary hashes and exact commands are in the archive and JSON records.

Final results:

- 167 engine/client cases passed; one unrelated display-only measurement screenshot case skipped.
- 20 relay cases passed, including rejection of mismatched simulation versions.
- Both golden fixture checks passed; simulation-revision contract passed; its nine Python unit tests also passed.
- The exact new match record verified with one and four workers: tick841, no rejected orders, identical complete traces and result hash. Both traces equal the committed golden.
- Twelve saved-continuation traces match across workers1/2/4/8 and diagnostics off/waits/trace.
- New tests cover entry occupancy, canceled interior timers/effects, retained and zero-cost reservations, blocked exits, and save formats159/160/161. Generated saves and continuation traces are included.

See `final-results.json` for commands/exit codes, `summary.json` for hashes and limits, `trace-comparison.log` for comparisons, and `fixture-audit.txt` for the distinction between version-only resource checksum changes and a fresh match recording with regenerated orders.

`evidence.tar.gz` contains all supporting logs, JUnit, inventory, environment/dependency metadata, synthetic saves, replays and traces. Extract it into `artifacts/service-restoration/` in a checkout of the tested commit. Run `python3 artifacts/service-restoration/check_evidence.py` to check recorded comparisons; use the command records to reproduce tests.

Historical evidence is labeled separately: `baseline-review/` contains the original two failing reproductions. The unprefixed `focused.log` records two subsequently corrected test-fixture crashes (an overloaded deletion call deleted the inn); `prepare-blocked-regressions.log` shows their fix and `final-focused.log` is the successful committed-revision run.

Limits: no cross-platform determinism result, release/package qualification, hosted matrix, or manual playtest. The cached Linux image lacks Python/SCons/SDL build metadata (probe included); Windows/browser/mobile runs were not performed. Maintainer acceptance is pending. This evidence does not claim gameplay completion of the synthetic match (its team outcomes remain unresolved).
