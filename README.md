# Map Studio verification evidence

Tested feature commit: `822ac6e15f58bad26bec877384078598bb0f82fc`.
Integrated/fetched master base: `bd57ae815d25bbfe1c6696b5dd9e5519406055b5`.
Baseline screenshots: `745cef3d157018efed75d0c20ef8addb6c163882`, same seeded saved-project fixture.

Environment: macOS 26.6.2, arm64, bundled Node 24.19.0, PostgreSQL 16.15 (Homebrew), npm lockfile dependencies (`npm ci`). Production Vite build; Playwright Chromium desktop and Pixel 7 emulation; current master CSP policies.

## Exact commands and results

From platform, with Node 24 first on PATH:

```sh
npm run lint
npm run typecheck
npx vitest run apps/web/test/studio.test.tsx apps/web/test/studioReview.test.tsx apps/web/test/studioStream.test.tsx apps/web/test/musicStudio.test.tsx apps/web/test/musicStudioStream.test.tsx packages/map-studio/test apps/api/test/studio.test.ts apps/api/test/studioEvents.test.ts apps/ai-map-worker/test/progress.test.ts apps/ai-map-worker/test/attempts.test.ts apps/ai-map-worker/test/playability.test.ts packages/protocol/test
```

All pass: 134 tests in 16 files, including protocol fixture/schema checks. From platform/apps/web:

```sh
npx vite build
PORT=4283 SCREENSHOT_DIR=<evidence directory> npx playwright test -c e2e/playwright.config.ts e2e/studio.spec.ts e2e/music-studio.spec.ts --timeout 30000
```

Build passed; all 4 browser tests passed. `git diff --check` passed. Matching final logs are in this directory.

## Coverage and limits

Store/worker tests cover transactional completion, duplicate submissions/completions, one linked generation and reservation per turn, rollback, credit and lease rechecks, crash journal recovery, malformed decisions, and legacy isolation. Intent examples use mocked structured responses and prompt/schema contracts; they do not establish live model classification quality. Browser tests cover desktop geometry, keyboard resizing, popover focus, version targets, fresh-map transitions, active/failed/uncertain/no-credit states, mobile scroll/draft persistence, accessibility, and Music Studio regressions.

Message history occupies 80.7% of its pane at 1440×900 and 77.2% at 1366×768; geometry is in the layout JSON files. Before/after screenshots at 1440×900, 1366×768, and 390×844 are included, with both mobile tabs.

No native simulation code, save/replay/network compatibility, or map algorithms changed in this PR. Native determinism and full native delivery suites were omitted on that basis. No live provider/payment/paid generation test; delivered terrain still needs human play review. No DB migration. Deploy the updated map worker before API/browser rollout.

Evidence is isolated on this branch and must not merge into master.
