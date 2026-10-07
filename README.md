# Runtime-resource platform verification follow-up

CI reference under inspection: `9084ea907a004ec9a2315598256e1631793c00bf`.
Base: `e53a673b2609175e781ad2608a15a220866cf2fa`.

The exact merged reference's [full master run](https://github.com/Globulation2/glob2/actions/runs/37551820848) remains in progress. Its platform TypeScript and music jobs passed; native, Windows, macOS, browser, sanitizer and Android jobs had no completed failure at this snapshot. These jobs must finish; they have not been cancelled or restarted. [The preceding master build](https://github.com/Globulation2/glob2/actions/runs/37524511499) passed. No introduced failure has yet been identified; pending jobs are not passing evidence.

| Boundary | Published evidence / current command | Current result and limits |
| --- | --- | --- |
| Native production checkpoint | [b3fab native evidence](https://github.com/Globulation2/glob2/tree/32bbac1/native-b3fab-checkpoint) | Four targets built; stock and official-match traces unchanged. Historical checkpoint, not a substitute for merged-reference source/compiler/dependency provenance. |
| Final fixture, translation, test reporting repairs | [final-cli-repairs](https://github.com/Globulation2/glob2/tree/f42fd9e/final-cli-repairs) | Includes exact commands and passing native generator/telemetry/JavaScript repairs. Final affected Windows/macOS jobs are still pending. Translation audit and all 32 non-English catalogs are included in original PR evidence. |
| Windows merged reference | [job 112569573348](https://github.com/Globulation2/glob2/actions/runs/37551820848/job/112569573348); `test/test_cli_smoke.py`, `test/check_shared_runtime_save_continuation.py`, `test/run-browser-determinism.py` | Compiling at snapshot. Production CLI continuation, stock, match, shared scripting and engine checks not yet complete. |
| macOS merged reference | [job 112569573599](https://github.com/Globulation2/glob2/actions/runs/37551820848/job/112569573599); `.github/workflows/ci-macos.yml` | Compiling at snapshot. Native continuation and stock/match traces pending. Current workflow excludes generator cases. No locally accessible macOS executor has been established. |
| Browsers, merged reference | [master run](https://github.com/Globulation2/glob2/actions/runs/37551820848); `browser/tests/determinism.spec.js` | Wasm build still running, Chromium/Firefox/WebKit serial/threaded runtime checks pending. Prior browser a1 evidence is historical after subsequent query optimizations. |
| Android production checkpoint | [Android b3fab evidence](https://github.com/Globulation2/glob2/tree/78dd865/android-b3fab-checkpoint) | 36 unit / 98 engine cases; native-identical stock, official-match and 150-row custom composition traces. x86-64 emulator only; source/payload stable. Physical ARM and APK UI not established. |
| macOS generator full fingerprints | `MapGeneratorGoldenTest PROFILE --print` and independently archived pre-epoch map comparisons | Explicitly unavailable. Current table has 544 Linux rows; 1,108 historical rows remain preserved. No expected hashes were changed; strict `--require-rows` is unchanged. New macOS output requires actual macOS execution and independent comparison before acceptance. |
| Gameplay tournament | [final-tournament-b3fab](https://github.com/Globulation2/glob2/tree/f42fd9e/final-tournament-b3fab) | Reference existing 42-run campaign and repair evidence. No rerun and no expanded gameplay-equivalence claim. |

## Focused collection/comparison repair

Existing browser cases already emit and compare the 150-row custom composition fixture. The current CI aggregation did not require this evidence across native and browser producers. The repair adds native collection to Linux, Windows and macOS determinism jobs and requires exactly one complete trace from each selected native platform and each serial/threaded Chromium, Firefox and WebKit producer. It compares complete trace bytes against the committed fixture, checks matching clean embedded source provenance, and rejects missing/duplicate identities and failed browser runs. Historical golden files and production simulation are unchanged.

Commands validated locally on Linux x86-64, Python 3.14.4:

```
python3 -m unittest discover -s test/build_system -p 'test_ci_*.py'
python3 -m unittest discover -s test/build_system -p test_resource_evidence_collection.py
git diff --check
```

Results: 89 CI policy/selector/contract cases passed; 3 collector cases passed. Collector checks include complete evidence, truncated output, interrupted/failing execution, temporary profile cleanup and rejecting stale evidence. Comparator tests cover missing platforms/browsers, partial traces, dirty producers, mismatched revision/source hashes and failed browser execution. This is tooling verification, not native or browser simulation execution. New collection must run on the final repair revision before claiming those platform gaps closed.

Raw API snapshots: `artifacts/resource-followup/platform-closure/`. Publish this matrix and logs to an evidence branch / PR after finalizing the tested revision. These local paths alone are not published evidence. Deferred: physical ARM/iOS, expanded statistical gameplay trials and maintainer gameplay review.

Repair source: `17bbbcf41`. Focused tooling tests only; native/browser execution of the new collector is pending. Raw logs and environment are in `platform/`. Older campaign evidence remains linked above.

## Benchmark tooling

Tested source `4b7fa7a37`, base `17bbbcf41`; Linux x86-64/Python3.14.4. `python3 -m unittest discover -s test -p 'test_*benchmark*.py'`:22passed, exact output in `benchmark/benchmark-contracts-final.log`. Independent rotated review passed. Report-only preserves threshold diagnostics and still rejects execution, changed inputs and incomplete windows. Governor orchestration tests cover restoration and interruption/owned-child cleanup. No live governors changed and no performance acceptance is claimed. Native builds and recovery of removed original checkpoint assets remain underway; actual effective interval frequency is explicitly unavailable without a supported measurement.

## Generator evidence collector

Tested source `0a19ac804`, base `4b7fa7a37`; Linux x86-64/Python3.14.4. `python3 -m unittest discover -s test/build_system -p test_generator_evidence.py`:4passed. Rotated review identified a binary-provenance gap, corrected with before/after binary/source/table audits and explicit limitation that producing build-job evidence binds executable to source. Host compiler is labelled as such. Same-host repeats are not independent-platform evidence, and collected rows remain unapproved. Historical expectations and strict missing-row checks are unchanged. Actual macOS collection awaits hosted execution.

## Introduced Maxima source-contract failure

Merged master job [112574794900](https://github.com/Globulation2/glob2/actions/runs/37551820848/job/112574794900) failed a stale source-string assertion after the indexed material query refactor. Focused repair `3b3d7a8d5036906e9c4b64c80bb9e7a8ff44619b`, base `9084ea907`: update that assertion only. Local before failure reproduced, after61tests passed with1expected unavailable-binary skip; independent focused17tests pass. Exact failed hosted log and before/after outputs are in `maxima-repair/`. No production code, simulation/save/network behavior or gameplay changes. The unavailable local dynamic test is covered by the requested hosted follow-up, not claimed passed locally.

## Clean final native execution

Source `7bafc3f15298a1649e807e42909c3fa2fe3bc609`, clean embedded test provenance; integrated production remains `9084ea907` (only test/tooling/docs changed). Linux x86-64/GCC15.2 release/O3 optimized_assets=0 with the recorded SDL/recording prefixes. `native-7baf-validation.zip` retains exact commands/environment/build logs, all per-file SHA256 values (independently re-read after compression), stock/match/composition traces, generator JSON and results.

- Maxima policy suite:75passed, zero skips, closing the earlier unavailable-dump omission. The focused repair is separately committed at `3b3d7a8d5036906e9c4b64c80bb9e7a8ff44619b`; its source assertion is identical in this tested stack.
- Five-design resource-epoch case:passed; all five observed full/topology outputs match existing Linux references. This does not verify macOS rows.
- Updated native collector:passed, stock + official match traces unchanged; all150resource composition rows match the committed fixture with clean source provenance.

Commands: same prefix environment as prior native build, `scons -j8 release=1 optimized_assets=0 engine-tests maxima-strategy-dump`; `GLOB2_BUILD_DIR=build/linux/client/release python3 -m unittest discover -s src/ai/maxima -p '*Test.py'`; `test/run_tests.py` exact design filter and updated `test/run-browser-determinism.py` invocation retained in summary/logs. Omitted: fresh complete native suite, display/gameplay review, physical ARM/iOS and final hosted platform execution. These test-only changes do not change simulation/save/network behavior.
