# Building capability refactor: checkpoint20 evidence

This archive records intermediate verification for draft PR805. It is not final acceptance evidence for the newer source on the integration branch.

Tested checkpoint20: cbb8d6a1964f571f64088b2c074c94e25810c6ce. Functional comparison baseline: 71d7eee1bb2c7105d7f1711a799c967c96687cca. Linux x86-64, GCC13 release builds; build logs include exact compiler flags and dependency paths. Per-run commands, source identifiers, executable hashes, raw results and file checksums are retained inside the archive. The top-level source.json identifies curation time, not the tested revision.

- Native full run: 814 passed, one reproduced baseline ImageAssets failure, 133 skipped.
- Browser full run: 27/33 passed. Six shared-scripting runs failed because repository examples were absent from the browser package. A packaging fix was committed subsequently; this archive does not establish that fix passed.
- All 15 native/browser simulation trace comparisons matched: Chromium, Firefox and WebKit, serial and threaded paths. Custom building and market experiment tests passed in those browsers.
- The scripting corpus comparison retains its raw path-label differences and explicit case-label mapping; compared contents matched, but failed suites are not accepted as passing execution evidence.
- Controlled no-AI feeding scenes matched across 198 measurement rows. Paired gameplay exposed a material Maxima growth regression; that regression remains under correction. Raw unsuccessful controls are retained.
- Historical performance diagnostics and assembly are included only as diagnostics. The required final interleaved performance gate has not run.
- Windows, macOS, Android and iOS execution and human gameplay review remain unverified.

The archive contains files.json with SHA256 for each artifact. Local filesystem paths in command manifests describe reproduction inputs; they are not links needed to access the evidence. Source and baseline revisions are available from the repository.

Archive SHA256: 7235817bfaf9c356f8b70669fec12fbdd8d50cff3bb2a2e978b8af261aa58b37
