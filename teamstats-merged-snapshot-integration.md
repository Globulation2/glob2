# PR #1152 actual merged snapshot integration

Merged revision: `b4c463a439953059c9ac058902cff8a652e87f02`. Actual master parent: `2e507cdcdf1b76060e04cc77cd80f3cfe231c5e1` (PR #1173). Previously qualified feature head: `ef23867049e1f3a6ae89e6188fa2a3e05d765d2b`, rebased onto `6b95c81c0d438f64e3391ff4785bc95d3436d174` (PR #1166). PR #1173 landed between the final pre-merge fetch and merge. This supplement qualifies the actual merged tree rather than treating the earlier base as the merge parent.

## Interaction

The merged tree preserves standard simulation/presentation capture and adds entity-buffer reuse when a presentation request widens an unchanged boundary. Statistics event inputs are still captured separately. Fixed-deadline and immediate statistics publication invalidate the observation revision before recapture, so the entity reuse path cannot hide a published generation. An independent sub-agent reviewed this interaction and found no blocker; no follow-up code repair was needed.

## Verification

Full routine rebuild of unit and engine targets succeeded on Darwin arm64, Apple Clang 21, Python 3.14.7, `dev_fast=1`, `-O0 -g1`. This is correctness verification, with no new performance claim.

- Unit selection: 100 passed, zero failures/skips. Executor happy paths and edge cases, phases, gradients, header compatibility, replay gates and experimental rules.
- Focused engine selection: 71 cases, 64 passed, zero failures, seven display-only skips. All WorldSnapshot, TeamStatsPipeline and EngineSession cases plus the committed golden match.
- Includes the new same-boundary entity reuse, invalidation and copied-byte accounting cases from PR #1173; cold widening, source/deadline publication, immediate recapture, paused/waiting boundaries, headless stepping and pending save/history coverage.
- Simulation-version guard against the actual master parent passed. `git diff --check` passed; checkout is clean.

The archive contains exact commands, binary SHA-256 hashes, source/platform provenance, raw build/test logs, JUnit reports and generated test artifacts. The earlier rebase report remains the evidence for the broader 254-passed engine inventory and its recorded revisions.

## Limits

This supplement runs macOS native development correctness checks only. No fresh release profiling, sanitizer, Linux/Windows/browser/mobile execution, cross-platform checksum comparison, GUI/gameplay review or shipping/package qualification. Seven engine cases requiring display are skipped explicitly. Earlier profiling claims remain tied to their original measured revisions.
