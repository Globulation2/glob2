Local / VM verification

- Tested commit SHA: `1f605a3edabdb43c8d9f9ec024ba9d064e95ff83`.
- Base revision and integration state: PR head derived from `72f8c9373`; checked for a clean merge against current master `3908fe832a62ba8690076ff027b219200d262923`. The newer base changes coverage artifact compaction in native CI, not the web editor, web dependencies, or browser harness.
- Environment: macOS 26.6.2, arm64; Node 24.14.0; npm lockfile dependencies installed with `npm ci --no-audit --no-fund`; Vitest 5.0.3, Playwright 1.63.0, Chromium 1243, Vite 8.3.2.
- Dependencies, build configuration and flags: standard production Vite build; headless Chromium with `--use-angle=swiftshader --enable-unsafe-swiftshader`. Browser tests ran against production `vite preview --host 127.0.0.1 --port 4281` using the included temporary Playwright configuration. Production assets built at `058a7ac0f`; the only subsequent source change makes the test wait for the selected model, so the application/dependencies/build inputs are identical at the tested head.
- Coverage rationale: hidden, sloping, edge-on, and shared-UV brush coverage; fixed upright rotation through all models; document transactions; undo/redo; animation and pattern cancellation; capture ownership; multi-pointer/pen handling; pinch zoom and final-view framing.
- Exact commands (run from platform unless otherwise noted):
  - `npx vitest run apps/web/test/skin-projection.test.ts apps/web/test/skin-document.test.tsx apps/web/test/skins-workspace.test.tsx`: 3 files, 20 tests passed, exit 0.
  - `npx tsc -p apps/web/tsconfig.json`: passed, exit 0.
  - `npx eslint apps/web/src/skins/{geometry,MeshPreview,projection}.ts* apps/web/src/pages/Skins.tsx apps/web/test/skin-projection.test.ts apps/web/e2e/skins.spec.ts`: passed, exit 0.
  - `npm run build -w @glob2/web`: passed, exit 0; existing bundle-size advisory remains.
  - Repository root: `platform/node_modules/.bin/playwright test -c artifacts/skin-editor/playwright.config.ts --grep 'inspection rotation|direct strokes|two-finger gestures|capture ownership|trackpad pinch'`: 10 cases passed across desktop and emulated phone layouts, exit 0.
- Omitted checks and limitations: physical phone/pen hardware, Firefox/WebKit, backend publishing/account integration, and native engine/platform suites. This change affects web editing interactions only; no engine simulation, save format, replay acceptance, network protocol, or simulation version changes. Browser viewport emulation does not establish physical device coverage. No full hosted matrix requested.
- Evidence: logs, configuration and final desktop/phone screenshots on the dedicated evidence branch linked below.
- Maintainer acceptance: author accepts this focused evidence for the revision, acting on genixpro's explicit request to commit, push and merge.
