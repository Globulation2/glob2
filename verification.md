Local / VM verification

- Tested commit: `a055ba04707e3547f95a21a47cd47c1841218e35`.
- Base: `3bbd470baf3bc172871fcc6500d0f04674ee90b2`. PR head includes the fetched master, including the shared material registry, shader tooling, and migration 0045. Conflicts were resolved before this validation; migration 0046 is additive.
- Environment: macOS 26.6.2 (25G83), arm64; Node 24.14.0; PostgreSQL 16.15; Playwright 1.63.0 Chromium desktop and Pixel 7 viewport.
- Dependencies/configuration: package-lock.json dependencies installed with `npm ci --ignore-scripts`; local PostgreSQL at port 55432; Vite production build, no native compiler flags. Commands below use Node 24 in PATH.
- Coverage rationale: independent working designs, account ownership/export/deletion, migrated drafts, revision conflicts, acknowledged application, historical texture preservation, offline recovery, guest sign-in, bounded preview pagination, pointer behavior, responsive layouts, themes, and moderation.

Exact commands, run from platform/ unless stated:

```sh
npx vitest run apps/api/test/skinCollection.test.ts apps/api/test/skinPublishing.test.ts apps/api/test/skinDrafts.test.ts apps/api/test/skins.test.ts apps/api/test/skinModeration.test.ts apps/api/test/accountExport.test.ts packages/db/test/schema.test.ts apps/web/test/skin-document.test.tsx apps/web/test/skins-workspace.test.tsx apps/web/test/skin-projection.test.ts
npm run typecheck
# From platform/apps/web:
../../node_modules/.bin/vite build
# From platform/:
npm run e2e -w @glob2/web -- skins.spec.ts skins-studio-ux.spec.ts
# Changed TS/TSX/CSS paths, relative to platform, supplied through lint-files.txt:
xargs npx eslint < ../artifacts/skin-collection/lint-files.txt
xargs npx prettier --check < ../artifacts/skin-collection/lint-files.txt
# From repository root:
git diff --check origin/master
```

Results: all commands exit 0; 57 focused tests in 10 files, 36 desktop/phone browser cases, full platform/web/e2e typecheck, production build, ESLint and Prettier pass. ESLint emits its standard ignored-CSS warning; Prettier validates CSS. Vite emits existing large-chunk guidance.

An initial integration run exposed the migration-count assertion (23 instead of 24); the assertion was corrected and all focused tests rerun. An initial browser launch overlapped the build clearing dist/index.html; the browser suite was rerun after the build completed. These final logs show the sequential successful run.

Limits: headless Chromium, no Safari/Firefox or physical pen/tablet. No native builds/checksums: simulation, game-save bytes, replay/network version gates and signed manifests are unchanged. No deployment or expensive hosted matrix requested. Hosted cheap checks may remain queued at merge under repository policy.

Evidence in this dedicated branch is generated review material, not part of the implementation branch. Collection screenshots below are from the tested final production build. Earlier screenshot files are omitted.

Maintainer acceptance: merge explicitly authorized by genixpro through the user request; prepared and validated by Codex under repository author-merge policy.
