## Native 4ca checkpoint and credible CPU regression

`native-4ca-summary.json` identifies source revisions, exact CLI identity and limitations. The current CLI at 4ca3b6eb6 is byte-identical to da80dbaac; the intervening merge only refreshes the browser replay fixture. No embedded Git revision label is claimed.

- `native-4ca-broad.zip`: 1,911 native cases, 1,763 passed, 148 skipped, no failures/errors, with raw outputs and fixtures.
- `native-4ca-focused-display.zip`: 361 focused passes and one intentional display skip; all nine display cases pass. Includes screenshots, clean builds and stock/match traces.
- `native-4ca-performance-diagnostics.zip`: retained calibrations, aligned fixed-window preflight, the stopped full-corpus campaign, and independent three-arm diagnosis. The completed middle scenario fails the 5% individual CPU gate: ratio 1.185884, 95% interval [1.176691, 1.195702]. All rows remain, input hashes are unchanged, and no full-corpus aggregate is claimed. The approved-fixes control reproduces the changed end state; this does not waive the performance gate. Profiles are diagnostic, not acceptance timings.

Each archive contains a per-file SHA256 inventory; every decompressed member was verified. Executables and dependencies are not bundled. Final platform refresh, candidate tournament and remaining stress runs are pending. This is checkpoint evidence, not completion or performance acceptance.

Identity clarification: earlier raw notes used “build label” for the checkout revision during a build. No embedded Git label was verified; those notes describe checkout provenance only. Binary SHA256 records remain authoritative.

## Native query-optimization checkpoint641dca365 (historical)

The `native-query-641-cases.zip` and `native-query-641-diagnostics.zip` pair retains 725 evidence files: focused native tests, build logs, the clean641 stock/match traces, the preceding f0 integration checks, profiling commands/raw samples/stat counters, and historical41384 single-case calibration rows. `native-query-641-summary.json` records exact boundaries and limitations. Extract both ZIPs into one directory; the common `file-inventory.json` authenticates every member by SHA256. Each decompressed member was verified before publication. Executables and dependency installations are omitted; existing corpus/SDL packages supply related reproduction inputs.

Focused native JUnit has317cases:316pass, one deliberately skipped display case, zero failures/errors. These tests ran against the exact source subsequently committed as641dca365 (checkout HEAD72fdb68c8 plus the exact tested patch); a clean build at641dca365 and native CLI traces are separately retained. This is not a final full-suite or final platform run. Source has since advanced to da80dbaac with additional reviewed query optimizations; that revision's acceptance remains pending.

The first cache pass preserves checksums/fullteam histories/job counts across7680ticks and reduces user instructions9.1%. Instructions and sampled cycles are diagnostic because these profiling runs include startup and concurrent workloads. Earlier quiet41384 calibration is retained with its inconclusive/drifting pure-control result and slower approved-fixes comparison, not presented as a641 performance verdict. No corpus CPU gate has passed. The ZIP splitting helper initially reused mutable ZipInfo objects and failed; the original archive remained unchanged, the helper was corrected, and all published members passed fresh hash verification. No simulation was rerun or altered by that packaging repair.

## Historical approved-fixes control tournament (ee3 baseline)

`ee3-approved-fixes-control/` preserves all 84 runs (42 pairs) comparing pure `ee3be8ecd9d7100de78ef63869271371d427cb66` with that same engine plus only the three approved behavior fixes. This is **not a refactor comparison**, and master has since advanced to `f0ff8384b`; a fresh aligned campaign is running. No performance claim is made from these concurrent runs.

The five lossless log shards preserve every original engine log (5.182 GB uncompressed); every decompressed member was checked against its original SHA256. The companion ZIP contains metrics, commands, telemetry, provenance, exact analysis scripts, the seven-file patch, runtime catalog bytes and all 42 starting fixtures. The inventory identifies contents and hashes. Executables and dependencies are not included; earlier corpus and SDL reproduction packages provide related inputs.

All runs and 248 team observations completed with no engine, hash or parser failure. The wrapper exited 143 after writing run records but before summary analysis; the cause is unknown. An analysis-only recovery verified inputs and produced summaries without rerunning or altering those records; its manifest remains in the ZIP.

For the 40 primary games (eight geometry clusters), the fixes alone changed mean final population by -13.65 (cluster-bootstrap 95% CI [-31.03, -1.83]) and food deliveries per 1,000 ticks by -2.01 (CI [-4.08, -0.22]). Starvation delta was -2.4 (CI [-7.58, 3.10]); resolved games increased 14 to 17, with different winners in three of the ten games resolved in both arms. Full metrics, censored outcomes and separate small historical controls are retained. These effects must not be attributed to the resource refactor.

# Runtime resources validation evidence

Implementation PR: https://github.com/Globulation2/glob2/pull/846
This evidence-only branch must not be merged into implementation.

## Integrated candidate

