# Runtime resources validation evidence

Implementation PR: https://github.com/Globulation2/glob2/pull/846
This evidence-only branch must not be merged into implementation.

## Integrated runtime-resource candidate

Production revision `9a54d4bf20b73bd63bd9d0f762cee67dda71eb51`; current test/schema revision `a1c437295`. Their native CLI binaries are byte-identical (SHA-256 `5d4b5672ebfbdc7eddd5fd49bffad5b3e8a4f0d5c3a1c604ed05c03c19af0e29`). Upstream base `ee3be8ecd9d7100de78ef63869271371d427cb66`; save/replay140, protocol59, simulation revision22, supported save floor58.

- `native-a1-validation-evidence.zip`: broad production run1,897cases:1,748passed,148skipped,one fixture failure; corrected fixture and12related continuation/golden cases pass in final13-case rerun. This is a union of broad and affected runs, not one all-green final full run. Eight targeted display cases passed separately. Map image/report CLI suites pass. Build-system346tests passed with2skips using documented encoder interpreter; original system-Python invocation failures are retained and classified. Exact commands, source/build/runtime provenance, XMLs, logs, traces and screenshots are bundled; summary JSON is also directly available.
- `android-de4-validation-evidence.zip`: exact fixture revision `de4a315d8`,9registry+56engine cases pass on Android API35x86_64 emulator with NDK28.2/API24. Full8,137-file payload hashes match before and after checks.1,500tick stock trace,150-row resource composition and official match trace match native. This covers the command-line engine/test bridge, not APK UI or physical ARM hardware. Summary/commands/toolchain/cleanup are bundled.

Native stock trace: `5570a99e4345b5aa80d1f14928c4a5e5b37a03244b9327310ca1d0760d3df7e4`.
Official match trace: `c02adc67dc9e6b9a30b289f78038d72c3b7550c95db84025b28d65f49bf0cd38`.
Composition trace: `0c2b5b0ae134187198c1a9a3a43b3474192cf05f605b0922b70f6f3fa9f47ab2`.

**Still pending:** final browser/Windows evidence, refreshed paired tournaments and quiet-host performance acceptance. No performance or gameplay-equivalence claim is made. macOS/iOS/physical ARM and human gameplay review remain unverified. Keep PR draft. Prior results below are historical and do not substitute for this integrated candidate.

`sha256.json` authenticates every top-level evidence bundle and summary. Archives omit executables/dependency caches. Local absolute paths identify the recorded environment; substitute equivalent paths when reproducing from the tested commits.

---

# Historical 524 validation evidence

Implementation PR: https://github.com/Globulation2/glob2/pull/846
Historical tested source: `524844f49ec8cf283a015cb687bf11286f5c4ed5`.
This separate branch contains generated review evidence only. Do not merge it into the implementation branch.

## Historical results and limits

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
