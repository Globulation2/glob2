# Evidence: web app design (multiplayer/web-design)

- `before/`: the web app as #518 shipped it (desktop and phone, light only).
- `after/`: commit 000474824 on top of master d8d2cd641, every page in both
  themes, desktop (1280 px) and phone (Pixel 7 emulation), from the e2e run.
- `axe-results.jsonl`: one line per page, project and theme (64 runs), tags
  wcag2a/aa, wcag21a/aa, wcag22a/aa and best-practice: 0 violations.
- `e2e-output.txt`: `npm run e2e -w @glob2/web`: 36 passed, 4 skipped (phone-only
  and desktop-only checks on the other project, and Watch in browser, which needs
  a browser game build).
- `npm-check.txt`: `npm run check` in platform/ (lint, typecheck, 41 files /
  344 tests) against PostgreSQL 16.

Commands (in platform/, with TEST_DATABASE_URL pointing at a PostgreSQL 16 superuser):

    npm run check
    npm run build -w @glob2/web
    SCREENSHOT_DIR=after AXE_REPORT=axe-results.jsonl npm run e2e -w @glob2/web
