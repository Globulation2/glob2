# Native coverage storage repair: PR #1272

Tested revision: `b27d3a8ad85d559bbdc1a3e23708e707f18b3720`. Base: `60be419db529347457c8cf80e853d2ab2eff4dcb`.

The old coverage runner failed with `System.IO.IOException: No space left on device` while the coverage build was in progress. The workflow's evidence-upload step never ran. The exact runner annotation is retained in `runner-annotations.json`; [original job](https://github.com/Globulation2/glob2/actions/runs/40498721013/job/122181163598).

## Changes and verification

CI selects line-table debug information and a four-file LLVM online profile merge pool per binary signature. Local defaults remain full debug information and individual process profiles. The same tests, coverage instrumentation/mapping, optimization, timeout, export/HTML and failure-retention rules remain selected. No game, simulation, runtime dependency, save/replay/network format or version changes.

- All 10 coverage tests pass with matching Clang/LLVM 18.1.8. The new test compares complete LLVM coverage exports across all four debug/profile combinations. Each executes 16 processes in four concurrent batches, including one normal failing exit. Every main-function count is 16; failed inputs remain on disk. The per-process layout produces 16 profiles and the pool produces four.
- All four complete JSON exports have SHA256 `709b6c609e3bafb4d20cb3725143f4a6923f7d6fd3754ee750c1c5181f82608a`. The exports, raw profiles, merged profiles, fixture, executable binaries and command list are retained under `profile-proof/`. These binaries are local verification artifacts, not shipping assets.
- The actual `GradientPreparationTest.cpp` instrumented object compiles successfully under both modes with Clang 18. Full debug: 16,266,112 bytes. Line tables: 11,595,832 bytes. Its coverage-map section is byte-identical (SHA256 `14a970d781e7f7a3cef5a49292b180c65db0da95d53b2681da3554769e8338f0`). Exact compiler arguments, object hashes, extracted coverage maps and debug-section records are in `object-comparison/`; large object files are omitted.
- CI Python contracts: 162 cases, no failures, two skips. Documentation: 310 files, zero errors. Parser help and invalid-pool-size exit behavior were checked. Python compilation and `git diff --check` pass.

Environment: Ubuntu 26.04 x86-64, Clang/LLVM 18.1.8. `verification.json` records exact revision, environment and commands. Run `python3 -m unittest discover -s test -p test_run_coverage.py -v` with matching versioned Clang/LLVM 18 tools to repeat the native counter/region test; it skips when those optional tools are unavailable.

## Limits and acceptance

These are measured local artifact sizes and functional counter/region equivalence, not a claim of hosted execution-time or runner-minute savings. The complete hosted coverage build and its peak storage remain unverified. No full-master checkpoint is claimed; no selected check, test or report is removed. No active master run was cancelled.

Author acceptance: the explicit CI-only options preserve the measured LLVM data and retain existing local defaults; focused evidence is sufficient to merge this build/tooling repair under AGENTS.md. The user authorized repair merges. Follow the retained full master run to establish actual recovery and investigate any remaining disk pressure.
