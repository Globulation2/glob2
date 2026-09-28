# Final lazy building-gradient path

Implementation: `c0c24b5825ade417b88bf9c1d431bbbf830c5aea`. This removes the experimental CLI/environment toggles, the boolean rebuild argument, and the eager-building fallback. All game entry points use resumable building fields. The general full-field solver remains for resource and round-trip gradients and shares the expansion kernel. Duplicate switched CI invocations are removed; normal regression runs now cover lazy behavior. The paused-field regression constructs its frontier through a normal gameplay query.

## Validation

Native macOS release client and harness builds pass. Gradient oracle, building invalidation, immobile-unit, clearing-flag and save-safety regressions pass without gradient-mode configuration. The removed CLI option is rejected as unknown. Four retained games (65,442 ticks) match the original eager baseline's per-tick checksum sidecars and initial/final save bytes. Another 1,024 ticks from the original saved game match checksums and final save bytes. No save/replay/network format changes or intended gameplay/pacing changes.

`tests.json`, `validation.json`, `continuation.json` and `removed-option.json` retain commands, results and hashes. Archives include the new per-tick checksum files, result files and logs. Save bytes are identical to the already-published [initialization validation saves](https://github.com/Globulation2/glob2/tree/9c56176c6d8a504811cbf22468dde24940fc01a3/field-initialization); use the hashes in these records to verify the corresponding initial/final saves in those archives. They are not uploaded a second time here.

Build logs retain an initial invocation that used the wrong clearing harness target name and failed, followed by the corrected successful build and final incremental build. No production source error caused that failed invocation.

This consolidation was validated for correctness; it was not used to claim an additional speedup beyond the earlier lazy-propagation and single-pass initialization studies. Those retained reports identify their exact source commits and experimental options. Their mode switches no longer exist in the final implementation.

Full-game cross-platform checksum equivalence and interactive play remain unverified. CI runs separately on the final commit. Maintainer playtesting remains part of review. The earlier Maxima save-diagnostic caveat is not fixed by this change.
