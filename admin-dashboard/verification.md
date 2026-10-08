# Admin dashboard verification

Four stacked draft PRs: #949, #950, #952, #953. Phase commits:

- Phase 1: `ca976ab06a901499809d0bc75fece1f49c83fa6d`
- Phase 2: `f04dad2e4a01c3ffd425fc0a4bd612d9d153338f`
- Phase 3: `62df82ca9b9a85dc349d1c9457ef2778fd7e5104`
- Phase 4: `9bb8a0faebfffbd5596a0b36d4317f69c11cc299`

Initial base: `68aa1075b365efab78083fe5587051e689cb7ed1`. Phase 2/3/4 bases are the previous phase commits.
Fetched master before final validation: `37213ca3cb2dc806a491a77a525323464b96a859`. Intervening changes concern engine checksum references, not the platform components changed here. No merge/rebase was needed.

## Environment and commands

Linux x86_64 Ubuntu host, Node v24.19.0 (bundled official runtime), npm lockfile dependencies installed with `npm ci --ignore-scripts`; PostgreSQL 16 Docker on localhost:55432, isolated per-suite databases and API/worker database roles. TypeScript 6.0.3, Vitest 5.0.3, Playwright Chromium (desktop 1280x860 / Pixel 7 phone). Runtime image uses repository's official Node 22 image.

Commands in `platform/`:

```sh
npm run fixtures
npm run lint
npm run typecheck
npm test -- --maxWorkers=4
npm run --workspace @glob2/web build
SCREENSHOT_DIR=<evidence>/screenshots AXE_REPORT=<evidence>/axe-release.json npm run --workspace @glob2/web e2e -- --grep 'moderation pages'
```

Full platform run: 133 suites passed, 3 skipped; 969 tests passed, 13 skipped. The final credit-return reporting query and currency-minor-unit summary were then covered by 13 focused API/billing tests, final lint/typechecks, production build and final browser checks. `tests-release.log`, `finance-returned-final.log`, `phase4-lint.log`, `phase4-typecheck.log`, `build-release.log`, `browser-release.log` record those results.

Independent phase checks: phase 1 24 tests; phase 2 33 tests; phase 3 31 tests. Each phase had backend/web types, lint and freshly generated protocol fixtures. See phase-specific logs. Focused phase commands are listed in PR comments.

Browser check visits all eight admin sections in both themes, checks axe accessibility and screenshots on desktop/phone. Both tests passed. Screenshots represent synthetic seeded accounts/content, not production private data.

## Risk coverage and limits

Coverage includes all six moderation libraries and independent restore, timestamp/ID pagination, authorization, concurrent resolution/recovery and audits, deletion/export activity fencing, duplicate activity across replicas, UTC midnight, guest transitions, retention/rollup reruns, payment duplicates/out-of-order cumulative refunds and refund identities, dispute changes, currencies/modes and missing estimated-cost coverage.

No native simulation code changed, so native checksum/save/replay/platform builds are not applicable. Hosted expensive CI was not requested. Browser coverage uses Chromium only; no claims are made about Firefox/WebKit. The 13 skipped tests are existing opt-in native-engine/hardware/provider tests; no live provider calls or destructive production tests were performed. Existing large frontend chunk warnings remain.

## Performance

`performance.ts` creates 10,000 synthetic private maps/reports in an isolated database and samples each bounded admin endpoint 20 times. `performance-release.log` reports median and p95. Analytics/finances measurements include cached requests. This is local representative evidence, not a before/after regression benchmark or production latency guarantee. Full platform tests were running concurrently.

## Operational limits

Historical gaps are shown explicitly; active users are not reconstructed from last_seen_at. Cash remains separated by currency/mode and credits by product. No monetary provider rates are configured on the live host, so costs are labelled unavailable until immutable provider rates are configured. Missing amounts are never inferred from customer credits or current prices.

Deployment uses additive migrations before backend/UI, a database backup and preserved prior image IDs. Running engine/relay images are retained. Live verification compares attention and financial totals to read-only source aggregates; an ephemeral five-minute admin authentication session is removed afterward. Live details will be recorded in `live-verification.log` once deployment finishes.
