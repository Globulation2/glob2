Browser proxy ownership regression evidence for PR #551

Baseline source: 604510c37. Fixed source: 3b2d2568a. Tested with Emscripten
4.0.15 and Playwright Chromium on macOS arm64.

The audio harness opens a fake SDL device, enqueues a real Emscripten proxy
request on the application owner without yielding, and closes it. The device
weak reference must expire before pthread exit, and its disabled mixer must
never run. The WebSocket harness uses fake socket APIs and the production
callback/close logic with the real Emscripten proxy queue. Closing one transport
must reclaim its pending event and target without executing another transport's
pending callback. Both baseline ownership assertions fail; both fixed cases pass.
The network include files add public access solely to observe ownership in this
standalone harness. Native audio output, socket I/O and browser input are covered
separately by the maintained browser integration tests.

Reproduce from this branch with the pinned SDK installed and EMSDK set to its
root. Install the browser test dependencies and Chromium first:

```sh
npm ci --prefix browser --ignore-scripts
browser/node_modules/.bin/playwright install chromium
python3 artifacts/browser-worker-lifecycle/build.py
python3 browser/serve.py 8778 --directory artifacts/browser-worker-lifecycle
# In a second terminal:
node artifacts/browser-worker-lifecycle/run.cjs
```

The harness asserts its owners have been reclaimed *before* exiting, so it does
not depend on browser page teardown reclaiming the WebAssembly heap. Generated
binaries and compiler caches are intentionally omitted from this evidence branch.

Final integration verification

The serial and threaded production runtimes build successfully. An incremental
SCons build after merging current master into the fix branch (27eb95903) confirms
both runtimes are up to date. The updated package and gzip sidecars verify.

`browser-integration.log` records 15 passing Chromium/Firefox/WebKit cases:
serial startup, automatic fallback without isolation, injected abort fallback,
actual native Worker failure after the successful probe, and nonzero PCM output
with responsive settings and clean quit. `static-integration.log` repeats actual
worker failure/fallback and audio/quit on the hashed release package in Chromium.

Serve the production build or static package using browser/serve.py, then run:

```sh
GLOB2_TEST_URL=http://127.0.0.1:8781 GLOB2_TEST_RENDERER=software \
  browser/node_modules/.bin/playwright test --config browser/playwright.config.js \
  browser/tests/threading.spec.js --project=chromium --project=firefox --project=webkit \
  --grep 'serial selection|missing isolation|startup failure|real pthread|threaded audio' \
  --reporter=list
```

For the static package use its server URL, Chromium, and
`--grep 'real pthread|threaded audio'`.
