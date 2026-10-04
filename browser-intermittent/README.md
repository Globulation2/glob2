Intermittent browser failures retained for investigation

Full manual run 37184200265 (f66fe1866) had Chromium renderer startup stall and Firefox viewport freeze; the parallel master-push full run 37184199708 passed both jobs and every other test job. Its only failure was the obsolete match trace count repaired in #697.

Chromium failed while screen remained loading after core/game downloads and storage restore completed. Firefox froze loop/frame/tick counters after resizing, before the requested menu appeared. The traces are preserved. No production cause has been established; no retries, removed assertions or timeout increases were added.

Focused verification used the unmodified web-client artifact from run 37184200265, served at 127.0.0.1:8775, on Ubuntu 26.04 x86_64 / Node 22.22.1 / Playwright 1.63.0:
- `GLOB2_TEST_URL=http://127.0.0.1:8775 npx playwright test tests/rendering.spec.js --project=chromium --grep 'default renderer falls back' --repeat-each=10`: 20 passed (3.2m).
- Custom startup stress: 20 batches of four concurrent isolated contexts in Chromium; all 80 launches passed, no captured stall. The stress log is retained.
- `GLOB2_TEST_URL=http://127.0.0.1:8775 npx playwright test tests/viewport.spec.js --project=firefox --grep 'running match survives' --repeat-each=12`: 12 passed (16.7m), no retries; retained HTML report.

These passing repetitions do not establish a root cause or fix. Full exact latest-master verification follows separately.

Extended Chromium stress used 50 batches of eight simultaneous contexts: another 400 successful startups (480 stress launches total). The script and log are retained; copy the script to `artifacts/ci-repair/` in the product checkout before running so its relative Playwright dependency path resolves. It records debugger stacks on a failure; none was captured. This increases reproduction coverage but still does not identify a root cause.

Two additional constrained runs completed: 100 startup launches with process CPU affinity limited to CPUs 0 and 1, then 100 with the same affinity and the top-level browser's reported hardwareConcurrency set to 2 (a controlled browser-boundary input). Both passed all 50 two-context batches. Total stress coverage is 680 startup launches, plus the 20 renderer fixture repetitions. These constraints are local diagnostics, not a claim of reproducing the hosted machine. The original failure remains un-reproduced.
