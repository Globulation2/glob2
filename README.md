# Stripe webhook verification

Tested source: `cbabd290863d75815b11aada6990a0e990863e64`. Base: `da99df96df914b778a3ea994ede277c27a2c7760`. Branch starts from this base without a merge/rebase after validation. No engine or runtime dependency changes.

Environment: macOS 26.6.2 arm64; Node 24.14.0; npm lockfile installed with `npm ci --no-audit --no-fund`; PostgreSQL 16.15 (Homebrew), fresh task-specific cluster bound to localhost port 55437. Database roles/migrations run by the repository test harness. No deployment database used.

Commands (from platform/ with Node 24.14.0 on PATH):

```sh
npm ci --no-audit --no-fund
TEST_DATABASE_URL=postgres://glob2@127.0.0.1:55437/postgres npm test -- apps/api/test/skinWebhook.test.ts apps/api/test/skinBilling.test.ts apps/api/test/skinStripe.test.ts apps/api/test/creditWebhooks.test.ts packages/hive/test/checkout.test.ts packages/billing/test/products.test.ts
npm run typecheck
node_modules/.bin/eslint apps/api/src/hive/routes.ts apps/api/src/maps/studio.ts apps/api/src/skins/billing/routes.ts apps/api/test/creditWebhooks.test.ts apps/api/test/skinWebhook.test.ts packages/billing/src/checkout.ts packages/hive/test/checkout.test.ts
node_modules/.bin/prettier --check apps/api/src/hive/routes.ts apps/api/src/maps/studio.ts apps/api/src/skins/billing/routes.ts apps/api/test/creditWebhooks.test.ts apps/api/test/skinWebhook.test.ts packages/billing/src/checkout.ts packages/hive/test/checkout.test.ts
```

All commands exited 0. Six files / 27 tests passed; full platform typecheck passed; changed-file lint/format and `git diff --check` passed.

Coverage: signed webhook raw bytes/mode/duplicates; skin grants and billing; all seven credit products across four checkout event types and refund notifications reaching the skin route; paid skin payment reaching Hive is ignored before credit reconciliation; legacy Hive fulfillment/reversals and product isolation; both Hive/map invalid signatures return 400. Tests use signed fixture payloads and mocked Stripe SDK reads, with real PostgreSQL migrations and ledger operations.

Regression proof: baseline-tests.log runs new skin/signature tests and the initial Hive fixture against unmodified runtime sources from base (4 failures). Strengthened paid-skin Hive fixture independently fails against base billing checkout (baseline-hive.log). Sources were restored before final passing verification. Baseline failures are expected and retained as evidence.

Limits: no live Stripe card completion, refund/dispute delivery, delayed-method/3DS end-to-end verification or deployment. No full platform test suite or native platform matrix: changes are TypeScript payment routing only, with no simulation/save/replay/network changes. No gameplay feel change.
