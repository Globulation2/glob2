# AI Building Studio verification evidence

Tested source: `93e94f967cf7df34eec14f776b45276091cd9ac4`. Base: `285dd5b8f499f0925ddc42e0c2b34667a53704b8`; master integrated into the feature branch without conflicts. Evidence files are review artifacts on a dedicated branch, not shipped documentation.

Environment: macOS 26.6.2 (25G83), arm64; Apple clang 21.0.0 (clang-2100.3.34.2); Python 3.13.13; SCons 4.11.1; Node 22.22.1 for final runtime/browser/build checks. Static TypeScript/lint checks also passed with the locally installed Node 26 runtime. PostgreSQL 16.15, sharp 0.35.5, Vitest 5.0.3, TypeScript 6.0.3, locked npm dependencies. Native release build uses repository SCons configuration (-O3, -g, gnu++20); dependency link flags are in the native log.

Results: all commands exited 0. Service suite: 19 files, 102 passed, 1 skipped (opt-in live provider). Browser suite: 2 passed, desktop and phone; light/dark axe checks have no serious/critical findings. Typecheck, lint, production web build and native compilation passed. Native validator accepts the attached exact ZIP; its report is attached. The completed sprite uses the corrected elevated camera image. Construction sample was separately generated earlier; these samples were made through the image tool, not the worker's configured API.

Coverage: planner/schema, durable provider attempts, lease fencing, scoped assembly, team masks, native archive validation/repair, billing, ownership, references/quota, revisions/restoration, account export/delete and blob GC; browser revision-bound actions, responsive layouts and accessibility. Two independent code/UI agents completed two review rounds; their findings were fixed and rechecked. Documentation/configuration/comments were updated with the implementation.

Limitations: no live configured OpenAI worker call (credentials/model names unavailable); the opt-in test remains skipped. Docker daemon unavailable: Compose configuration checked, actual container build not run. No simulation rules changed, so cross-platform simulation/save/replay suites were not rerun. Native verification covers macOS arm64, not every supported platform. No claim of a maintainer gameplay session. User authorized merging after cleanup and review under the repository's author-owned verification policy.

Exact commands:

```sh
From the repository root:
PYTHONPATH=/opt/homebrew/Cellar/scons/4.11.1/libexec/lib/python3.14/site-packages /opt/homebrew/bin/python3.13 /opt/homebrew/bin/scons release=1 -j8 build/darwin/client/release/src/glob2

From platform/ (Node 22.22.1 on PATH for service tests):
BUILDING_NATIVE_BINARY=../build/darwin/client/release/src/glob2 BUILDING_VALIDATION_IMAGE=../artifacts/building-studio/real-ai-hospital-angle-v2.png BUILDING_VALIDATION_SITE_IMAGE=../artifacts/building-studio/real-ai-hospital-site.png npx vitest run packages/building-studio/test apps/ai-building-worker/test apps/api/test/buildingStudio.test.ts apps/api/test/buildingDrafts.test.ts apps/api/test/buildingLibrary.test.ts apps/api/test/buildingImages.test.ts apps/api/test/accountExport.test.ts apps/api/test/admin.test.ts packages/protocol/test packages/billing/test packages/db/test/schema.test.ts apps/worker/test/blobGc.test.ts
npm run typecheck
npm run lint

From platform/apps/web/ (Node 22.22.1 on PATH):
npx vite build
BUILDING_PREVIEW_IMAGE=<repository>/artifacts/building-studio/real-ai-hospital-angle-v2-game.png npx playwright test -c e2e/playwright.config.ts ai-building-studio.spec.ts
```

Native executable SHA-256 (binary not uploaded): `b0d0230d6e311ea18c596b64f93109fc3ccaec8df2045807dbaed86872b434b8`.
