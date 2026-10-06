# Coverage deadline and diagnostics verification

Tested d84e57233c2a52581a750a5e934d70d182b69bc0; fetched base 69284f8d5cf84ee749b35b5dd58fdaea7c925f32. Ubuntu26.04.1 x86_64, Python3.14.4. No native compiler/dependency change.

Two full master job deadlines: https://github.com/Globulation2/glob2/actions/runs/37396129432/job/112056743666 and https://github.com/Globulation2/glob2/actions/runs/37399726961/job/112074743651. Annotation: maximum execution time 1h0m0s. Both were executing coverage at termination; artifact upload then failed to complete. Execution logs unavailable (404), so slow phase/root cause unknown. Comparable passing coverage job https://github.com/Globulation2/glob2/actions/runs/37401387308/job/112070010201 took45m29s for build/tests/report; retained artifact https://github.com/Globulation2/glob2/actions/runs/37401387308/artifacts/11386559883. No timing savings claim or claim of ten-run statistics.

Commands at repository root:
- python3 -m unittest discover -s test -p test_run_coverage.py -v:9PASS; real subprocess emits ready and waits for observer to release it, proving output is forwarded before completion. Nonzero7exit and combined stdout/stderrfilecontents asserted. Defaultsilent/file-only subprocess retains nonzero3exit. All seven accounting/profile retention checks remain passing.
- python3 -m unittest discover -s test/build_system -p 'test_ci*.py' -v:88PASS (selection/tiers/concurrency/measurements/gates).
- python3 -m unittest discover -s test -p test_ci_failure_aggregation.py -v:PASS.
- git diff --check:PASS.

Only native-coverage job budget60to90min and opt-in stream-logs flag added to CI. Case selection, instrumentation-O0, jobs4, per-case900seconds, assertions, gate policy and failed profile retention unchanged. Streaming applies to command stdout/stderr, not machine-readable JSON export; export phase announced separately. Localfile-onlydefault retained.

Full native coverage not run locally; hosted subsequent full master required to confirm budget adequacy and diagnose any remaining stalls. No runtimeengine/simulation/save/replay/protocolcodechange or SIMD revision bump.
