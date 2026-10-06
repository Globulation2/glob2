# Runtime resources validation evidence

Implementation PR: https://github.com/Globulation2/glob2/pull/846
Tested final source: `524844f49ec8cf283a015cb687bf11286f5c4ed5`.
This separate branch contains generated review evidence only. Do not merge it into the implementation branch.

## Current results and limits

Native coverage combines the broad `f45a09238` run (1,876 cases: 1,728 passed, 144 skipped, three failed cases and one errored case) with affected-case reruns. The four broad failures were diagnosed and passed after test-oracle, editor-fixture and API-snapshot corrections. The `617071ba4` targeted run passed 52 cases and retained the one editor-context error subsequently fixed at final HEAD. The final JavaScript run passed two cases; an additional final display run passed four. These are a union of runs, not one all-green final full-suite execution. XMLs and failure logs are preserved alongside reruns. The only production change after f45 was editor-owned material visibility; 524 changes only fixtures. Frozen 617 and 524 CLI binaries are byte-identical.

Browser validation passed 33 cases across Chromium, Firefox and WebKit. Twelve serial/threaded 1,500-tick traces, three match-verification traces and six mixed-resource traces match native exactly. The browser archive includes comparison reports, per-browser artifacts and provenance. Emscripten 4.0.15 and Playwright 1.63.0 were used.

Native screenshots cover editor resource selection, fabric construction cost, gold stock, mixed fallback artwork, shared market stock, compact settings, and statistics. Automated display checks do not substitute for maintainer gameplay review.

**Pending:** Windows/Android execution comparisons, paired gameplay tournaments and idle-machine performance gates. No performance or gameplay-equivalence claim is made. macOS/iOS and ARM64 execution are not verified. No merge/ready-for-review claim is made.

## Reproduction

See `environment.json` for exact source/base, compiler, build flags and local dependency prefixes. Local absolute paths in the records identify the original run; substitute the equivalent checkout/dependency locations when reproducing. Use the same dependency revisions and patched SDL runtime described in the build logs. Test inputs and committed golden records are available from the tested source revision.

Broad native run at f45:

```sh
python3 test/run_tests.py --binary all --no-display --exclude-tag benchmark \
  --junit artifacts/resource-refactor/final-f45a09238-all.xml \
  --write-inventory artifacts/resource-refactor/final-f45a09238-inventory.json -j4
```

Affected reruns at 617 covered all FarmAreas, RuntimeResources and EditorActionCoverage cases, the three affected GUIInteractionCoverage cases, and the JavaScript terrain permissions fixture. Exact case names/results are recorded in XML. At 524, rerun the JavaScript terrain resource permissions fixture and realistic planner/survey case; both pass. Additional final display checks:

```sh
python3 test/run_tests.py --binary engine \
  --filter 'MarketsV2/market panels*' \
  --filter 'TeamStatsSave/measurement screenshots*' \
  --filter 'Settings/catalog building defaults*' \
  --filter 'BuildingCatalogFixtures/ordinary building aliases*' -j2
```

Native replay/match commands and fixture hashes are in `native-cross-617071ba4/manifest.json`; final CLI equality is recorded in both candidate provenance files. Browser exact commands and reports are inside its bundle.

`sha256.json` authenticates each bundle and environment record. Bundles retain successful results, original failures, limitations and generated images; they omit executable binaries and dependency caches.
