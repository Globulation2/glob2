# Skin color picker verification

PR head: a59f3a2aaa6da3ebd02d62fb84d439e6d1422cd5
Base: 1a2ff45d0182b8c3d24809e21a694bc26300d0a2
Integration commit: 4cbcfbf852b1b4bc878ff014b8e5ee062f48bfae (base plus cherry-picked PR change)

macOS 26.6.2, arm64, Node 24.14.0, npm 11.9.0. Dependencies from npm ci --ignore-scripts and unchanged package-lock.json; SHA256 in integration-verification.log. Default Vite production settings. No new runtime dependencies.

Integration commands, run from platform with Node 24 on PATH, all exited 0:

```
npm ci --ignore-scripts
npx vitest run apps/web/test/skin-color-picker.test.tsx apps/web/test/skins-workspace.test.tsx apps/web/test/skin-document.test.tsx
npx tsc -p apps/web/tsconfig.json
npx tsc -p apps/web/e2e/tsconfig.json
npx eslint apps/web/src/skins/ColorPicker.tsx apps/web/src/pages/Skins.tsx apps/web/src/skins/PatternDialog.tsx apps/web/test/skin-color-picker.test.tsx apps/web/e2e/skins-studio-ux.spec.ts
npx prettier --check apps/web/src/skins/ColorPicker.tsx apps/web/src/pages/Skins.tsx apps/web/src/skins/PatternDialog.tsx apps/web/src/styles/skin-studio.css apps/web/test/skin-color-picker.test.tsx apps/web/e2e/skins-studio-ux.spec.ts
cd apps/web
npx vite build
```

15 tests passed. Vite emitted its large-chunk advisory; build passed.

Browser checks used the PR head, Vite dev on 127.0.0.1:5178, and API responses mocked to 401 for the guest editor. Download scripts to artifacts/color-picker in that source checkout to reproduce:

```
cd platform/apps/web
npx vite --host 127.0.0.1 --port 5178
# In a second terminal at repo root:
node artifacts/color-picker/check.mjs
node artifacts/color-picker/preview.mjs
node platform/node_modules/@playwright/test/cli.js test --config artifacts/color-picker/playwright.config.mjs --grep 'color palette stays'
```

Browser scripts passed all 12 Chromium/Firefox/WebKit combinations (dark/light desktop 1440x900, dark phones 412x839 and 320x568), plus building and pattern colors in Firefox. The committed browser test passed in desktop and phone projects. Screenshots were visually inspected for theme integration, visible hue spectrum and phone sizing.

Coverage addresses pointer dragging, keyboard axes, hex editing, invalid partial hex input, grayscale hue retention, external eyedropper/undo synchronization and editor layout. Production compile checks integration with newer master web routes. Simulation determinism/save/replay suites omitted because there are no simulation or data format changes. Full seeded API E2E suite, authenticated persistence, native OS/physical touch-device matrices and release packaging were not run; browser checks are guest/editor interaction coverage. No hosted expensive CI requested.
