# Leader election crash fixture readiness

Head 51004306d09964cd3a7b10f511335f7102f49bd8; base fc3ae558d98cf3ee0ff148675160969bc29e4c57 fetched before final verification. Ubuntu26.04.1 x86_64, Node22.22.1, Vitest5.0.3, TypeScript6.0.3, Postgres16.15 (existing local test server, per-file isolated database/roles created and dropped by standard support). Production election behavior unchanged.

Hosted failure: c2ea87b78 fullmaster37287102079 platformjob111691073374, test terminates1session when it assumes2 after200ms. Log retained.

Controlled reproduction: temporary config and setup file under platform/artifacts/leader-readiness. The setup delays only the restarted leader fixture connection (third matching connect) by600ms. Original exact test fails with terminated1 expected2 (before-corrected.log); repaired finalhead passes identical delayed connection scenario (final-pressure-proof.log), with explicit delay-proof.txt confirming injection occurred. Diagnostics remain uncommitted. Initial config under root artifacts could not resolve platform node_modules; moved underneath platform before these checks. Successful Vitest output suppresses console, so explicit file proof was added for final verification.

From platform/:
```
GLOB2_DELAY_PROOF=<absolute evidence path>/delay-proof.txt npx vitest run packages/db/test/coordination.test.ts -t 'elects exactly one leader' --config artifacts/leader-readiness/vitest.config.ts
npx vitest run packages/db/test/coordination.test.ts
npx prettier --check packages/db/test/coordination.test.ts
npx eslint packages/db/test/coordination.test.ts
npm run typecheck
```
Final targeted controlled scenario1PASS; ordinary full coordination suite10PASS(2.21s); formatting, ESLint and both platform/web TypeScript projects exit0. Before original test and after repair use the same real Postgres server and per-file isolated databases.

Coverage rationale: test-only synchronization now observes two dedicated sessions before terminating them and requires a new leadership acquisition afterwards. Retains exact two-session termination and max-concurrent-one assertions, strengthens crash recovery instead of passing on the old callback. Other platform tests, hosted OS/browser/native matrix omitted because neither production TypeScript nor C++ simulation/build inputs change. Full master CI remains asynchronous; no known failure is waived.
