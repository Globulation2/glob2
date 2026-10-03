# Hive Mind final usability and merge validation

Source [efe60cc6a](https://github.com/Globulation2/glob2/commit/efe60cc6ad9569e8368f85f61ab20b069e646c86), including
online staging d4a996786 and its staged browser asset loading. Master was fetched
before validation and remains fe33142db.

Independent usability review verified the final screenshots and event paths.
All five findings were fixed: missing Settings strings, stop shortcut submitting
the draft, absent mouse/touch Stop, inaccessible earlier reports, and silent rapid
follow-up submission. Key-release bindings and UTF-8 preview boundaries are covered.
Screenshots use a normally loaded map with seeded commander data, not a live AI
multiplayer playtest. The Settings capture uses the native Settings screen.

## Regression coverage

Eleven new backend tests cover duplicate and concurrent wake delivery, stale and
paused/replaced alerts, zero-credit backlogs, catch-up after restart, cancellation,
minimum wake interval, reservation ownership/rate tampering, over-reservation
reconciliation, pinned rates, and late model responses after stop. A regression
exposed duplicate wake deliveries consuming another rate-limit slot and reporting
twice; wake receipt journaling now precedes those effects.

Native tests cover pending drafts, delayed replies after leaving a match, stop
failure/retry/deduplication, keyboard stop before submit, release-triggered bindings,
mouse Stop preserving installed automation, report history and UTF-8 truncation.
Browser/native worker parity includes malformed snapshots and wrong-typed input;
the isolated worker links with the exception mode used by staged browser builds.

## Validation

- Platform lint, format, typecheck and tests: 311 passed; 5 existing skipped.
- Focused native Hive Mind: 10 cases passed. Existing JavaScript compatibility,
  save/load and scripting cases passed; see the attached selected-case log.
- Native Settings layout/localization passed in explicit desktop mode.
- Linux game/tests and staged browser runtime builds passed.
- Browser unit tests: 34 passed. Browser packaging tests: 12 passed, 1 skipped.
- Strict translation audit: zero structural errors; new translation entries use
  registered English fallbacks while awaiting translation.
- Attached Chromium results cover worker recovery, per-tick determinism, committed
  match verification and staged asset loading. The four 1500-tick native/browser
  traces and the 703-line committed match trace match exactly.

The earlier version-2 live-model evidence remains in ../revision/eval; it has not
been rerun after these UI/reliability changes. The supported API/prompt are unchanged.
GitHub-hosted checks were queued at merge preparation; these local results must
not be described as completed Windows/macOS CI coverage.

## Scope of merging

All six Hive Mind PRs are merged into multiplayer/staging; its file tree matches
the validated source exactly. See merge-result.json for the merge commits. This is not
a merge of the separate online foundation stack into master or production
activation. Feature and sales flags remain off. Broad release still requires a
real multiplayer playtest, Windows/macOS containment and compatibility coverage,
non-Chromium validation, end-to-end Stripe test-mode evidence, dedicated production
credentials and configured prices. No simulation/save-format changes were made
by this usability pass.
