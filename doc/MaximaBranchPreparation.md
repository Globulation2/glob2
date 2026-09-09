# AI Maxima branch preparation

Prepared from `master` at `14b99bc82de940a095b816933ae4653274f6425a`.
All branches are local; nothing was pushed and no PR was opened.

## Branches

- `codex/ai-maxima-testing`: continue development and tournaments here. Contains
  all 161 source files from the original working changes, organized below.
- `codex/ai-maxima-core`: the initial AI feature plus its tests and design docs,
  ending at `7bdfeb81`. This is a starting point for the final PR, not an
  automatically updated mirror of the testing branch.
- `codex/ai-maxima-snapshot`: one recovery commit, `91e96d18`, preserving the
  original combined source tree before separation.

The combined eight implementation commits reproduce the snapshot tree exactly.
Build products, app bundles, disk images, IDE settings, local `output/` files,
and tournament results remain on disk outside Git. Local product exclusions
are in `.git/info/exclude`; the existing tournament exclusions are committed
with the research tooling.

## Commit boundaries

| Commit | Scope | Final PR treatment |
| --- | --- | --- |
| `8feaf06b` | Standalone Maxima, strategy files, registration and required game integration | Keep |
| `7bdfeb81` | Core native/policy tests and AI design/configuration docs | Keep |
| `a10d36e9` | macOS/Homebrew/SCons and rendering compatibility | Optional platform PR |
| `5dd240a0` | Echo fixes, Castor tie-breaking, Nicowar defense and fixed default strategy | Optional opponent/engine PR |
| `45de9438` | Speed controls, Maxima HUD and topology overlays, overlay test | Optional inspection UI |
| `175ab5ff` | RNG checkpoint persistence, absolute save paths and load diagnostics | Optional engine PR |
| `80ff61b3` | Tournament entry points, telemetry, checkpoint workflows and observer behavior | Testing only |
| `641293cc` | Campaigns, optimizers, research tests, fixture map and validation reports | Testing only |

This preparation guide is a separate workflow commit and can also be omitted
from the final PR.

Some shared files necessarily belong to the AI feature: factory/name/build
registration, legacy roster and save migration, per-match strategy resolution,
building lifetime identities, the shared worker limit and diagnostic interfaces.
The dormant telemetry switch is referenced directly by Maxima. Removing these
dependencies would require a code refactor, not just dropping optional commits.

The core uses save version **92**. Checkpoint support adds version **93** and
serialized RNG state. Echo's version-85 construction-lock serialization stays
in core so its saves and Maxima's legacy loader remain compatible. Treat
version-93 tournament checkpoints as testing-branch artifacts: the extracted
version-92 build cannot load them. Retest the extracted PR build with fresh
games or supported saves.

## Continuing work

Commit Maxima behavior, strategy data and core regression changes separately
from engine, UI and tournament changes. Use `feat(maxima)`, `fix(maxima)` or
`test(maxima)` for core work, and `engine`, `debug` or `tournament` for optional
work. Split shared-file hunks when necessary. Avoid mixing later core fixes into
a tournament commit, because extraction would then require splitting it again.

The current core does not include tournament commands. Tests that invoke those
commands or read optimizer code are introduced by the research layer; the
topology UI test is introduced by the inspection layer. Save-version assertions
are updated with the checkpoint layer.

## Preparing the final PR after testing

Keep the testing branch as the reproducible tournament record. Create the PR
branch from the core branch, then cherry-pick subsequent core commits in order:

```sh
git switch -c codex/ai-maxima-pr codex/ai-maxima-core
# git cherry-pick <later-core-commit> <next-core-commit>
```

Include platform fixes separately if the target build needs them, or land a
platform PR first. On this Mac, `a10d36e9` is required to build with the installed
Homebrew toolchain; it is not required by Maxima's runtime. Keep optional engine
behavior changes out of the final test environment when measuring the final AI.
Review the resulting diff against the current upstream base and rerun the build
and AI regressions before opening the PR.

An alternative is to branch from the completed testing branch and revert the
optional commits. For this initial snapshot, revert in the following order:

```sh
git revert --no-commit 641293cc 80ff61b3 175ab5ff 45de9438 5dd240a0 a10d36e9
git commit -m "Remove optional testing and engine layers from Maxima PR"
```

Run this on the new PR branch, after separately omitting this workflow guide.
Later optional commits must be removed first, in reverse dependency order;
later core commits should remain. Do not blindly revert every commit after
`7bdfeb81` once development resumes. The tournament entry points depend on both
checkpoint support and speed controls, so remove the entry points before those
support layers. Reverts produce the desired final diff but retain the original
optional work in commit history; extraction from core gives a cleaner history.

## Verification at preparation time

- All 161 original source-file hashes unchanged; combined source tree identical
  to `codex/ai-maxima-snapshot`.
- Reversing all six optional commits in an isolated index exactly reproduces
  the `codex/ai-maxima-core` tree.
- Fresh optimized build passed in `/Users/bradley/glob2-maxima-core-check`, using
  core plus only `a10d36e9` for macOS build compatibility:
  `scons --build=build-tournament release=1 -j4 build-tournament/src/glob2`.
- All nine native regression programs passed using
  `python3 test/run_maxima_implementation_regressions.py --build-dir build-tournament`:
  combat, implementation, farming, barrier scenarios, economy, director,
  standalone tactics, standalone farming and standalone placement.
- Core Python suite: 85 tests, 82 passed, three existing policy failures. The
  same failures reproduce against the original combined sources: expected
  schema count 699 versus 689; two additional unused barrier parameters; and
  the farming-policy literal inventory differs from its manifest. These are
  recorded, not silently updated to match the implementation.
- The combined source also has a reconnaissance test expecting save version 92
  while checkpoint support uses 93. The broad research suite was interrupted
  after stalling in
  `MaximaOptimizerContractTest.test_portfolio_cluster_groups_mixed_configuration_results`
  at `PortfolioCluster.run_many` / `pending.join()`.

Build and test logs, the original patch/hash manifest and the commit-splitting
scripts are retained locally in `.git/maxima-prep/`.
