# Browser startup verification

Tested PR head: `f56f7595a248cd02ddd987e58200e18906e22614`. Base: `21e838582406b910c05651aac48646f9dedb9c41` (PR head rebased onto current master).
Host: Linux 7.0.0-31-generic, x86_64; Emscripten 4.0.15; Playwright 1.63.0.
Pinned SDL dependencies and optimized asset export; standard release build flags
(`-O2`, WebAssembly exceptions, growable memory, serial and pthread variants).
Exact compiler/dependency inputs and flags appear in build logs.

## Final revision

- `scons target=web release=1 -j24`: both runtimes built, exit 0.
  [Integration build](integration-build.log.gz),
  [canvas-fix link](final-build-2.log.gz), [final build](verified-build.log.gz).
- `node --test browser/unit/*.test.js`: 42 passed, exit 0. [Log](final-unit.log).
- `python3 -m unittest discover -s tests/build_system -p test_web_assets.py -v`:
  13 passed, one fontTools-dependent glyph test skipped, exit 0.
  [Log](integration-packages.log). The asset/test inputs are unchanged by the
  final presentation-only JavaScript change, so these results remain applicable.
- Browser tests served the final packaged index.html with
  `python3 browser/serve.py 8772 --bind 127.0.0.1 --directory build/emscripten/client/release`.
  Eight focused cases passed across serial/software, threaded/software and
  threaded/WebGL2; Chromium and Firefox. All commands exited 0.

For each browser command use `GLOB2_TEST_URL=http://127.0.0.1:8772` and
`browser/node_modules/.bin/playwright test -c browser/playwright.config.js`:

| Environment | Test files and selection | Result / log |
| --- | --- | --- |
| `GLOB2_TEST_ENTRY_PATH='/?threads=threaded' GLOB2_TEST_RENDERER=software` | `browser/tests/staged-assets.spec.js browser/tests/asset-loading.spec.js --grep 'game sprites stay\|failed game download' --project=chromium --project=firefox --workers=1 --output=artifacts/browser-startup/verified-threaded --reporter=line` | [4 passed](verified-threaded.log) |
| `GLOB2_TEST_ENTRY_PATH='/?threads=serial' GLOB2_TEST_RENDERER=software` | `browser/tests/staged-assets.spec.js --grep 'game sprites stay' --project=chromium --output=artifacts/browser-startup/verified-serial --reporter=line` | [1 passed](verified-serial.log) |
| `GLOB2_TEST_ENTRY_PATH='/?threads=threaded' GLOB2_TEST_RENDERER=webgl2` | `browser/tests/staged-assets.spec.js --grep 'game sprites stay' --project=chromium --output=artifacts/browser-startup/verified-webgl --reporter=line` | [1 passed](verified-webgl.log) |
| `GLOB2_TEST_ENTRY_PATH='/?threads=threaded' GLOB2_TEST_RENDERER=software` | `browser/tests/viewport.spec.js --grep 'menus follow' --project=chromium --output=artifacts/browser-startup/verified-resize --reporter=line` | [1 passed](verified-resize.log) |
| `GLOB2_TEST_ENTRY_PATH='/?threads=threaded' GLOB2_TEST_RENDERER=webgl2` | `browser/tests/viewport.spec.js --grep 'menus follow' --project=chromium --output=artifacts/browser-startup/verified-resize-webgl --reporter=line` | [1 passed](verified-resize-webgl.log) |

PNG evidence is in the corresponding verified-* directories. The animation test
holds sprite downloads under the loader, then holds later downloads, verifies
colony pixels change, and starts a custom match. Resize tests verify actual size
changes and clickable controls in both software and WebGL presentation.

## Failure investigation and scope

The refreshed pre-fix software test reproduced a blank Chromium canvas both in
parallel and in isolation. [Failure log](integration-threaded-retry.log) and
trace/screenshots are retained in integration-threaded-retry. Instrumentation
observed valid SDL image pixels, intermittently transparent canvas pixels, and
repeated identical width/height assignments through the SDK canvas-size setter.
The guard now skips redundant software assignments when shared dimensions already
agree; real resizes, transferred canvases and GPU handling remain with the SDK.
[Instrumented capture](inspect-resize.log). The final tests above pass this case.

Earlier broad startup validation (before rebasing) covered cache reuse, required
package retries, language/font startup, menu restoration after gameplay, nested
dialogs and system cursor presentation. [Broad run](threaded-tests.log),
[isolated corrected checks](final-regressions.log), [serial run](serial-verified.log).
All 22 non-skipped Chromium/Firefox cases passed across those runs after fixing
an incorrect test counter; two existing Firefox direct-language-persistence
cases were skipped. These are supporting prior results, not final-revision claims.

WebKit could not launch on this host (missing libevent-2.1.so.7); its failed launch
logs remain in the broad run. Physical-device and live-hosting performance are
unverified. No measured end-to-end speedup is claimed.

Focused coverage addresses browser assets, initialization, presentation and real
resizes. Simulation rules, synchronized RNG, saves, replay acceptance and network
contracts are unchanged by this PR. The decorative colony owns its random stream.
Native/platform determinism matrices were not rerun for these presentation changes;
SDK, native rendering code and simulation code are unchanged by the PR.

This snapshot is dedicated PR evidence and is intentionally outside the main branch.
