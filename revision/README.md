# Hive Mind review revision evidence

Source: [2a455ce28](https://github.com/Globulation2/glob2/commit/2a455ce283cc21004cc03f8ec23ed958ce9f135e).
Base remains PR #591, on the #516/#518 integration. Master fetched before final
validation: fe33142db. This revision supersedes the original UI and version-1 evaluation.

## Rendered UI

The two in-match screenshots use the normal game renderer after loading
maps/balanced.map, with seeded commander conversation and standing-order data.
They establish placement and rendering; they are not live online AI playtests.
Settings is the native screen; credit pages render the React application with
mock account responses. No real payment has been made. The original grass/logo
capture was a presentation fixture with no match loaded and was misleading.

## Checks

- Platform lint, format, typecheck and tests: 300 passed, 5 existing skipped.
- Nine native Hive Mind cases passed, including delayed lease/control replies,
  recoverable drafts, lost checkpoints, order boundaries and the map screenshot.
- Ten existing GUI/settings/game-speed cases passed in explicit desktop mode.
  Adaptive/touch detection initially selected incompatible layouts for desktop-only
  expectations; these results do not establish touch UX coverage.
- Six Chromium cases passed: four per-tick variants, committed online match and
  isolated-worker parity. All four 1500-tick traces and the 703-line committed
  match trace match native byte for byte (attached).
- Linux native game/tests and browser production builds passed.
- Version-2 evaluation uses the production Commander/database and native client
  scheduler/order queue. Across 20 commands each, all three candidates achieved
  100% generated-script validity with zero repairs. Completion: gpt-6-luna 20/20,
  gpt-6.1-sol 19/20, gpt-6-astra 19/20. gpt-6-luna is the lowest-cost qualifying
  candidate; see eval/selection.json for measured latency and cost estimates.
- Recurring scenarios include manual interference, and triggers must wake the
  commander after later changes and achieve the asserted action. Construction
  completion means establishing the requested site. Report-quality automation
  only checks obvious technical leakage, not subjective clarity or military feel.

## Remaining release gates

Production flags and sales remain disabled. A real multiplayer playtest is still
required for pacing, report usefulness and manual/automation interaction. Windows
and macOS containment/hardening, their replay coverage, non-Chromium coverage,
end-to-end Stripe test-mode checkout/webhooks and dedicated production credentials
and configured prices remain outstanding. These tests do not establish long-running
economic performance or prove absence of permission-boundary defects.

Reproduce using docs/multiplayer/hive-mind.md on the source branch. Live evaluations
require explicit development opt-in. No credentials are included here.
