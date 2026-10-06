# Native, platform and performance checkpoint evidence

This bundle records historical validation and failures through candidate b3fab1a48, whose frozen CLI SHA256 is `7de08ee1bcbb74b7d711efd29ad84237a499efc4626055d61fb1bab1db0fbbc9`.

- `native-platform-b3fab.zip`: platform repairs at 4996a5f32, c3ca882ab and 9554357d4; initial and corrected focused tests; final native traces; resource-inspector UI captures and build/input provenance.
- `calibration-09b-governor.zip`: every row from the 09b priority calibration, its failed/inconclusive results and host context, plus the four successful governor restoration smokes and proposed stabilized protocol.
- `cache-diagnostics-b3fab.zip`: before/after checksum traces, exact-command instruction counters, readable profile reports, source patches and provenance. It retains the rejected distance-field experiment as well as the retained hard-space cache improvement.

These archives do not establish final acceptance. The 09b variable-clock Maxima case failed its CPU gate; Cortex remained inconclusive. The final candidate's performance-governor calibration had not begun when this bundle was prepared. No single all-green final full-suite or cross-platform claim is made. Per-file logs and provenance identify their tested revisions and any fixture-only repairs.

Executables, raw perf.data, redundant saved games and scratch profiles are excluded. Input saves are available in the separately published portable corpus; catalog sources are identified by Git revisions and hashes. Every retained checksum byte is included. Identical payloads are stored once per archive, with `file-inventory.json` mapping each original repository-relative path to its `stored_as` member, byte count and SHA256. `restore_evidence_paths.py` can reconstruct all original evidence paths into a new destination directory.

All archived payloads were decompressed and hash-verified. `summary.json` lists exact archive sizes, hashes, scope and limitations. This checkpoint is published by the coordinating agent; later acceptance results are reported separately.
