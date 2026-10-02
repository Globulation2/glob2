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
