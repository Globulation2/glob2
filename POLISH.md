# Independent review and cleanup

Final source: `478b9b4bae0aaf177b40c395f44466e8d2aa3152`.
Previous measured source: `37d609766374e7efc1679d643c7d18cde84364ab`.

Two fresh reviewers examined the PR independently. `polish_core_review` reviewed arithmetic, architecture, aliasing, snapshots and comments. `polish_tests_review` reviewed the benchmark, regression coverage and documentation. Neither found a defect in accepted engine results.

Addressed findings:

- Document the terrain-ID → cost-class → edge-slot relationship and initialized prefixes. Explain why queue reservation uses maximum attempts per source cell, including rejected writes, rather than summing across terrain classes.
- Correct the misleading equal-versus-distinct cost comment. Explain conservative uniform selection, including goals and distant non-forbidden cells; reverse destination-entry charging; positive-edge ring safety; and lazy snapshot/profile ownership.
- Reformat dense C++ benchmark code and split its Python driver into adapter, validation, snapshot, build, manifest and measurement helpers.
- Require all frozen baseline instrumentation anchors to match exactly. Preserve existing benchmark evidence rather than overwrite it; show failed-case commands and diagnostics. Reject malformed custom cases and overrides of the paired sampling protocol.
- Give strategic benchmark results an independent 64-bit heap oracle, instead of treating the frozen baseline as the expected result. Reject unsupported synthetic production costs, strategic propagation caps/deferred seeds, malformed integers, and dimensions that overflow temporary coordinate arithmetic.
- Add ordinary eager-specialization regressions for uniform fields, differently priced goals, forbidden outliers, distant cheaper cells, deferred seeds, no goals, all-blocked fields, caps, rectangular/thin grids and workspace reuse. Also check the Map eager entry point in the uniform/lazy test.
- A reviewer caught that an initial distant-cell fixture was placed at the toroidal antipode and could not improve onward routes. It was moved to one-third coordinates; a separate assertion proves that the cheaper cell changes expected routes. Independent reviewer calculation found 60, 16 and 5 changed cells in the three shapes.
- Document the profile/queue/snapshot architecture, distinct strategic metric, all five lazy layouts, repeated snapshot timing, mode restrictions and measurement limits. Register eleven fast Python contract tests in cheap CI.

The production C++ token stream is unchanged in all four engine files. A fresh committed-source full build produces the exact same game executable SHA-256 as the previously measured binary:

`74d14e8c9a22a54c76125bfb664b7c059ebb8c15f8a87fca9910fd5aa5e157b1`

This is a cleanup of the engine and a correctness/maintainability improvement to its tests and tooling, not a new optimization. Earlier performance and full-game evidence remains attributed to its original revision. We make no new speed claim from the smoke runs. The prior GCC maybe-uninitialized warning for the one-class SIMD specialization remains; the earlier independent arithmetic review explained why its constructor/lookup invariants initialize every accessed lane. No warning suppression or extra hot-loop initialization was added.

Final verification:

- Full committed-source production/unit/engine build: exit 0.
- Unit suite: 801 passed, 17 display/opt-in skips, no failures.
- Focused engine gradient/invalidation/continuation suite: 20 passed, no skips/failures.
- Python benchmark contracts: 11 passed; Python syntax and workflow YAML checks passed.
- Native SSE2 smoke matrix: 221 cases × two allocation layouts × cold/warm pairs = 1,768 samples, all oracle checks pass.
- Separately instrumented ASan/UBSan smoke matrix: the same 221 cases / 1,768 samples pass, with no sanitizer errors. Timing is not used for performance claims.
- Twelve invalid standalone CLI cases exit 1 with diagnostics, no output samples and no large allocation attempts.
- Production source lexical-token comparison and byte-identical final executable check pass; `git diff --check` passes.

Exact commands and hashes are retained in `commands.md`, `final-manifest.json`, benchmark manifests, build logs and `../polish-core/`. The existing archived per-tick/save/replay/parallel and cross-kernel evidence remains applicable to the identical game executable. No new full-game or real-ARM performance measurements were made. Full-engine non-Linux/x86-64 limitations remain as documented in the original report. PR remains stacked on ecology PR #787; master integration must be refreshed when retargeting.

## Download

[Review evidence archive](polish-evidence.tar.gz), SHA-256 `1e4d07c017bc15bba79d67990f8f7a75a2af06ee851ce0a783fecda6d64ca229`. Extract at a separate repository root with `tar -xzf polish-evidence.tar.gz`; original paths are under ignored `artifacts/gradient-optimization/`. The archive includes a per-file SHA-256 manifest.
