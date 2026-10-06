Music decoder CSP verification

Tested revision 16c2f0135; base/current master 745cef3d157018efed75d0c20ef8addb6c163882. macOS arm64, Node 22.22.1, PostgreSQL 16, Playwright 1.63.0 bundled Chromium. Dependencies from platform/package-lock.json.

Local browser command (from platform/apps/web, Node 22 on PATH):
TEST_DATABASE_URL=postgres://glob2@127.0.0.1:55439/postgres node ../../node_modules/@playwright/test/cli.js test -c e2e/playwright.config.ts music-decoder.spec.ts music-studio.spec.ts
The decoder test fails with the reported CompileError when the worker allowance is removed (csp-before.log), then decoder and workspace tests pass on desktop and phone, including workspace axe checks (4 passes, csp-after.log). The test server reads policy strings from the actual Caddyfile. Decoder JS/WASM are the deployed production binaries from the verified 847ff8af3 build; decoder source, build scripts and lockfile inputs have not changed between that revision and the tested source. The existing trimmed Opus fixture contains 4813 stereo frames; the worker validates its SHA-256, opens all three mood streams and renders nonzero PCM. Main-document WebAssembly compilation stays blocked.

Live command from root:
node platform/node_modules/@playwright/test/cli.js test -c artifacts/music-enable/csp-live.config.ts
Desktop and phone both pass against https://app.glob2online.com with the real live document and worker CSP headers and live decoder binaries (csp-live.log). Only the tiny existing fixture audio response is intercepted; no decoder/policy mock, private music reads, generation or credit charges. This verifies production decoding, not the user's speakers or a full private-track playback session.

ESLint and Prettier on changed TypeScript files pass; git diff --check passes. Caddy validate passes on the production image. The deployment image reuses the unchanged web/decoder layers of glob2-caddy:scroll-847ff8af3 and copies only the tested Caddyfile. Image glob2-caddy:csp-16c2f0135, digest sha256:99fb2152a25b53527e3626c8e566a9a2b3e2f581eaf4ab7894eb7a67469c16fd. Deployment respected the shared lock; host source cherry-pick b38298af4. Live document retains script-src self; only /music/decode-worker.js adds wasm-unsafe-eval.

A broader 132-test web run stopped after an unrelated accessibility fixture failed its generic H1 assertion on Colony Studio; its dark counterpart was interrupted and 130 cases did not run (csp-suite.log). The same H1 failure is checked without the newly applied test-server headers (csp-a11y-baseline.log). Do not count the broad suite as passing. Full native/simulation/save/replay/platform matrix omitted because only edge CSP, its browser harness and documentation changed.
