Local verification of PR #962

- Tested commit: `320a67d78ee500d0542ade55f79f0d3ccb1fa4fd`.
- Base revision: `b5c9f3ed276b82a557f582eb0be1b910b93a9a49`; tested the PR head rebased onto current master, including its shared compute-configuration changes. Master fetched before final validation.
- Environment: macOS 26.6.2, arm64, Apple M3; Apple clang 21.0.0; Python 3.14.7. Dependency versions and source/binary SHA-256 hashes are in the evidence manifest. Existing pinned recording prefix; no dependencies changed. Release client build with `release=1 server=0 -j8`.
- Coverage rationale: distinguish reader demand from speculative work, allow shrinking histories, preserve weighted field values and scheduled publication, and verify saves with pending work.

Exact commands (all run from the isolated PR checkout):

```sh
GLOB2_RECORDING_PREFIX=/Users/bradley/.codex/worktrees/e728/glob2/build/darwin/client/release/recording/prefix scons -j8 release=1 server=0 --build=build/depth-feedback unit-tests engine-tests build/depth-feedback/src/glob2
/Users/bradley/.codex/worktrees/e728/glob2/artifacts/depth-benchmark/venv/bin/python test/test_gradient_depth_fit.py
python3 test/run_tests.py --build-dir build/depth-feedback --binary unit --filter 'PathGradient/*' --junit artifacts/depth-feedback-pr/path-gradient.xml
python3 test/run_tests.py --build-dir build/depth-feedback --binary unit --tag pathfinding --junit artifacts/depth-feedback-pr/pathfinding.xml
python3 test/run_tests.py --build-dir build/depth-feedback --binary engine --filter 'BuildingGradientInvalidation/*' --junit artifacts/depth-feedback-pr/building-gradient.xml
python3 test/check_gradient_pipeline.py build/depth-feedback/src/glob2 --output artifacts/depth-feedback-pr/pipeline
git diff --check
```

Native build, source checks, 14 fitter tests, and 39 distinct native cases passed (7 path-gradient + 13 pathfinding, with 3 overlapping, + 22 building-gradient cases; no skips). Fitter tests used the repository-pinned numpy 2.4.6 and matplotlib 3.11.2 in an ignored local environment.

Game compatibility check passed (exit 0): identical 1,024-tick traces/outcomes across 0/1/2/4/8 workers and resource delays 1/3/8; building delays 1/4/8; all seven building-depth modes (table/full/lazy plus four λ points); 8 resource-pipeline save phases and 13 building-pipeline phases resumed at three worker counts (63 continuations); invalid-option/delay rejection. All 13 building saves caught fields in flight. Pending save completion did not change resumed execution.

Limits: Native macOS arm64 coverage only. Linux, Windows, browser, and ThreadSanitizer were not verified for this PR. Hosted PR checks passed cheap contracts only; the expensive hosted matrix was not requested. No rules, saved bytes or replay/network acceptance boundaries change, so no simulation-version bump is needed. Automated checks cover logical execution; gameplay feel was not manually play-tested. Additional performance measurements were cancelled at the user's request; this PR makes no final-default whole-game speedup claim. The λ curve was recomputed from the original lazy dataset, whose traces already represent reader demand; coefficients are retained, with λ=0.1 selected as the default.

Evidence: [environment and binary hashes](https://github.com/Globulation2/glob2/blob/evidence/building-depth-reader-demand/environment.json), [logs and JUnit reports](https://github.com/Globulation2/glob2/tree/evidence/building-depth-reader-demand), and [all generated pipeline fixtures, saves, replay/checksum traces, manifests and command logs](https://github.com/Globulation2/glob2/blob/evidence/building-depth-reader-demand/pipeline.tar.gz). The archive includes unsuccessful attempts to catch pending jobs, preserving the complete verifier output.

Maintainer acceptance: the requesting maintainer authorized the selected λ=0.1 and merge after final verification. This evidence supports the stated macOS coverage and explicitly records the omitted platforms.
