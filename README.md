# Studio web artifact verification

Tested PR head: 3644e84f4 (full head recorded in PR comment). Base: 7c5e54fbb5bdcba166f94cf7b538a6de8a8beb23; fetched again before acceptance, unchanged.

Original exact CI web-client artifact: run 37271070460, source 42c7c07a51efe62bceebb36c425416704e8bc5a1, artifact 11329434694. This contains the serial/pthread WASM engines, index.html and loader but omits studio.html. Its iframe tests fail across Chromium, Firefox and WebKit. Local HTTP probe confirms 404. The generated studio.html is byte-identical to index.html by scons/web_build.py's existing shell action. Materialized that missing generated output from the archive's index.html, leaving runtime bytes unchanged, to verify the revised archive contents.

Ubuntu 26.04.1 x86_64, Python 3.14.4, Node 22.22.1, Playwright 1.63.0 and its installed browser engines; CI WASM build toolchain identity attached. This PR only adds the generated Studio entry to two upload lists; intermediate master commits change smoke fixtures, native mobile preprocessing and Python tests, not these browser runtime bytes. Full engine rebuild is unnecessary for artifact inclusion verification.

Commands (all exit 0):

```
python3 browser/serve.py 8782 --bind 127.0.0.1 --directory artifacts/master42c-web-client
# From browser/:
GLOB2_TEST_URL=http://127.0.0.1:8782 GLOB2_TEST_RENDERER=software node node_modules/@playwright/test/cli.js test tests/studio.spec.js --project chromium --output ../artifacts/studio42c-after-results
GLOB2_TEST_URL=http://127.0.0.1:8782 GLOB2_TEST_RENDERER=software node node_modules/@playwright/test/cli.js test tests/studio.spec.js --project firefox --project webkit --output ../artifacts/studio42c-compat-results --reporter list
# From repository root:
python3 -m unittest discover -s test/build_system -p test_ci_policy.py -v
python3 -m unittest discover -s test/build_system -p test_browser_package.py -v
```

12 Studio tests pass across three browser engines: live iframe launch/progress, bounded failing-controller diagnostics, and serial/threaded traces whose exact byte length and SHA-256 match the committed native golden fixture. Chromium also checks pause/resize/stop/relaunch. CI policy: 19 tests pass. Browser packaging: 14 tests pass. Entry page checksums match.

No full browser suite, manual hosted download, native/mobile or WebGL2 matrix rerun. No source simulation/save/replay/network changes, no SIM_REVISION change. Existing separate Chromium campaign page crash remains under investigation and is not resolved by this repair. Focused checks establish artifact entry restoration, not whole-master green status.

Original failure: https://github.com/Globulation2/glob2/actions/runs/37271070460/job/111649924530
Maintainer acceptance: Codex accepts focused local verification as sufficient for this artifact inclusion fix under AGENTS.md.
