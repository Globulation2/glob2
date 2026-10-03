> Current master integration validation: see [local-merge-validation](local-merge-validation/README.md).

> Current master integration validation: see [master-integration](master-integration/README.md).

> Current final usability and merge validation: see [merge-readiness](merge-readiness/README.md). Older captures and smoke results below are superseded where noted.

> Superseded UI and version-1 evaluation: see [review revision evidence](revision/README.md) for current screenshots, production-harness evaluations and validation.

# Hive Mind validation evidence

Source: [Hive Mind stack 45f90bd81](https://github.com/Globulation2/glob2/commit/45f90bd813fa9ba10f99be5b440279d1aedba9f8).
Base: multiplayer/staging 9a765e2f3 (PR #516), including web application PR #518;
master fe33142db. The final stack was rebased without code changes after validation.
These are development results, not production release approval.

## Results

- Platform lint, format, typecheck and tests: 297 passed, 5 existing skipped.
- Focused native Hive Mind tests passed; the evaluation fixture is inactive without its input environment.
- Existing native JavaScript suite: 54 cases in 36 groups passed.
- Chromium: four replay variants, committed online match verification, isolated Hive worker parity passed (six tests).
- Chromium serial and threaded shared scripting corpora: two tests passed.
- Native and all four browser 1500-tick replay traces are byte-identical. Native/browser committed online match traces also match (703 lines).
- Three live model candidates each achieved 20/20 first-attempt valid programs, 20/20 bounded objectives, no repairs. gpt-6-luna selected on measured estimated cost; see eval/selection.json for latency and cost bounds.
- Commander screenshot is a rendered fixture, not a manual live match playtest.
- Browser static packaging integrity validation passed, including the isolated worker assets.

## Reproduction

Use docs/multiplayer/hive-mind.md on the source branch for configuration and credentials policy.
Run npm --prefix platform run check with PostgreSQL available; build native game/tests using SCons.
Run test/run_tests.py with HiveMind and JavaScript filters.
Build browser and web-tests via SCons, then run browser Playwright Chromium determinism and hive-worker tests.
The browser corpus requires the web-tests build. Build dependencies, compiler and platform must match before reusing binaries.
Run the explicitly enabled development evaluation runner and model selector documented in the guide.
No credentials are included in this evidence branch.

## Outstanding release gates

Windows/macOS OS-level worker containment still requires implementation/hardening and verification;
Linux provides the tested syscall allowlist. Windows/macOS and non-Chromium compatibility are unverified.
Real multiplayer playtesting of pacing/manual interaction, end-to-end Stripe test-mode webhooks/checkout,
dedicated production credentials and configured prices remain required.
Production feature and sales flags remain off. The bounded construction suite establishes sites;
it does not establish long-running economic performance. Evaluations are not a security proof.
