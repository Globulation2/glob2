# Maxima renewable wood capacity verification

Tested PR head: `68f671be7a686218b104de971cb9d198f3b48d72`. Base: `985b78d7d434aff8dbabd89035f273819e96df6c`. PR: https://github.com/Globulation2/glob2/pull/1338

The final clean Linux/GCC fast build passed 73 focused engine cases and 75 Python tests (one standalone-client CLI test skipped). Broader native coverage validates 184 unique current cases across identical production source: the earlier broad run passed 181 cases, the corrected clearing fixture passes at final head, and both long feeding fixtures passed isolated 600-second-budget runs in 86.171 and 101.15 seconds. The original failures/timeouts are retained, not counted as passes. Only the clearing test file changed after reviewed production commit `0562d2286d`.

`accepted-verification.json` records exact commands, source/binary provenance, case inventory, coverage and limitations. `independent-review.md` records the review findings and final audit. The JUnit files retain each selected case; `current-match-*` retains a verified replay and per-tick checksums. Full build logs are compressed. Reproducer scripts and exact argv receipts are included.

Cross-platform Maxima checksum comparisons, release performance, comparative AI effectiveness and human gameplay remain unverified. Hosted predictive checks are tracked on the PR independently of this local evidence.
