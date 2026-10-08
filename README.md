Verification for [PR #920](https://github.com/Globulation2/glob2/pull/920).

Tested commit: `d6444127bcd2c0b2166e31761c9c37074596c851`. Branch base: `0f1a2569ab7f23c8702a078978054f73f4ddb9cc`. Latest master: `d6953ef57aee3cae6b4fd7c2dce78209aa6250fe`.
The later master change only corrected a browser test; the merge tree is clean. Its updated Chromium editor rotation case passed against the final packaged client. The feature branch was not rebased merely for that test-only advancement.

macOS 26.6.2 arm64, Apple LLVM21, Node24.14.0 for platform checks and Node20.19.4 for browser bridge/master-integration runners, PostgreSQL16.15. Dependency versions, source hashes, results and omissions are in verification.json. Build logs retain compiler/includes/link flags. Test executables were rebuilt after the final commit; their own build-provenance record is included. The pre-existing untracked .playwright-mcp directory was preserved and excluded from the feature commit.

186 native cases passed, zero failures/errors/skips. 122 distinct selected platform/web tests passed; the6 workspace cases were repeated after final test-type corrections. Six desktop/phone real-engine cases and the updated master integration case passed.18 packaging and3 browser bridge checks passed. TypeScript, affected ESLint, production web build, translation audit and Unicode CLI regression passed.

Commands (repository root except where indicated):

```sh
artifacts/sets/verify-final-native.sh
scons target=web release=1 -j4
python3 -m unittest discover -s test/build_system -p test_browser_package.py
node --test browser/unit/set-preview.test.js browser/unit/quit-navigation.test.js
# From platform/ with Node24 on PATH; tests use the owned PostgreSQL55432 cluster:
npm run typecheck
npm run --workspace @glob2/web build
npx vitest run apps/api/test/sets.test.ts apps/api/test/catalog.test.ts apps/api/test/accountExport.test.ts packages/core/test/core.test.ts packages/core/test/engineJobs.test.ts packages/protocol/test/fixtures.test.ts apps/engine-agent/test/runners.test.ts apps/engine-agent/test/setValidation.test.ts apps/engine-agent/test/assetSandbox.test.ts apps/engine-agent/test/engineCli.test.ts apps/web/test/setModel.test.ts packages/db/test/schema.test.ts
npx vitest run apps/web/test/setsWorkspace.test.tsx apps/worker/test/reliability.test.ts apps/web/test/forms.test.tsx
PORT=4283 SET_E2E_ENGINE=1 npm run --workspace @glob2/web e2e -- sets.spec.ts
```

`verify-final-native.sh` contains the exact native suite selection and build command. Affected TypeScript/TSX files were passed to ESLint; exit0 is logged. The Unicode CLI fixture and report accompany the bounded error test. Latest-master responsive test source/config and its log are included. Native fixtures include custom maps/saves, screenshots, checksum traces and build provenance. Browser images show actual custom PNG rendering and mobile attribution/editor layouts.

Coverage follows the changed boundaries: catalog imports/updates and stocks, custom terrain/resource pixels, immutable scene snapshots, offline map/full-save round trips, legacy loading, replay/network acceptance and golden verification; API ownership/CAS, pagination, jobs/credits, and browser editing recovery. The initial native export hit disk exhaustion, then passed after removing inactive disposable test databases; no source compilation failure occurred.

Limits: Windows/Linux/Android/iOS native checksum equivalence and live Linux namespaces were unavailable. Sandbox routing/configuration and macOS fail-closed behavior were tested, but that is not a live deployment claim. Native dialog interface/localization received code/design review, without manual interactive authoring. Cheap hosted checks passed; expensive hosted checks were skipped under the normal draft policy. These omissions remain explicit; hosted green is not used as engine evidence.
