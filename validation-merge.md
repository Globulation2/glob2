# Merge validation

Tested commit: 69f605662422d347228cfa5154a23bdecd2e5fbf; integrated with base c7d688e9cb85ad481e7f883d83a2971946876400. No uncommitted source changes. Exact commands: merge-commands.txt. Platform and compiler: provenance-merge.json (Linux x86_64, GCC 13.3.0, existing SDL3 prefix, dev_fast=1, -O0 -g1, linker=auto).

Build succeeded: build-merge.log. Unit: 68 cases passed. Engine: 93 of 94 passed in the parallel campaign; hostile network fuzz exceeded default 120-second runner limit. Isolated rerun with --timeout 600 passed in 162.9 seconds. Both original timeout and successful rerun logs and JUnit are retained. Five Python trace-reader cases passed. git diff --check passed.

Coverage includes executor wait/tracing paths, stale handles and slot reuse, lanes/suspension/external completion, lifecycle joins, mode/thread save continuation, gradient preparation and backend switching, historical saves and turn/network replay verification. Earlier renderer cases are retained in engine-integrated-junit.xml. No intended game-feel, save-format or simulation-version change.

Nine Off/Waits/Trace x 1/2/4 worker configurations have identical per-tick checksums, replay bytes and final saves. Save continuation to tick 320 matches uninterrupted execution across all modes. games-merge.tar.gz contains the full initial/final saves, replays, checksum traces and result JSON; check_modes_merge.py reproduces the campaign. Common per-tick checksum SHA256: 787238e7b51d7fe0a946ab0b0b92c241f0272f1a57face8b680011f9be6244b6.

Performance samples and qualification are documented in validation.md and benchmark-summary.jsonl. No clock references in optimized disabled execution objects. These microbenchmarks preceded the last small empty-submit guard and do not establish zero final-code overhead. Full representative-game release performance, robust enabled overhead, and other-platform checksum equivalence remain unverified. Hosted PR checks were cheap contracts only. Maintainer instructed merge after receiving the implementation and performance limitations; evidence and omissions are explicitly recorded here.
