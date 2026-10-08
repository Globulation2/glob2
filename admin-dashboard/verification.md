# Reviewed admin dashboard verification

PRs #949, #950, #952 and #953 deliver the four phases. Two independent sub-agents reviewed backend correctness and admin usability; their fixes are included in phase 4.

Final revision: `a40f3b32700b62c61b31a6ef4a9f465e1b5d2501`. Platform code tested at `9a17c549ea5095ba36f1efd5e3b405f67b1feaa0`; the final commit changes hosting documentation only. Current master `943bc48d43ed7537ea9c6688aaf8ac7f4c78160a` was fetched and integrated before final validation. Its shared studio services and additive draft-history migration overlapped this work; these were integrated rather than bypassed. No admin change touches simulation source or its version.

Updated phase revisions:

- Phase 1: `4099c22e8351d5e8961ad8f80fd27e9e3a980dfe`
- Phase 2: `19d2593f56992c576f4624715559f318a8b2b4a1`
- Phase 3: `2181640b50b359a3a5e49139d3b171d85116a4ab`
- Phase 4: `a40f3b32700b62c61b31a6ef4a9f465e1b5d2501`

## Verification

Ubuntu 26.04.1 Linux x86_64; Node24.19.0 bundled runtime; lockfile dependencies; PostgreSQL16.15 in Docker; TypeScript6.0.3, Vitest5.0.3 and Playwright Chromium. Production runtime uses the repository Node22 image.

Commands run in platform/:

```sh
npm run fixtures
npm run lint
npm run typecheck
npm test -- --maxWorkers=4
VITE_WEBSITE_URL=https://glob2online.com/ VITE_DOWNLOAD_URL=https://glob2online.com/downloads/ npm run --workspace @glob2/web build
SCREENSHOT_DIR=<evidence>/review-screenshots AXE_REPORT=<evidence>/review-axe-final.json npm run --workspace @glob2/web e2e -- --grep 'moderation pages for administrators only'
```

Full final run: **1025 tests passed,13 existing opt-in tests skipped;138 suites passed,3 skipped**. Lint, formatting, all backend/web/e2e types and protocol fixture generation passed. Logs: review-full-tests.log, review-final-lint.log, review-final-typecheck.log, review-final-fixtures.log.

Independent review regression runs:67 backend/API/billing/history/protocol tests and23 UI/chart/page tests passed. Refreshed phase1/2/3 smoke runs passed14/16/21 tests respectively, with full types and fixture generation. Final full-suite coverage includes upstream shared studio services and all reporting workers.

Desktop1280x860 and Pixel7 phone smoke checks visit all8 sections in light/dark themes. Final production-build screenshots and axe results are review-screenshots/ and review-axe-final.json. There are no axe violations; some automated color-contrast checks remain incomplete and the reviewers inspected both themes manually. Screenshots use disposable synthetic records, including sparse daily points, USD/JPY cash and metered uncertain recovery.

## Review defects fixed

- Exact PostgreSQL microsecond cursors prevent timestamp ties from skipping rows in six paginated lists, including existing shared match history.
- Account role changes lock/re-read the target and commit complete before/after audits atomically. Moderation-only audit access uses an exact action allowlist; UTC end-date filters include the chosen day.
- Failed/late provider metering remains available for recovery. Forms require inspected evidence, explicit usage and confirmation; Hive cache-write usage retains customer credit rules. Costs remain unavailable when cache-write monetary prices are unsupported.
- Itemized provider refunds replace covered cumulative journal snapshots without double counting, including concurrent and older arrivals. Partial disputes remain independent of refunds. Verified charge/event times replace checkout-start timestamps; observed dispute closure dates are labelled and replaced by verified closure events.
- Successful legacy staff guards and same-origin dashboard polling are excluded from account activity.
- Contribution snapshots replace only the state actually counted across collection pauses; repeated/late updates remain idempotent and source cleanup preserves anonymous daily totals.
- Both admin-first and studio-first database upgrades pass without renaming deployed migrations; missing applied migration history is still rejected.
- Admin screens and neutral URL/CSV/product helpers are separated. Charts preserve reporting bounds, show one-day observations and break missing-day lines. Completion averages include samples; tables distinguish cohorts, lifetime rankings, partial today and unknown costs, and use clear spacing/financial units.

Coverage also includes all six report libraries, concurrent resolution/recovery, access controls, deletion/export, UTC midnight/guest transitions, account activity replicas, retention, payment identities/modes/currencies and unknown historical facts.

## Performance

performance-review.ts seeds10000 private maps/reports,60000 credit ledger rows,10000 payment events and10000 uncertain requests in an isolated database. It samples each admin endpoint20 times and prints cold,median,p95 times. Analytics/finance medians include warm cache hits. review-performance.log includes EXPLAIN ANALYZE showing the bounded credit-period query using map_ledger_admin_period_idx. Full tests were running concurrently; these are representative local measurements, not a before/after benchmark or a production latency guarantee.

## Rollout and limits

Additive migrations and backend precede dependent UI, with a private database backup and preserved prior images. The scoped rollout updates API/worker, AI map/music TypeScript runtimes and Caddy; it preserves native binaries. Live verification compares dashboard totals against source aggregates and removes its temporary five-minute verification session. No destructive production tests or provider calls are performed.

No native simulation verification is required for this platform-only diff. Browser coverage is Chromium; Firefox/WebKit are not claimed. Existing large web chunk warnings remain. Expensive hosted checks were not requested; the repository accepts local evidence. Superseded PR workflow cancellations are not evidence of source failure; running master CI is left alone.

Historical gaps remain visible. Active users are never reconstructed from last_seen_at. Monetary provider rates are not configured on the live host: costs remain unavailable until verified immutable rates are configured. Credits are never treated as cash or summed across products.
