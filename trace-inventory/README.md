# Full browser trace and catalog inventory repair

Candidate `f0f615ea3` against fetched master `43b153739838207983c922ab4cbeed7773459de9`. Ubuntu 24.04 x86_64, Python 3.12 with unchanged pinned requirements-dev environment. No runtime/production code or dependency changes.

The exact full hosted run [38916805749](https://github.com/Globulation2/glob2/actions/runs/38916805749) rejects WebKit threaded/auto because the comparison expects only serial/1 and threaded/1,2,4. Baseline local reproduction using the downloaded actual hosted artifacts fails identically (`local-before.log`). Fixing the identities reveals Firefox/WebKit's obsolete runtime-resource grep omits the runtime-catalog composition contracts; the intermediate failure is retained (`local-after.log`).

The repair requires all six existing producer identities and adds all eight serial/threaded catalog registry/composition tests to each Firefox/WebKit WSS/simulation job. Actual Playwright --list before/after inventories show 10 -> 18 cases per browser, zero removals. Negative contracts reject missing or hash-mismatched 8/auto traces and duplicates; a contract checks producer/consumer inventory equality and both browser filters.

All 16 added real-browser cases pass (58.4 seconds total) with the existing clean 43b release binaries: Emscripten 4.0.15, Playwright 1.63.0, pinned Node 24.19.0, headed Firefox/Xvfb and WebKit. Their embedded producer identities are checked against clean 43b; no source or build mutation of the parallel full Chromium run. Those four missing browser/variant resource and unit producers supplement the original hosted inputs in ci-traces/browser-determinism-wasm-local-catalog. Every other producer is the original downloaded CI artifact. The candidate comparison then passes all 22 per-tick traces, 10 resource and 10 unit composition traces, plus ten 73-artifact numeric/data/complete-trace/decoded-save corpus comparisons (`local-after-complete.log`). Raw inputs, manifest identities, logs and exact orchestration scripts are included.

Final candidate commands:

- pinned Python -m unittest discover -s test/build_system -p 'test_*.py': 568 tests, OK, two optional skips (76.375 seconds).
- pinned Python tools/check_docs.py: 309 docs, zero errors.
- python3 .github/scripts/ci_compare_evidence.py --traces <ci-traces> --scripting <ci-scripting> --compatibility true --variants true --macos true: exit 0.
- python3 run-catalog-producers.py: 16 passed, zero failures/skips/retries. The script records exact Playwright arguments and environment; source/runtime producers remain 43b because this candidate changes only CI selection/comparison and docs.
- git diff --check: pass. Master was fetched before final verification and is still 43b.

The initial 567-contract result predates the extra browser-filter regression; the final 568-contract result covers the committed candidate. An initial focused regression exposed mixed int/string sorting in the missing-trace error; that error path is corrected and the final suite passes. Full current-policy hosted matrix on resulting master remains pending; no hosted recovery claim is made here. No reduced tiers or performance savings are enabled or claimed. Existing unrelated optional skips and store/release packaging remain outside this focused repair; full native/browser runtime validation is retained in the adjacent merged-master-43b checkpoint.