Production revision: `9a54d4bf20b73bd63bd9d0f762cee67dda71eb51`. Final native/browser test and schema revision: `a1c437295`. Windows/Android fixture revision: `de4a315d8`. Native 9a and a1 CLI binaries are byte-identical: SHA-256 `5d4b5672ebfbdc7eddd5fd49bffad5b3e8a4f0d5c3a1c604ed05c03c19af0e29`.

Upstream base: `ee3be8ecd9d7100de78ef63869271371d427cb66`. Save/replay format 140, protocol 59, simulation revision 22; supported save floor 58 remains unchanged.

| Bundle | Result | Coverage and limitations |
| --- | --- | --- |
| `native-a1-validation-evidence.zip` | Broad run: 1,897 cases, 1,748 passed, 148 skipped, one fixture failure. Corrected fixture and 12 related cases: 13/13 passed. Eight targeted display cases also passed. | This is a union of broad and affected runs, not one all-green full-suite run at final HEAD. Map image/report CLI suites pass. Build-system suite: 346 tests, two skipped, zero failures with the documented encoder interpreter. The incorrect system-Python invocation and original failure logs are retained. |
| `android-de4-validation-evidence.zip` | Nine registry and 56 engine cases passed. All three traces match native. | NDK 28.2/API 24, API 35 x86_64 emulator. All 8,137 staged inputs verified before and after tests. Covers the command-line engine bridge, not APK UI or physical ARM. |
| `browser-parity-a1-evidence.zip` | All 51 selected cases passed across Chromium, Firefox and WebKit. Twelve stock traces, three match traces and six composition traces match native/golden. | Serial/threaded engines, registry, compositions, scripting/replay boundaries, static-material continuation and completed-tick observation. An initial wrong-suite wrapper selection and six successful corrected reruns are retained; no C++ failures occurred. Fresh-process catalog-poisoning checks remain native-only. |
| `windows-de4-validation-evidence.zip` | 76/76 focused headless cases passed; stock, match and composition traces match native. | Windows 11 x86_64, MSYS2 MinGW GCC 16.2, release build with optimized assets disabled. All 16,018 source files verified before and after. Full Windows display coverage is not claimed. |

Exact commands, toolchains, build flags, dependency paths and hashes, source manifests, XMLs, logs, traces and screenshots are bundled. Compact summaries are available beside each archive. All task-owned platform processes, servers and VMs were stopped after validation.

Native stock trace: `5570a99e4345b5aa80d1f14928c4a5e5b37a03244b9327310ca1d0760d3df7e4`.
Official match trace: `c02adc67dc9e6b9a30b289f78038d72c3b7550c95db84025b28d65f49bf0cd38`.
Composition trace: `0c2b5b0ae134187198c1a9a3a43b3474192cf05f605b0922b70f6f3fa9f47ab2`.

## Portable inputs and dependencies

The frozen corpus is supplied in `resource-corpus-land-and-eight-team.tar.gz`, `resource-corpus-water-two.tar.gz`, `resource-corpus-water-four.tar.gz` and `resource-corpus-companion.tar.gz`. Together they preserve all 110 distinct saves used by 98 legacy performance windows, three eight-team windows and 42 completion-tournament scenarios. See `resource-corpus-archives.json` for sizes and hashes. Extract all four together, then run the companion's `bind_paths.py` to verify fixtures and create manifests for the chosen local paths. Optional checkout validation rejects changed, added or missing runtime catalogs within its documented extension scope. Original manifests, generation/checkpoint commands, control patches and provenance remain bundled.

`evidence-window-preflights-ee3-9a54d4bf2.tar.gz` contains 202 terminal window records and six shortened-prefix validations. One wrapper exit 143 was recovered with unchanged inputs; its cause remains unknown and original partial evidence is retained. All preflight timing is explicitly invalid for performance inference. Validated common lengths are 5,120 ticks for mixed-land late, 7,680 for Maxima-water late and 6,144 for the small mixed control's middle window. Other windows retain their original lengths.

The accepted timing runner is at source revision `bcecdf6ab`, adding before/after input verification and accurate metric descriptions without changing production code. Its eight Python tests pass (`benchmark-harness-bcecdf6ab-tests.log`). Use this runner revision with the recorded frozen binaries/data roots; the preflight helper remains preserved separately.

`sdl-reproducibility-evidence.zip` records pinned source archives, exact patches, CMake configurations, compiler/link commands, package inventories and fresh-prefix rebuild instructions. The retained SDL build matches the tested installed library after CMake's documented RUNPATH replacement. The original complete system environment/header snapshot was not recorded, so source/configuration are reconstructible but a clean bit-identical rebuild has not been performed.

**Still pending:** refreshed paired tournaments and quiet-host performance acceptance. No performance or gameplay-equivalence claim is made. macOS/iOS/physical ARM execution and human gameplay review remain unverified. Keep the PR draft. Earlier results below are historical and do not substitute for this integrated candidate.

`sha256.json` authenticates every top-level evidence bundle and summary. Archives omit executables and dependency caches. Local absolute paths identify the recorded environment; substitute equivalent paths when reproducing from the tested commits.

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
