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

Exact e958fe16e full isolated run 37187146428 failed only Chromium shard 5 (restore page stuck before MainMenuScreen), and the normal master-push run 37187008011 failed the same storage case later (original page loop/frame counters froze after the second page restored and closed, before retry completed). Logs and traces are retained. Every test job on fa23fed7d passed in both full runs 37186519642 and 37186526575, but these e958 failures remain unresolved.

Hosted diagnostic run 37189372890 repeated the two renderer fallback cases and the abort storage case 30 times each: 90 passed. Its storage case used automatic renderer selection, whereas the failing shard explicitly sets GLOB2_TEST_RENDERER=software. The custom startup stress scripts above also use automatic renderer selection. This is a material reproduction limit. Run 37190821450 was cancelled because it retained that input mismatch; corrected cold-browser software diagnostic run 37191128127 follows. No master run was cancelled.

Corrected hosted software cold-browser run 37191128127 passed all 30 independently launched original storage cases (no retries). Its application artifact from full run 37187146428 was compared with the normal master-push artifact from 37187008011: all 49 files are byte-identical. Neither this nor the earlier diagnostic used the failing shard's Xvfb display or installed native runtime libraries. Complete-shard matching diagnostic 37191976691 adds those original steps and command, with stack capture on unexpected failure.

Local explicit-software fresh-browser storage case: 10 passed under CPUs 0/1. Five further forced-JavaScript-GC cases passed (a runtime probe, no application binary changes). Ten fresh-browser cases with CPUs 0/1 and navigator.hardwareConcurrency=2 in both UI and classic worker contexts also passed. These are reproduction probes, not a root-cause claim. A first concurrent local attempt shared the Playwright output directory and failed teardown with ENOENT; it was a diagnostic harness collision, preserved locally, and the ten reported explicit-software cases use a separate output directory.
