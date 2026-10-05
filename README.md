# Terrain refactor verification evidence

PR: https://github.com/Globulation2/glob2/pull/773

Tested source: `1abebe9e6d8d1a805319302edbc27fd291ad1ec8`, based on `934c3c588bea51ec7421152e15f5168b2aaf5502`. The source was committed after validation with tracked-diff and every new-file hash confirmed unchanged. Production binary SHA256: `00a8d43494919ea78900d77214f93e75cf7516c8f6e7bceeb393120d715330ca`.

Integration assessed against master `fbac1d73b8645684850dfcc5ab2f6542a3e5758f`; Git's merge tree is conflict-free. The overlapping upstream changes are an already-applied test correction and independent CI artifact-list additions. No unnecessary rebase was performed.

This evidence branch is intentionally separate from product history and is not intended for merging. The archive contains build/test logs and JUnit results; source/binary/input hashes; commands and scripts; performance samples; ecology train/holdout reports and paired-game analysis; independent oracle sources; screenshots; representative JavaScript save/replay/checksum artifacts; and both AI feeding diagnostic final checkpoints. `manifest.json` identifies every included file and its SHA256. Repeated full raw telemetry and duplicate traces are retained locally but omitted from this compact bundle.

Read `validation.md`, `performance.md`, `ecology-assessment.md`, `cortex-findings.md`, and `maxima-assessment.md` for results and limitations. The historical validation narrative says the changes were uncommitted because it was written before the exact source was committed; the tested commit above is authoritative.

Extract with `tar -xzf terrain-validation-compact.tar.gz`. Paths inside reports refer to the archive layout or the original workspace. Linux full-game validation does not establish full-game Windows/macOS/browser/ARM equivalence. The isolated ARM64/NEON checks cover kernels only. Human gameplay review and the observed midgame hunger warning remain explicitly outstanding.

Maintainer approval to merge was given in the task after those limitations were reported. Hosted checks are not represented as having passed by this evidence.
