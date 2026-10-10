# Free launch merge verification

Tested commit: 6887a2f6aa8e192b18c9e24b8ac6ddd7423e3636
Integration base: 5443f425e0ff00065457ce8baa68238557db49f6
Original implementation base: 7d17f40d156fa847cc8e585907aee1d13ac6fdd0

Environment: macOS 26.6.2 arm64, Apple clang 21.0.0, Node 24.14.0, Postgres 16.15.
Dependencies installed from the integrated package lock using npm ci --ignore-scripts --no-audit --no-fund.
Native development flags: dev_fast=1 linker=auto client_profile=free -j2; resolved features Commander=0, authoring_links=0, community_ai=1, community_generators=1. Native dependency identities and flags match the preceding compile. The source was integrated with current master because its App.tsx design-system update overlaps this change; integration completed without conflicts.

Focused Python contracts: 156 tests passed. Commands, from the repository root:
```
TMPDIR=/private/tmp python3 -m unittest discover -s test/build_system -p 'test_client_features.py'
TMPDIR=/private/tmp python3 -m unittest discover -s test/build_system -p 'test_layout.py'
TMPDIR=/private/tmp python3 -m unittest discover -s test/build_system -p 'test_dev_build.py'
TMPDIR=/private/tmp python3 -m unittest discover -s test/build_system -p 'test_free_cohort.py'
TMPDIR=/private/tmp python3 -m unittest discover -s test/build_system -p 'test_downloads*.py'
TMPDIR=/private/tmp python3 -m unittest discover -s test/build_system -p 'test_*release*.py'
python3 -m unittest discover -s test/build_system -p 'test_ci_concurrency.py'
```

Native: the 15 selected free-profile production/test objects successfully passed the refreshed SCons check. The exact command and each target are recorded in merge-native-compile.log; preceding compiler commands/results are retained in free-native-affected.log. This establishes compilation, not native test execution or gameplay qualification. The earlier cold full native build was interrupted during conversion of thousands of unchanged runtime artwork files.

Refresh results: all recorded commands exited successfully. Python contracts: 156 tests; native: 15 targeted objects; platform: 23 files / 120 tests. Typecheck, ESLint/Prettier and the 33-language translation inventory/catalog check all passed.

Platform tests: 23 files / 120 tests passed. Platform commands, from platform/, using Node 24.14.0 on PATH:
```
npm run typecheck
npm run lint
npm run i18n:check
npx vitest run --maxWorkers=2 apps/api/test/publicFeatures.test.ts apps/api/test/publicShutdown.test.ts apps/api/test/studio.test.ts apps/api/test/aiStudioRoutes.test.ts apps/api/test/generatorStudioRoutes.test.ts apps/api/test/musicStudio.test.ts apps/api/test/terrainStudio.test.ts apps/api/test/buildingStudio.test.ts apps/api/test/skinCollection.test.ts apps/api/test/skinDrafts.test.ts apps/api/test/skinPublishing.test.ts apps/api/test/skinWebhook.test.ts apps/api/test/accountExport.test.ts apps/api/test/hive.test.ts apps/web/test/features.test.ts apps/web/test/pages.test.tsx apps/web/test/forms.test.tsx apps/web/test/ais.test.tsx apps/web/test/generators.test.tsx apps/web/test/generatorStudio.test.tsx apps/web/test/musicStudio.test.tsx apps/web/test/studio.test.tsx apps/web/test/skins-workspace.test.tsx
```

Coverage rationale: profile resolution and isolation, legacy/staged publication contracts and workflow guards; disabled direct API mutations/checkout, retained payment callbacks and persisted community/account data; enabled studio regressions; hidden navigation/direct bookmarks; integration with the new website shell. No runtime dependencies are added by this PR; the design-system dependency comes from the integration base. Simulation computations and save/replay formats are unchanged.

Omissions: native executable/runtime tests, native gameplay, full release asset export, Windows/Linux/Android/iOS/browser game builds, signing/notarization, store/real-device acceptance, production deployment, external download-site schema-2 consumption and cross-platform simulation/save/replay/network qualification. These remain required for selected release packages before publication; the PR adds no successful qualification attestations.

Earlier broad build-system coverage (450 tests) encountered two existing FFmpeg/libjxl environment failures and a macOS /var versus /private/var path assertion. The focused development tests passed with TMPDIR=/private/tmp. Those original failures are retained in build-system-tests.log and are not claimed fixed.

No live configuration, store listing, public release or version tag was changed.

## Evidence files

- [merge-contracts.log.gz](merge-contracts.log.gz)
- [merge-native-compile.log.gz](merge-native-compile.log.gz)
- [merge-platform-tests.log.gz](merge-platform-tests.log.gz)
- [merge-npm-ci.log.gz](merge-npm-ci.log.gz)
- [merge-typecheck.log.gz](merge-typecheck.log.gz)
- [merge-lint.log.gz](merge-lint.log.gz)
- [merge-i18n.log.gz](merge-i18n.log.gz)
- [free-native-affected.log.gz](free-native-affected.log.gz)
- [build-system-tests.log.gz](build-system-tests.log.gz)
