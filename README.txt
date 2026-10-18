PR #1222 verification evidence
Source: 0e36aee51be98cc297d3f6a72fe6438209d0eb9a
Base: 64c792a61c4b5a7cdbfc49f33e7c846127645056
verification-evidence.zip: macOS commands, source identity, logs, saves and traces.
linux-callgrind-evidence.zip: clean before/after profiles, Linux release build/test logs, traces and saves.
linux-callgrind-summary.json adds inclusive hot-path and exclusive remaining-work group summaries.
Earlier checksum-diagnostic profiles are excluded from the performance comparison.

Operation-local demand/quota reuse experiment against PR head 0e36aee51:
reuse-prototype.patch: tested implementation, two regression tests and architecture note; uncommitted.
reuse-summary.json: before/after instructions and compatibility results.
linux-reuse-evidence.zip and macos-reuse-evidence.zip: profiles, builds, tests, checksums and saves.
