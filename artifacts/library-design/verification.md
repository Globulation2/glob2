# Unified library verification

Implemented in the uncommitted working tree based on `2c37ebf45e7b8ac2986f9ebd27fe2b03f06a810a`. Fetched master `3f67899b0899780efb4c309d111852b1e9f267ff` before final validation; it contains no intervening web, i18n, or affected guide changes. Exact source hashes are in [source-manifest.json](source-manifest.json); [implementation.patch](implementation.patch) includes new files.

Environment: macOS / Apple Silicon, Node 24.14.0, lockfile dependencies installed with `npm ci`, PostgreSQL 16.15 in an isolated data directory on port 55432, Playwright Chromium desktop and Pixel 7 emulation. No runtime dependencies changed. System FFmpeg had a broken dylib reference; isolated development-only `@ffmpeg-installer/darwin-arm64@4.1.5` and `@ffprobe-installer/darwin-arm64@5.0.1` supplied FFmpeg 4.4 / ffprobe 5.0.1. These tools are contained in this ignored evidence directory.

Commands run from `platform/`, with Node 24 prepended to PATH:

| Command | Result | Log |
| --- | --- | --- |
| `npx vitest run apps/web/test` | 40 files, 287 tests passed | [web-tests.log](web-tests.log) |
| `npm run typecheck` | Passed | [typecheck-final.log](typecheck-final.log) |
| `npm run lint` | ESLint and Prettier passed | [lint-final.log](lint-final.log) |
| `npm run i18n:check` | 33 languages × 2834 messages passed; generated launcher current | [i18n-final.log](i18n-final.log) |
| `npm run build -w @glob2/web` | Production build passed; existing chunk-size warning | [build.log](build.log) |
| `MUSIC_E2E_PROCESSING=1 SCREENSHOT_DIR=<screenshots> npm run e2e -w @glob2/web -- libraries.spec.ts buildingLibrary.spec.ts ais.spec.ts sets.spec.ts music.spec.ts` | 41 passed, 1 intentional skip (worker integration is desktop-only) | [browser-verified.log](browser-verified.log) |
| `SCREENSHOT_DIR=<screenshots> npm run e2e -w @glob2/web -- skins.spec.ts --grep 'designs save automatically\|collection duplicates\|large collections'` | 6 passed | [browser-skins-final.log](browser-skins-final.log) |
| `npx tsc -p apps/web/e2e/tsconfig.json` | Final test edits passed | [e2e-typecheck-final.log](e2e-typecheck-final.log) |
| `npx eslint apps/web/e2e/libraries.spec.ts apps/web/e2e/music.spec.ts` | Final test edits passed | [e2e-lint-final.log](e2e-lint-final.log) |
| `npx prettier --check apps/web/e2e/libraries.spec.ts apps/web/e2e/music.spec.ts` | Final test edits passed | |
| `git diff --check` | Passed | |

Browser runs prepend the isolated FFmpeg and ffprobe directories to PATH as well. PostgreSQL was initialized with `initdb -D artifacts/library-design/pg -A trust -U glob2` and started with `pg_ctl -D artifacts/library-design/pg -l artifacts/library-design/pg.log -o '-p 55432 -h 127.0.0.1' start`; default test harness URL targets this port. The temporary instance was stopped after verification.

[Comparison gallery](comparison.html) links all six libraries at 1470px, 900px, and 390px in both themes. Screenshots use real seeded Maps/Buildings and representative synthetic Terrain/AI/Music fixtures; Skins includes the stock card and tested real personal collections. Loading, empty, retryable errors, and filtered-empty screenshots are in [screenshots/](screenshots/). The matrix checks element bounds as well as document overflow, headers and art, and axe violations on desktop/phone in both themes. Axe found no violations. Shared CSS preserves visible theme focus and disables hover movement for reduced motion.

Behavior coverage includes trimmed live search, Enter, reset, secondary filter count, collection selection, cancellation of superseded music results, AI URL/back/focus restoration, building release availability/download/fork/withdrawal, terrain drafts and PNG persistence, music playback/fades/loop/error/retry and real worker publishing/bulk ZIP download, skin save/conflict/duplication/deletion/equipment and six-item pagination. Initial playback browser failures were obsolete `Play` selectors; they were updated to the existing `Play music` label, then the final run passed. Earlier failures and logs are retained locally for diagnostics.

Limitations: Chromium only; phone coverage is emulation rather than a physical device. No Firefox/Safari or hosted CI run. No native engine checks were needed because simulation, APIs, database contracts, dependencies, studios and detail implementations were not changed. Visual QA is backed by screenshots; maintainer review of the feel remains useful. Changes are uncommitted and no pull request or deployment was created.
