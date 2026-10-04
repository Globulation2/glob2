Final full master verification

Tested revision: e958fe16e25091f177c2f280301d4cc3a509df71.
Immediate base: fa23fed7d5fb843a71d45b9f1f5fb3d487408e80.
Normal master-push run: https://github.com/Globulation2/glob2/actions/runs/37187008011 (attempt 2), completed SUCCESS.
The latest remote master was checked again after completion and still equals the tested revision.

The selected inventory is the complete development matrix: full_matrix=true, tiers_enabled=false, reused_run_id=null. Logs and job metadata are attached here. All selected jobs succeeded; skipped entries are unselected duplicate reusable-workflow branches, not omitted selected tests.

Coverage includes primary GCC 13, GCC 11 and Clang 18 native tests and map/CLI/compatibility suites, native coverage audit, thread sanitizer, Windows MinGW, macOS native, Android arm64-v8a/armeabi-v7a/x86_64, WebAssembly, Chromium/Firefox/WebKit browser suites, software/WebGL2 coverage, browser self-hosting, native WSS, TypeScript platform and self-hosted platform stack. Each job's OS, compiler, dependency and command details remain in the linked hosted run logs. CI build flags and dependency pins are those committed at the tested revision; no diagnostic sanitizer flags were used in that run.

Simulation evidence: all 7 selected --verify-match traces match; all 16 exact native/browser corpus traces match. The complete comparison log is attached, including per-case numeric/data/decoded-save/complete-trace comparisons. Save/load, replay, scripting and network suites ran in the selected matrix. These repairs did not change SIM_REVISION or the golden simulation record.

Both full runs on immediate base fa23fed7d passed on their first attempt: normal master push 37186519642 and manual full run 37186526575. e958 changes documentation only relative to that base.

Material limitation: e958's first master-push attempt had an intermittent Chromium storage-case runtime proxy abort. The failed jobs alone were rerun without changing source, assertions, retries, timeouts or coverage; the original full 20-case Chromium shard and final aggregation then passed. The SDK proxy root cause is NOT established or fixed. Its original log, trace and extracted errors remain in ../browser-intermittent. An isolated e958 full run also reproduced a storage startup stall. Do not interpret the green rerun as a runtime repair or evidence that the intermittent fault cannot recur.

Focused diagnostics retained separately: 100 unchanged complete-shard Chromium cases with matching Xvfb/native dependencies/software renderer passed, another 100 with a pre-abort proxy observer passed, 30 fresh-browser hosted software storage cases passed, and five headed Firefox viewport cases passed. These probes plus local repeats did not reproduce the proxy error. They do not replace the original failure evidence.
