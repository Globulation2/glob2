Serial renderer CI budgets and partition repair (#1218)

Candidate: 76f558837babe32859aa7665c4f476362b294e93. Base: 9e8612a4d394a19bd98aaa8956bd73426ed19aa7. Linux x86_64; pinned Emscripten 4.0.15, Node 24.19.0 and Playwright 1.63.0; existing SDL 3.4.16/codec dependencies; release=1. Only test deadlines, CI partitioning and the verification guide change. There are no production, simulation, save-format, runtime-dependency or gameplay changes.

Two hosted Chromium 3/5 runs took 57.3 and 57.5 minutes while serial cases exhausted their six-minute total deadline. A captured menu-switch trace shows stable enabled controls and host loops advancing to 363. Simple control queries have a 42 ms median and 2.38 s maximum; two-animation-frame waits reach 4.55 s and screenshots take 8–10.7 s. Normal software-GL work consumes the case budget. The raw text trace, source logs and derived progress/timing records are retained; the embedded-Wasm trace.zip is excluded.

Serial switch cases now have a bounded 12-minute limit; serial recording/backend-pair cases have 16 minutes. Threaded six/eight-minute limits and the existing standalone shader 15-minute limit remain. Every assertion remains. Renderer cases move from the five general Chromium shards into three serial shards (per-test partitioning, one worker each) and one threaded job (one worker). The 60-minute job limit and maximum browser concurrency of ten remain.

Actual Playwright --list comparison proves the moved/general inventory is exactly 202 cases before and after, with matching multiplicities and no omissions, additions or duplicates. All 19 other matrix entries remain unchanged, including intentional platform/browser/renderer repeats. Exact commands and case lists are in matrix-*-inventory.json and inventory-comparison.json. The coverage-policy fingerprint changes, requiring a fresh full hosted checkpoint.

Final matching-source validation:
- Clean candidate browser rebuild passes: env CCACHE_MAXSIZE=512M scons --build=build/ci-recovery-web target=web release=1 -j8 web-tests build/ci-recovery-web/index.html.
- All four new renderer commands pass: 16 cases passed, five capability skips, zero failures. Commands, individual logs, screenshots/media and results are in clean-current/. The longest local job took 9.4 minutes; this is not a hosted performance claim.
- All 24 determinism/scripting cases pass on that exact clean producer. All four exported build manifests independently match actual binary hashes and the clean source/embedded revision. Headers, file hashes, raw outputs and per-tick traces are in clean-provenance/.
- All 567 build contracts pass (two optional skips): artifacts/ci-recovery/python-tests/bin/python -m unittest discover -s test/build_system -p 'test_*.py'. The same pinned interpreter runs tools/check_docs.py: 309 documents, zero errors. Diff checks pass.

Initial executions against the production-identical older 562 runtime also passed all four renderer jobs; the root chromium-* folders explicitly identify that diagnostic composition. They are separate from final clean-current/ qualification. The complete prior 221-case Chromium checkpoint remains in master-562 evidence (204 passed, 17 skipped, zero failures) and is not attributed to this candidate.

Timing fields are separate in hosted-run-metrics.json and timing-summary.json. Historical failed full run 38895151593: queue 613 s; active execution 7,442 s; execution span 7,449 s; idle 7 s; time to result 8,062 s; runner minutes 781.2167. This is one failed historical observation. No ten-successful-run matching comparison exists, and no queue, runtime, runner-minute or feedback improvement is claimed. No reduced coverage tier is enabled.

Full hosted confirmation of the new policy remains pending. The current 9e run passed Windows and the formerly failing GCC 11 recording shard, but lacks this budget/partition repair. Actual Firefox/WebKit execution of the changed renderer fixture is omitted locally because these selected renderer cases run in Chromium; their existing browser inventories remain unchanged.
