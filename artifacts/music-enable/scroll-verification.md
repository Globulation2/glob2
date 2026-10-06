Revision 847ff8af3, base 984b5826aa2e40b4442888293ac133f4d142fcf7 (current master fetched before validation).
macOS 26.6.2 arm64, Node 22.22.1, PostgreSQL 16, Playwright 1.63.0 bundled Chromium. Dependencies from platform/package-lock.json.

Commands from platform/apps/web (Node 22 on PATH):
TEST_DATABASE_URL=postgres://glob2@127.0.0.1:55439/postgres node ../../node_modules/@playwright/test/cli.js test -c e2e/playwright.config.ts music-studio.spec.ts --project=desktop
Before CSS change: fails workspace full-content containment assertion (scroll-before.log).
node ../../node_modules/vite/bin/vite.js build
Pass (scroll-build.log).
TEST_DATABASE_URL=postgres://glob2@127.0.0.1:55439/postgres SCREENSHOT_DIR=<evidence>/scroll-screenshots node ../../node_modules/@playwright/test/cli.js test -c e2e/playwright.config.ts music-studio.spec.ts
After change: desktop and phone pass, including axe accessibility (scroll-after.log). Full-page screenshots in both themes show controls and validation no longer clipped.
From platform: node node_modules/prettier/bin/prettier.cjs --check apps/web/src/pages/music-studio/music-studio.css apps/web/e2e/music-studio.spec.ts; node node_modules/eslint/bin/eslint.js apps/web/e2e/music-studio.spec.ts
Pass. Root git diff --check: pass.

Coverage targets the reported 1470x730 clipping and existing phone layout. The browser fixture does not play or decode real audio; this change only removes music workspace height/overflow constraints. No native simulation, save, replay, network or generation changes; full native matrix omitted as unrelated. Map workspace selectors retain their existing behavior. Live deployment pending shared deployment lock.
