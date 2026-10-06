Music player control visibility verification

Revision 637c438988d0d58a785cdfd71ad70f882bcc3361, base 08aa1c2a069f87839930edd794e4be3b1965a708. Current master 9eb3eaef16abd8519ee1c22845e0ce90310f9a6e introduces no changes to the player or its browser fixture.

macOS 26.6.2 arm64, Node 22.22.1, PostgreSQL 16, Playwright 1.63.0 bundled Chromium. Dependencies from platform/package-lock.json.
Commands, from platform:
- node node_modules/vitest/vitest.mjs run apps/web/test/musicPlayer.test.tsx (1 pass)
- node node_modules/prettier/bin/prettier.cjs --check apps/web/src/music/Player.tsx apps/web/e2e/music-studio.spec.ts (pass after formatting)
- node node_modules/eslint/bin/eslint.js apps/web/src/music/Player.tsx apps/web/e2e/music-studio.spec.ts (pass)
From platform/apps/web:
- node ../../node_modules/vite/bin/vite.js build (pass)
- TEST_DATABASE_URL=postgres://glob2@127.0.0.1:55439/postgres SCREENSHOT_DIR=<evidence>/player-screenshots node ../../node_modules/@playwright/test/cli.js test -c e2e/playwright.config.ts music-studio.spec.ts (2 passes, desktop and phone, including axe accessibility checks)
Root: git diff --check (pass).

The workspace test asserts Play is in the initial viewport, using the reported 1470×730 desktop size and the existing Pixel 7 project. Screenshots show both themes. Fixture data contains no real user content. Playback state/worker error behavior remains covered by the existing player test. This is a control-placement change; the workspace fixture does not decode or play audio. The live decoder WASM endpoint returns HTTP 200. No audio, simulation, save/replay or protocol changes; full native matrix omitted as unrelated. Edge build/deployment evidence will be added separately.
