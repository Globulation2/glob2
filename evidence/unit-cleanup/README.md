# Configurable units cleanup verification

Tested final PR head: `32c7ec892b5a1dfb22bde2d9025de106e8eac4d3`. Rebased onto master `c6a7eae787a0f945619a622ed99ac755c2632689`; immutable tree equals the clean merge of the previously tested cleanup revision with that master. Source remained clean through execution.

Linux x86_64, GCC15.2, ordinary release build (`release=1 dev_fast=0 linker=default`, six build jobs), O3 and default stripped linking. Build logs and test provenance retain exact compiler flags, dependency paths, source identities and commands. Display checks use Xvfb/Mesa. No runtime dependencies added by this refactor.

## Actual final results

- `build-current`: exit0.
- `engine-current`: 125 passed, 8 declared skips, no failures/errors.
- `unit-current`: 917 passed, 24 declared skips, no failures/errors.
- `assets-current`: 19 passed, 0 declared skips, no failures/errors.
- `presentation-current`: 2 passed, 0 declared skips, no failures/errors.
- `cli-current`: 15 passed, 0 declared skips, no failures/errors.
- `cli-reference-current`: exit0.
- `traces-current`: exit0.
- `package-optimization-current`: 6 passed, 0 declared skips, no failures/errors.
- `android-release-current`: 16 passed, 0 declared skips, no failures/errors.
- `play-release-current`: 10 passed, 0 declared skips, no failures/errors.

Four canonical replay/match/resource/unit traces equal the frozen pre-cleanup producer `9e6f406b634023702eb34125916f74fe453a5e54` as whole raw bytes, without normalization. Individual hashes and trace bytes are included. These are fresh final-head executions; earlier full-state/platform comparisons retain their original source identities.

Independent static reviews cover the cleanup and intervening master integration. The earlier cleanup head `00f50c5ff1b1da77254b4057c3e04059917cc5a8` passed125 engine cases,915 unit cases,15 CLI cases and four equal traces before the final rebase. Raw receipts remain attached separately.

## Cleanup and retained evidence

Removed campaign-only continuation/full-state tracing, owner-thread benchmark counters, the unit benchmark wrapper and extra gates, instrumentation-only tests and CLI metadata. Durable composition/ablation fixtures, compatibility tests, ordinary checksums and developer documentation remain.

Task-owned captures, old/current build trees, private test profiles, SDL staging and working notes were removed after validation. Foreign worktrees, profiles, dependency caches and evidence branches were preserved. The local one-off cleanup runner/evidence directory is removed after publication; the remote evidence is intentionally retained for review.

Original failed invocations remain in the archive: an old incompatible build-directory attempt, an original source guard that mistook a moving shared master ref for source mutation, and an invalid unittest module invocation. Corrected final invocations passed. No historical failure receipt was relabeled.

## Coverage limits

This publication establishes final cleanup execution on Linux only. Prior GCC/Clang, macOS/browser and complete-state proofs are in the [original foundation evidence](../FINAL45-README.md) with their original identities. Windows/mobile hardware execution and maintainer play-through are not newly verified. Historical saves preserve gameplay/orders/RNG while retaining the previously disclosed309 diagnostic differences; raw legacy-state equality is not claimed. Local checks do not claim hosted CI success. User removed performance acceptance requirements; no new performance measurement or final performance claim.

[Structured summary](summary.json) | [Raw receipts, commands, logs, test results, provenance and traces](unit-cleanup-32c7ec892.tar.gz)

Archive SHA256: `a0cca3adf7b249d21fc3ebb47290d2ced68624c7a85632b9daeb427f07c767c5`. Maintainer acceptance remains unrecorded; PR is draft.
