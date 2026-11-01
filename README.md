# Maxima renewable wood capacity verification

Final tested PR head: `6185769c4fd567bc8fb10fb18a687fca59ff8440`. Refreshed integration base: `8d5293c436c1a65e8ad47459e738152644fcf137`. PR: https://github.com/Globulation2/glob2/pull/1338

Final integrated validation: **82 engine cases passed**, **75 Python tests passed, one CLI-only test skipped**, and the revision119 golden record/simulation gate passes. The final merge resolution was independently reviewed with no unresolved findings. See `integration-verification.json` for exact commands, inventory, clean source/binary provenance and limitations; `integration-review.md` for the final audit. `integrated-current-match-*` retains a verified replay and per-tick checksums.

Earlier files remain as clearly versioned **pre-integration supplemental evidence**: `accepted-verification.json` identifies its older head/base, the broader native runs retain original failures/timeouts, both isolated feeding fixtures passed, and review-driven fixture corrections were validated. These earlier results are not represented as final-base qualification.

Limits: native Linux/GCC fast development only; no cross-platform Maxima checksum comparison, release performance qualification, comparative effectiveness tournament or human playtest. Hosted predictive CI is tracked independently on the PR.
