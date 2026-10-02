# Memory optimization evidence for PR 577

The measured implementation was validated before rebasing onto current master. Source commit: 0ad379bd3; final persistence binary SHA256: 54a1a91b347537d045a5355972c0d603fece361d1a53f32a4c7d51ec5b94fe6c. Current-master integration and review results are reported separately in the PR.

Start with artifacts/memory-optimization/review.md and peak-followup/review.md. JSON reports retain exact commands, fixture and executable hashes, paired CPU observations and save/checksum comparisons. JUnit and raw test logs back the test claims.

This compact bundle includes the initial fixture, a full matched early-game continuation checksum stream, and its final save/replay. Larger mid/late saves and the multi-gigabyte full-game checksum stream remain in the local ignored artifacts directory; their hashes/comparison results are included here, not the large binaries themselves. Review limits: native macOS arm64 only; no Linux/Windows execution or interactive maintainer playtest.

No generated evidence is part of the implementation branch. evidence-manifest.json lists payload hashes.

Final current-master integration and review: integration-review.md and independent-review.md. Original validated source is preserved in this evidence branch history as commit0ad379bd3.
